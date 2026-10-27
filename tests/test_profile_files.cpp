// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tests of the one-file-per-profile layer (src/profile_files.hpp): slugger, loader (isolation of broken files,
// duplicates, file over legacy section, limits, diagnostics), writer (changed files only, delete, rename order,
// stamp check, refusing links), stamps and the debounce. Host independent: temp directories only; the assertions
// that need symlinks are skipped where they cannot be made.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <system_error>
#include <vector>

#include "platform/fs.hpp"
#include "profile_files.hpp"
#include "testutil.hpp"
#include "utf8.hpp"

using namespace spn;
namespace stdfs = std::filesystem;

static void put(const std::string &p, const std::string &c) {
    stdfs::create_directories(stdfs::path(p).parent_path());
    std::ofstream f(p, std::ios::binary);
    f << c;
}
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}
static bool exists(const std::string &p) { return fs::pathExists(p); }
static bool makeSymlink(const std::string &target, const std::string &link) {
    std::error_code ec;
    stdfs::create_symlink(target, link, ec);
    return !ec;
}

static const FileDiag *findDiag(const std::vector<FileDiag> &d, const std::string &path) {
    for (const FileDiag &x : d)
        if (x.path == path) return &x;
    return nullptr;
}
static const FileDiag *findProfile(const std::vector<FileDiag> &d, Section::Kind k, const std::string &name) {
    for (const FileDiag &x : d)
        if (x.origin == FileDiag::PROFILE_FILE && x.kind == k && x.name == name) return &x;
    return nullptr;
}
static bool anyFatal(const std::vector<FileDiag> &d) {
    for (const FileDiag &x : d)
        if (x.fatal) return true;
    return false;
}

// ---- slugger ----------------------------------------------------------------------------------

static void testSlug() {
    assert(profileSlug("SpaceMouse Pro") == "spacemouse pro");
    assert(profileSlug("Firefox") == "firefox" && profileSlug("firefox") == "firefox");  // case folded
    // forbidden characters, controls
    assert(profileSlug("a/b\\c:d*e?f\"g<h>i|j") == "a_b_c_d_e_f_g_h_i_j");
    assert(profileSlug(std::string("a\tb\x01") + "c") == "a_b_c");
    assert(profileSlug("a\xC2\x85" "b") == "a_b");        // C1 control U+0085
    assert(profileSlug("a\xFF" "b") == "a_b");              // invalid UTF-8 byte
    // leading / trailing dot and space
    assert(profileSlug(".hidden") == "_hidden");
    assert(profileSlug("..") == "__");
    assert(profileSlug("name.") == "name_" && profileSlug("name ") == "name_");  // (names are trimmed by the loader)
    assert(profileSlug(" x") == "_x");
    // path traversal attempts never yield a separator or a dot name
    for (const char *evil : {"../../etc/passwd", "..\\..\\x", "/abs", "C:\\x", "a/../b", ".", "..", "...", "./x"}) {
        const std::string s = profileSlug(evil);
        assert(!s.empty() && s.find('/') == std::string::npos && s.find('\\') == std::string::npos);
        assert(s[0] != '.' && s != "." && s != "..");
        assert(s.find(':') == std::string::npos);
    }
    // reserved Windows names (also with an extension), case independent
    for (const char *n : {"CON", "con", "PRN", "AUX", "NUL", "COM1", "com9", "LPT1", "lpt9"})
        assert(profileSlug(n) == utf8::lowerAscii(n) + "_");
    assert(profileSlug("con.x") == "con_.x");
    assert(profileSlug("COM0") == "com0" && profileSlug("COM10") == "com10" && profileSlug("console") == "console");
    // UTF-8 is kept, only ASCII is folded
    assert(profileSlug("Gr\xC3\xB6\xC3\x9F" "e") == "gr\xC3\xB6\xC3\x9F" "e");
    assert(profileSlug("\xC3\x84") == "\xC3\x84");  // no non-ASCII folding
    // hash: over the folded name, stable value
    assert(profileNameHash("Foo") == profileNameHash("foo"));
    assert(profileNameHash("foo") != profileNameHash("bar"));
    assert(profileNameHash("foo").size() == 8);
    assert(profileNameHash("a") == "e40c292c");  // FNV-1a 32 of "a"
    assert(profileSlugHashed("Foo") == "foo-" + profileNameHash("foo"));
    // length: cut at a UTF-8 boundary, hash suffix, at most 80 bytes, stable
    const std::string longAscii(200, 'x');
    const std::string sl = profileSlug(longAscii);
    assert(sl.size() == kMaxSlugBytes && sl.compare(sl.size() - 9, 1, "-") == 0);
    assert(sl.substr(sl.size() - 8) == profileNameHash(longAscii));
    assert(profileSlug(std::string(80, 'y')).size() == 80 && profileSlug(std::string(80, 'y')).find('-') == std::string::npos);
    assert(profileSlug(std::string(81, 'y')).size() == 80);
    std::string longUtf;
    for (int i = 0; i < 60; i++) longUtf += "\xC3\xA4";  // 120 bytes
    const std::string lu = profileSlug(longUtf);
    assert(lu.size() <= kMaxSlugBytes && utf8::isValid(lu));
    // two long names that differ only after the cut do not collide
    assert(profileSlug(longAscii + "a") != profileSlug(longAscii + "b"));
    // case-fold collision: the same name modulo ASCII case is the same profile and the same slug
    assert(profileSlug(longAscii + "A") == profileSlug(longAscii + "a"));
    // a cut that would end in a dot or space does not
    const std::string dots = std::string(70, 'a') + std::string(20, '.');
    const std::string ds = profileSlug(dots);
    assert(ds.size() <= kMaxSlugBytes && ds.back() != '.' && ds.find("._") == std::string::npos);
}

// ---- loader -----------------------------------------------------------------------------------

static void testLoadBasics(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\nSCROLL_V_SENS = 7\n");
    put(d + "/devices/pro.conf", "[device:SpaceMouse Pro]\nSCROLL_V_SENS = 8\n");
    put(d + "/apps/fx.conf", "# comment\n[app:Firefox]\nZOOM_SENS = 12\n");
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    assert(ps.sections.size() == 3 && !anyFatal(diag));
    assert(ps.sections[0].kind == Section::GLOBAL && ps.sections[1].kind == Section::DEVICE &&
           ps.sections[2].kind == Section::APP);
    assert(ps.resolve("SpaceMouse Pro X", "firefox").zoom.sensitivity == 12);
    assert(ps.resolve("SpaceMouse Pro", "").scroll_v.sensitivity == 8);
    const FileDiag *fd = findDiag(diag, d + "/apps/fx.conf");
    assert(fd && fd->name == "Firefox" && fd->section == 2 && !fd->stamp.empty() && !fd->fatal);
    assert(findDiag(diag, d + "/dofscroll.conf") && findDiag(diag, d + "/dofscroll.conf")->origin == FileDiag::GLOBAL_FILE);
    // missing everything: defaults, no error
    const std::string e = tu::makeTempDir("pf-empty");
    ProfileSet p2;
    assert(loadProfileTree(e, &p2, &diag) && p2.sections.size() == 1 && !anyFatal(diag));
    tu::removeAll(e);
    // loadProfileTreeAt with a legacy file name looks next to it
    put(d + "/old.conf", "[global]\nTICK_HZ = 100\n");
    ProfileSet p3;
    assert(loadProfileTreeAt(d + "/old.conf", &p3, &diag) && p3.sections.size() == 3);
    assert(configDirOf(d + "/old.conf") == d && configDirOf("x.conf") == "." && configDirOf("/x.conf") == "/");
    // the single-file reader is unchanged and does not look at devices/
    ProfileSet p4;
    std::vector<std::string> w;
    assert(loadLegacyProfileFile(d + "/dofscroll.conf", &p4, &w) && p4.sections.size() == 1);
}

static void testIsolation(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/devices/good.conf", "[device:Good]\nSCROLL_V_SENS = 9\n");
    put(d + "/devices/nohdr.conf", "SCROLL_V_SENS = 9\n");                       // keys without a header = global keys
    put(d + "/devices/two.conf", "[device:A]\nENABLED=0\n[device:B]\nENABLED=0\n");  // several sections
    put(d + "/devices/wrongkind.conf", "[app:X]\nENABLED=0\n");                    // kind != directory
    put(d + "/devices/empty.conf", "");                                            // no section
    put(d + "/devices/glob.conf", "[global]\nTICK_HZ=100\n[device:G]\n");           // global keys in a profile file
    put(d + "/apps/good.conf", "[app:Fine]\nZOOM_SENS = 3\nBOGUS_KEY = 1\n");      // bad line: warning only
    put(d + "/devices/.hidden.conf", "[device:Hidden]\n");                          // dot files are not read
    put(d + "/devices/note.txt", "[device:Txt]\n");                                  // other extensions neither
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    assert(ps.find(Section::DEVICE, "Good") != ProfileSet::npos);
    assert(ps.find(Section::APP, "Fine") != ProfileSet::npos);
    assert(ps.find(Section::DEVICE, "A") == ProfileSet::npos && ps.find(Section::DEVICE, "B") == ProfileSet::npos);
    assert(ps.find(Section::DEVICE, "Hidden") == ProfileSet::npos && ps.find(Section::DEVICE, "Txt") == ProfileSet::npos);
    assert(ps.find(Section::DEVICE, "G") == ProfileSet::npos);
    assert(ps.sections.size() == 3);  // global + Good + Fine
    for (const char *f : {"nohdr", "two", "wrongkind", "empty", "glob"}) {
        const FileDiag *x = findDiag(diag, d + "/devices/" + f + ".conf");
        assert(x && x->fatal && x->section == ProfileSet::npos && !x->warnings.empty());
    }
    const FileDiag *fine = findDiag(diag, d + "/apps/good.conf");
    assert(fine && !fine->fatal && !fine->warnings.empty() && fine->section != ProfileSet::npos);
    assert(!findDiag(diag, d + "/devices/.hidden.conf") && !findDiag(diag, d + "/devices/note.txt"));
    // the good profiles still resolve
    assert(ps.resolve("Good", "Fine").zoom.sensitivity == 3);
    assert(ps.resolve("Good", "").scroll_v.sensitivity == 9);
}

static void testDuplicates(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/devices/a.conf", "[device:Same]\nSCROLL_V_SENS = 1\n");
    put(d + "/devices/b.conf", "[device:SAME]\nSCROLL_V_SENS = 2\n");   // same header name, folded
    put(d + "/apps/same.conf", "[app:Same]\nZOOM_SENS = 3\n");          // other kind: no clash
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    assert(ps.sections.size() == 3);
    const FileDiag *a = findDiag(diag, d + "/devices/a.conf"), *b = findDiag(diag, d + "/devices/b.conf");
    assert(a && !a->fatal && b && b->fatal && b->section == ProfileSet::npos);
    assert(b->warnings.back().find("a.conf") != std::string::npos);
}

static void testLegacyAndPrecedence(const std::string &d) {
    put(d + "/dofscroll.conf",
        "[global]\nTICK_HZ = 200\n[device:Pro]\nSCROLL_V_SENS = 1\n[device:OnlyLegacy]\nSCROLL_V_SENS = 4\n"
        "[app:fx]\nZOOM_SENS = 2\n");
    put(d + "/devices/pro.conf", "[device:pro]\nSCROLL_V_SENS = 9\n");   // file wins over the legacy section
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    assert(ps.sections.size() == 4);  // global, devices (legacy before files: OnlyLegacy, pro), apps (fx)
    assert(ps.sections[1].pattern == "OnlyLegacy" && ps.sections[2].pattern == "pro" && ps.sections[3].pattern == "fx");
    assert(ps.resolve("pro", "").scroll_v.sensitivity == 9);
    assert(ps.resolve("OnlyLegacy", "").scroll_v.sensitivity == 4 && ps.resolve("", "fx").zoom.sensitivity == 2);
    int shadowed = 0, legacy = 0;
    for (const FileDiag &x : diag) {
        if (x.origin != FileDiag::LEGACY_SECTION) continue;
        legacy++;
        if (x.fatal) {
            shadowed++;
            assert(x.name == "Pro" && !x.warnings.empty());
        }
    }
    assert(legacy == 3 && shadowed == 1);
    // a defective file does not shadow the legacy section
    put(d + "/devices/pro.conf", "garbage without section\n[device:Pro]\n[device:Pro2]\n");
    assert(loadProfileTree(d, &ps, &diag));
    assert(ps.resolve("Pro", "").scroll_v.sensitivity == 1);
    // legacyPart: what dofscroll.conf must keep
    const ProfileSet lp = legacyPart(ps, diag);
    assert(lp.sections.size() == 4 && lp.find(Section::DEVICE, "Pro") != ProfileSet::npos);
    // an old UI (mixed versions) that rewrote the single file: still loads, the file wins again
    put(d + "/devices/pro.conf", "[device:Pro]\nSCROLL_V_SENS = 9\n");
    assert(loadProfileTree(d, &ps, &diag) && ps.resolve("Pro", "").scroll_v.sensitivity == 9);
}

// All resolutions of a few device and program names, as text, to compare two ProfileSets.
static std::string resolveAll(const ProfileSet &ps) {
    std::string out;
    for (const char *dev : {"", "Pro", "Pro Wireless", "Compact"})
        for (const char *app : {"", "firefox", "firefox-esr", "fox", "org.mozilla.firefox", "blender", "xyz"})
            for (const KV &kv : allKeys(ps.resolve(dev, app))) out += kv.first + "=" + kv.second + ";";
    return out;
}

static void testPriorityAndMigrationOrder(const std::string &d) {
    // an old single file with overlapping patterns: the order of the file decides
    const std::string legacy =
        "[global]\n[device:Pro]\nSCROLL_V_SENS = 1\n[device:Pro Wireless]\nSCROLL_V_SENS = 2\n[device:Pro]\nZOOM_SENS = 9\n"
        "[app:fox]\nZOOM_SENS = 3\nSCROLL_V_SENS = 3\n[app:firefox]\nZOOM_SENS = 4\n[app:fire]\nSCROLL_V_SENS = 5\n"
        "[app:mozilla]\nSCROLL_V_SENS = 6\n[app:blender]\nENABLED = 0\n";
    put(d + "/old/dofscroll.conf", legacy);
    ProfileSet before;
    assert(loadLegacyProfileFile(d + "/old/dofscroll.conf", &before, nullptr));
    // pure legacy through the tree loader: identical resolution (file order kept inside a kind)
    ProfileSet viaTree;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d + "/old", &viaTree, &diag) && resolveAll(viaTree) == resolveAll(before));
    // split: the file names sort in a different order than the legacy file (blender, fire, firefox, fox, mozilla)
    ProfileSet toSplit = before;
    assignMigrationPriorities(&toSplit);
    assert(sectionPriority(toSplit.sections[toSplit.find(Section::APP, "fox")]) == 10);
    assert(sectionPriority(toSplit.sections[toSplit.find(Section::APP, "blender")]) == 50);
    assert(sectionPriority(toSplit.sections[toSplit.find(Section::DEVICE, "Pro Wireless")]) == 20);
    std::string err;
    assert(writeProfileSetSplit(d + "/new", toSplit, nullptr, &err));
    ProfileSet after;
    assert(loadProfileTree(d + "/new", &after, &diag) && !anyFatal(diag));
    assert(resolveAll(after) == resolveAll(before));
    assert(after.resolve("", "firefox").scroll_v.sensitivity == 5);  // "fire" comes after "fox" in the old file
    // without priorities the file names would decide (documents the reason for PRIORITY)
    ProfileSet plain = before;
    assert(writeProfileSetSplit(d + "/plain", plain, nullptr, &err));
    ProfileSet byName;
    assert(loadProfileTree(d + "/plain", &byName, &diag));
    assert(resolveAll(byName) != resolveAll(before));
    // a kind with a single profile gets no PRIORITY; the key survives a save and a UI-style rewrite
    ProfileSet one;
    one.findOrAdd(Section::APP, "solo");
    assignMigrationPriorities(&one);
    assert(one.sections[1].kv.empty());
    const size_t i = after.find(Section::APP, "fox");
    Settings st = after.sectionSettings(i);
    after.setSectionFromSettings(i, st);
    assert(sectionPriority(after.sections[i]) == 10);
    // order: priority ascending, ties by file name, negative before positive
    put(d + "/p/dofscroll.conf", "[global]\n");
    put(d + "/p/apps/a.conf", "[app:A]\nPRIORITY = 5\n");
    put(d + "/p/apps/b.conf", "[app:B]\nPRIORITY = -5\n");
    put(d + "/p/apps/c.conf", "[app:C]\n");
    put(d + "/p/apps/d.conf", "[app:D]\n");
    ProfileSet pr;
    assert(loadProfileTree(d + "/p", &pr, &diag) && !anyFatal(diag));
    assert(pr.sections[1].pattern == "B" && pr.sections[2].pattern == "C" && pr.sections[3].pattern == "D" &&
           pr.sections[4].pattern == "A");
    assert(findDiag(diag, d + "/p/apps/a.conf")->section == 4 && findDiag(diag, d + "/p/apps/b.conf")->section == 1);
    // PRIORITY is validated like every key and ignored in [global]
    Settings probe;
    std::string e2;
    assert(applyKey(probe, "PRIORITY", "1000", &e2) && applyKey(probe, "priority", "-1000", &e2));
    assert(!applyKey(probe, "PRIORITY", "1001", &e2) && !applyKey(probe, "PRIORITY", "x", &e2));
    put(d + "/p/apps/bad.conf", "[app:Bad]\nPRIORITY = 5000\n");
    assert(loadProfileTree(d + "/p", &pr, &diag) && !findDiag(diag, d + "/p/apps/bad.conf")->warnings.empty());
}

static void testLimits(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\n");
    // too large
    put(d + "/devices/big.conf", "[device:Big]\n# " + std::string(kMaxProfileFileBytes, 'x') + "\n");
    // too many files: kMaxProfileFiles + 3 in apps/
    for (size_t i = 0; i < kMaxProfileFiles + 3; i++) {
        char n[32];
        std::snprintf(n, sizeof n, "p%04zu", i);
        put(d + "/apps/" + n + ".conf", std::string("[app:") + n + "]\nENABLED = 0\n");
    }
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    const FileDiag *big = findDiag(diag, d + "/devices/big.conf");
    assert(big && big->fatal && ps.find(Section::DEVICE, "Big") == ProfileSet::npos);
    assert(ps.sections.size() <= kMaxSections);
    assert(ps.sections.size() == 1 + kMaxProfileFiles);
    bool dirWarn = false;
    for (const FileDiag &x : diag)
        if (x.origin == FileDiag::DIRECTORY && x.path == d + "/apps") dirWarn = true;
    assert(dirWarn);
    assert(findDiag(diag, d + "/apps/p0000.conf") && !findDiag(diag, d + "/apps/p0000.conf")->fatal);
    assert(!findDiag(diag, d + "/apps/p0" + std::to_string(kMaxProfileFiles + 2) + ".conf"));
    // total count over the limit: legacy sections fill the budget, files beyond it are fatal
    std::string legacy = "[global]\n";
    for (int i = 0; i < 200; i++) legacy += "[app:L" + std::to_string(i) + "]\nENABLED=0\n";
    put(d + "/dofscroll.conf", legacy);
    assert(loadProfileTree(d, &ps, &diag));
    assert(ps.sections.size() == kMaxSections);
    int tooMany = 0;
    for (const FileDiag &x : diag)
        if (x.origin == FileDiag::PROFILE_FILE && x.fatal) tooMany++;
    assert(tooMany > 0);
}

static void testUnreadableGlobal(const std::string &d) {
    // dofscroll.conf is a directory: false, *out untouched
    stdfs::create_directories(d + "/dofscroll.conf");
    ProfileSet ps;
    ps.findOrAdd(Section::APP, "keep");
    std::vector<FileDiag> diag;
    assert(!loadProfileTree(d, &ps, &diag));
    assert(ps.sections.size() == 2 && diag.size() == 1 && diag[0].fatal && diag[0].origin == FileDiag::GLOBAL_FILE);
}

static void testSymlinksRefused(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/target.txt", "[device:Linked]\nENABLED = 0\n");
    if (!makeSymlink(d + "/target.txt", d + "/devices_link_probe")) return;  // no symlinks here
    stdfs::create_directories(d + "/devices");
    assert(makeSymlink(d + "/target.txt", d + "/devices/linked.conf"));
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    assert(ps.find(Section::DEVICE, "Linked") == ProfileSet::npos);
    const FileDiag *x = findDiag(diag, d + "/devices/linked.conf");
    assert(x && x->fatal);
}

// ---- stamps -----------------------------------------------------------------------------------

static void testStamps(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\n");
    const std::string s0 = profileTreeStamp(d);
    assert(s0 == profileTreeStamp(d) && s0 == profileTreeStampAt(d + "/dofscroll.conf"));
    put(d + "/devices/a.conf", "[device:A]\n");
    const std::string s1 = profileTreeStamp(d);
    assert(s1 != s0);
    put(d + "/devices/a.conf", "[device:A]\nENABLED=0\n");  // size changes
    const std::string s2 = profileTreeStamp(d);
    assert(s2 != s1);
    put(d + "/apps/b.conf", "[app:B]\n");
    assert(profileTreeStamp(d) != s2);
    put(d + "/dofscroll.conf", "[global]\nTICK_HZ=100\n");
    const std::string s3 = profileTreeStamp(d);
    put(d + "/devices/ignored.txt", "x");  // not a profile file
    put(d + "/devices/.dot.conf", "x");
    assert(profileTreeStamp(d) == s3);
    assert(profileFileStamp(d + "/missing", false).empty());
    assert(!profileFileStamp(d + "/devices/a.conf", false).empty());

    StampDebounce db;
    db.reset("A");
    assert(!db.settled("A"));
    assert(!db.settled("B"));  // first sight of a change
    assert(db.settled("B"));   // the same change at the next poll
    assert(!db.settled("C"));  // still moving: starts over
    assert(!db.settled("A"));  // back to the applied state: nothing to do
    assert(!db.settled("C"));
    db.reset("C");
    assert(!db.settled("C"));
}

// ---- writer -----------------------------------------------------------------------------------

static Section mkSec(Section::Kind k, const std::string &name, const std::string &key = "", const std::string &val = "") {
    Section s;
    s.kind = k;
    s.pattern = name;
    if (!key.empty()) s.kv.emplace_back(key, val);
    return s;
}

static void testWriter(const std::string &d) {
    // new profile
    WriteResult r = saveProfileFile(d, mkSec(Section::DEVICE, "SpaceMouse Pro", "SCROLL_V_SENS", "8"), nullptr);
    assert(r.ok() && r.path == d + "/devices/spacemouse pro.conf" && !r.stamp.empty());
    assert(get(r.path) == "[device:SpaceMouse Pro]\nSCROLL_V_SENS = 8\n");
    // loads back
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    const FileDiag *known = findProfile(diag, Section::DEVICE, "SpaceMouse Pro");
    assert(known && known->stamp == r.stamp && known->section == 1);
    // a second new profile of the same name is a Conflict (a file of this profile exists)
    r = saveProfileFile(d, mkSec(Section::DEVICE, "spacemouse pro"), nullptr);
    assert(r.status == WriteStatus::Conflict);
    // update with the known stamp: ok, only that file changes
    put(d + "/apps/other.conf", "[app:Other]\nENABLED = 0\n");
    const std::string otherStamp = profileFileStamp(d + "/apps/other.conf", false);
    FileDiag k = *known;
    r = saveProfileFile(d, mkSec(Section::DEVICE, "SpaceMouse Pro", "SCROLL_V_SENS", "9"), &k);
    assert(r.ok() && r.path == k.path && get(r.path).find("= 9") != std::string::npos);
    assert(profileFileStamp(d + "/apps/other.conf", false) == otherStamp);
    // the stamp is now old: somebody (the user) edited the file by hand in between
    put(r.path, "[device:SpaceMouse Pro]\nSCROLL_V_SENS = 3\n# by hand\n");
    r = saveProfileFile(d, mkSec(Section::DEVICE, "SpaceMouse Pro", "SCROLL_V_SENS", "10"), &k);
    assert(r.status == WriteStatus::Conflict);
    assert(get(d + "/devices/spacemouse pro.conf").find("by hand") != std::string::npos);  // nothing lost
    // invalid content is never written
    assert(saveProfileFile(d, mkSec(Section::APP, "x", "NO_SUCH_KEY", "1"), nullptr).status == WriteStatus::Invalid);
    assert(saveProfileFile(d, mkSec(Section::APP, "  "), nullptr).status == WriteStatus::Invalid);
    assert(saveProfileFile(d, mkSec(Section::APP, "x", "ENABLED", "1\nTICK_HZ = 1"), nullptr).status == WriteStatus::Invalid);
    assert(saveProfileFile(d, mkSec(Section::GLOBAL, ""), nullptr).status == WriteStatus::Invalid);
    assert(!exists(d + "/apps/x.conf"));
    // comment lines in front (migration)
    r = saveProfileFile(d, mkSec(Section::APP, "Commented", "ENABLED", "0"), nullptr, "# my note\n# second");
    assert(r.ok() && get(r.path) == "# my note\n# second\n[app:Commented]\nENABLED = 0\n");
    assert(saveProfileFile(d, mkSec(Section::APP, "BadComment"), nullptr, "[global]").status == WriteStatus::Invalid);
}

static void testCollisions(const std::string &d) {
    // "a:b" and "a_b" both slug to "a_b": the second gets the hashed name, both load and stay separate
    WriteResult r1 = saveProfileFile(d, mkSec(Section::APP, "a:b", "ENABLED", "0"), nullptr);
    WriteResult r2 = saveProfileFile(d, mkSec(Section::APP, "a_b", "ENABLED", "1"), nullptr);
    assert(r1.ok() && r2.ok() && r1.path != r2.path);
    assert(r1.path == d + "/apps/a_b.conf" && r2.path == d + "/apps/" + profileSlugHashed("a_b") + ".conf");
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag) && !anyFatal(diag) && ps.sections.size() == 3);
    // updating each one finds its own file again
    const FileDiag *k2 = findProfile(diag, Section::APP, "a_b");
    assert(k2 && k2->path == r2.path);
    FileDiag kk = *k2;
    WriteResult r3 = saveProfileFile(d, mkSec(Section::APP, "a_b", "ENABLED", "0"), &kk);
    assert(r3.ok() && r3.path == r2.path);
    // the first profile is removed: the second keeps its hashed file (no churn)
    assert(deleteProfileFile(d, Section::APP, r1.path, "").ok());
    kk.stamp = r3.stamp;
    WriteResult r4 = saveProfileFile(d, mkSec(Section::APP, "a_b", "ENABLED", "1"), &kk);
    assert(r4.ok() && r4.path == r2.path);
    // a defective foreign file on the base name: also treated as taken, never overwritten
    put(d + "/apps/zzz.conf", "junk");
    r1 = saveProfileFile(d, mkSec(Section::APP, "ZZZ", "ENABLED", "0"), nullptr);
    assert(r1.ok() && r1.path == d + "/apps/" + profileSlugHashed("zzz") + ".conf" && get(d + "/apps/zzz.conf") == "junk");
    // both names taken by foreign files: refused
    put(d + "/apps/yyy.conf", "junk");
    put(d + "/apps/" + profileSlugHashed("yyy") + ".conf", "junk");
    assert(saveProfileFile(d, mkSec(Section::APP, "yyy"), nullptr).status == WriteStatus::Refused);
}

static void testDeleteAndRename(const std::string &d) {
    WriteResult r = saveProfileFile(d, mkSec(Section::DEVICE, "Old Name", "ENABLED", "0"), nullptr);
    assert(r.ok());
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    FileDiag known = *findProfile(diag, Section::DEVICE, "Old Name");
    // rename: new file first, old one gone afterwards
    WriteResult rn = saveProfileFile(d, mkSec(Section::DEVICE, "New Name", "ENABLED", "0"), &known);
    assert(rn.ok() && rn.path == d + "/devices/new name.conf" && rn.warning.empty());
    assert(exists(rn.path) && !exists(r.path));
    // crash between the two steps: the old file is still there -> a duplicate that is reported, nothing lost
    put(d + "/devices/old name.conf", "[device:New Name]\nENABLED = 0\n");
    assert(loadProfileTree(d, &ps, &diag));
    int fatal = 0;
    for (const FileDiag &x : diag)
        if (x.fatal) fatal++;
    assert(fatal == 1 && ps.sections.size() == 2 && ps.find(Section::DEVICE, "New Name") != ProfileSet::npos);
    // the old file was edited by hand after it was read: the rename keeps it and says so
    put(d + "/devices/o2.conf", "[device:Two]\nENABLED = 0\n");
    assert(loadProfileTree(d, &ps, &diag));
    FileDiag k2 = *findDiag(diag, d + "/devices/o2.conf");  // hand-named file: canonical name differs
    put(d + "/devices/o2.conf", "[device:Two]\nENABLED = 0\n# edited\n");
    rn = saveProfileFile(d, mkSec(Section::DEVICE, "Two", "ENABLED", "1"), &k2);
    assert(rn.ok() && rn.path == d + "/devices/two.conf" && !rn.warning.empty() && exists(d + "/devices/o2.conf"));
    // hand-renamed file is rewritten under the canonical slug (old removed when unchanged)
    put(d + "/apps/mine.conf", "[app:Canonical Me]\nENABLED = 0\n");
    assert(loadProfileTree(d, &ps, &diag));
    FileDiag k3 = *findDiag(diag, d + "/apps/mine.conf");
    rn = saveProfileFile(d, mkSec(Section::APP, "Canonical Me", "ENABLED", "1"), &k3);
    assert(rn.ok() && rn.path == d + "/apps/canonical me.conf" && !exists(d + "/apps/mine.conf"));
    // rename onto a name owned by another profile's file: that file is never touched
    put(d + "/apps/target.conf", "[app:Target]\nENABLED = 0\n");
    WriteResult rr = saveProfileFile(d, mkSec(Section::APP, "Target", "ENABLED", "1"), nullptr);
    assert(rr.status == WriteStatus::Conflict && get(d + "/apps/target.conf").find("= 0") != std::string::npos);

    // delete: stamp check
    const std::string p = d + "/apps/del.conf";
    put(p, "[app:Del]\n");
    const std::string st = profileFileStamp(p, false);
    put(p, "[app:Del]\nENABLED = 0\n");  // changed after it was read
    WriteResult dr = deleteProfileFile(d, Section::APP, p, st);
    assert(dr.status == WriteStatus::Conflict && exists(p));
    assert(deleteProfileFile(d, Section::APP, p, profileFileStamp(p, false)).ok() && !exists(p));
    assert(deleteProfileFile(d, Section::APP, p, "whatever").ok());  // already gone
    // only files directly in the profile directory of that kind
    put(d + "/dofscroll.conf", "[global]\n");
    assert(deleteProfileFile(d, Section::APP, d + "/dofscroll.conf", "").status == WriteStatus::Refused);
    assert(deleteProfileFile(d, Section::APP, d + "/apps/../dofscroll.conf", "").status == WriteStatus::Refused);
    assert(deleteProfileFile(d, Section::APP, d + "/devices/two.conf", "").status == WriteStatus::Refused);
    assert(deleteProfileFile(d, Section::APP, d + "/apps/sub/x.conf", "").status == WriteStatus::Refused);
    assert(exists(d + "/dofscroll.conf") && exists(d + "/devices/two.conf"));
}

static void testWriterRefusesLinks(const std::string &d) {
    put(d + "/victim.txt", "precious");
    stdfs::create_directories(d + "/apps");
    if (!makeSymlink(d + "/victim.txt", d + "/apps/evil.conf")) return;
    WriteResult r = saveProfileFile(d, mkSec(Section::APP, "evil", "ENABLED", "0"), nullptr);
    assert(r.status == WriteStatus::Refused && get(d + "/victim.txt") == "precious");
    assert(deleteProfileFile(d, Section::APP, d + "/apps/evil.conf", "").status == WriteStatus::Refused);
    assert(exists(d + "/apps/evil.conf") && get(d + "/victim.txt") == "precious");
    // a directory at the target
    stdfs::create_directories(d + "/apps/dir.conf");
    assert(saveProfileFile(d, mkSec(Section::APP, "dir"), nullptr).status == WriteStatus::Refused);
}

static void testGlobalAndSplitWrite(const std::string &d) {
    // legacy file with sections: saveGlobalFile keeps what it is told to keep
    put(d + "/dofscroll.conf", "[global]\nTICK_HZ = 200\n[device:Leg]\nENABLED = 0\n[app:Shadow]\nENABLED = 0\n");
    put(d + "/apps/shadow.conf", "[app:Shadow]\nENABLED = 1\n");
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    const std::string gstamp = findDiag(diag, d + "/dofscroll.conf")->stamp;
    ProfileSet keep = legacyPart(ps, diag);
    assert(keep.sections.size() == 2 && keep.find(Section::DEVICE, "Leg") != ProfileSet::npos);
    keep.sections[0].kv.emplace_back("TICK_HZ", "300");
    WriteResult r = saveGlobalFile(d, keep, gstamp);
    assert(r.ok());
    ProfileSet again;
    assert(loadProfileTree(d, &again, &diag) && again.resolve("", "").tick_hz == 300 &&
           again.find(Section::DEVICE, "Leg") != ProfileSet::npos);
    assert(again.resolve("", "Shadow").enabled);  // the file still wins
    // stale stamp
    put(d + "/dofscroll.conf", "[global]\nTICK_HZ = 111\n");
    assert(saveGlobalFile(d, keep, gstamp).status == WriteStatus::Conflict);
    assert(get(d + "/dofscroll.conf").find("111") != std::string::npos);
    // a directory is never replaced
    const std::string e = tu::makeTempDir("pf-gdir");
    stdfs::create_directories(e + "/dofscroll.conf");
    assert(saveGlobalFile(e, ProfileSet(), "").status == WriteStatus::Refused);
    tu::removeAll(e);

    // whole set in the new layout
    const std::string n = tu::makeTempDir("pf-split");
    ProfileSet full;
    full.sections[0].kv.emplace_back("TICK_HZ", "150");
    full.sections.push_back(mkSec(Section::DEVICE, "SpaceMouse Pro", "SCROLL_V_SENS", "8"));
    full.sections.push_back(mkSec(Section::APP, "Gr\xC3\xB6\xC3\x9F" "e", "ZOOM_SENS", "4"));
    full.sections.push_back(mkSec(Section::APP, "a:b", "ENABLED", "0"));
    full.sections.push_back(mkSec(Section::APP, "a_b", "ENABLED", "1"));
    std::vector<FileDiag> written;
    std::string err;
    assert(writeProfileSetSplit(n, full, &written, &err));
    assert(written.size() == 4 && !anyFatal(written));
    ProfileSet back;
    assert(loadProfileTree(n, &back, &diag) && !anyFatal(diag) && back.sections.size() == 5);
    assert(back.sections[0].kv == full.sections[0].kv);
    for (size_t i = 1; i < full.sections.size(); i++) {
        const size_t j = back.find(full.sections[i].kind, full.sections[i].pattern);
        assert(j != ProfileSet::npos && back.sections[j].kv == full.sections[i].kv);
    }
    assert(get(n + "/dofscroll.conf").find("\n[device:") == std::string::npos);
    // running it again over the same directory overwrites the files of the same profiles
    assert(writeProfileSetSplit(n, full, nullptr, &err));
    // a foreign file on the name stays untouched
    tu::removeAll(n);
    stdfs::create_directories(n + "/apps");
    put(n + "/apps/a_b.conf", "[app:Other]\nENABLED = 0\n");
    assert(writeProfileSetSplit(n, full, nullptr, &err));
    assert(get(n + "/apps/a_b.conf") == "[app:Other]\nENABLED = 0\n");
    tu::removeAll(n);
}

static void testPartialWritesOnlyChangedFiles(const std::string &d) {
    ProfileSet full;
    full.sections.push_back(mkSec(Section::DEVICE, "A", "ENABLED", "0"));
    full.sections.push_back(mkSec(Section::APP, "B", "ENABLED", "0"));
    assert(writeProfileSetSplit(d, full, nullptr, nullptr));
    const std::string sa = profileFileStamp(d + "/devices/a.conf", false);
    const std::string sg = profileFileStamp(d + "/dofscroll.conf", true);
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    FileDiag kb = *findProfile(diag, Section::APP, "B");
    assert(saveProfileFile(d, mkSec(Section::APP, "B", "ENABLED", "1"), &kb).ok());
    assert(profileFileStamp(d + "/devices/a.conf", false) == sa && profileFileStamp(d + "/dofscroll.conf", true) == sg);
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-pf-test");
    int n = 0;
    auto fresh = [&] { return tu::makeTempDir("pf-case" + std::to_string(n++)); };
    testSlug();
    struct T { void (*f)(const std::string &); };
    for (auto f : {testLoadBasics, testIsolation, testDuplicates, testLegacyAndPrecedence, testPriorityAndMigrationOrder, testLimits, testUnreadableGlobal,
                   testSymlinksRefused, testStamps, testWriter, testCollisions, testDeleteAndRename,
                   testWriterRefusesLinks, testGlobalAndSplitWrite, testPartialWritesOnlyChangedFiles}) {
        const std::string d = fresh();
        f(d);
        tu::removeAll(d);
    }
    tu::removeAll(root);
    std::puts("test_profile_files: ok");
    return 0;
}
