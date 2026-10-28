// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Review and audit fixes of the split layout: migration priorities for the maximum number of profiles, notes
// about shadowed legacy sections and comments, labelled parser notes, names with invalid UTF-8, unreadable
// profile directories, legacy locations without profile directories, checked replacement of dofscroll.conf,
// Windows-safe slugs and stale temporary files. Host independent: temp directories only.
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "config.hpp"
#include "platform/fs.hpp"
#include "platform/path_rules.hpp"
#include "profile_files.hpp"
#include "profile_migrate.hpp"
#include "modes.hpp"
#include "testutil.hpp"
#include "ui/model/settings_model.hpp"

using namespace spn;
namespace stdfs = std::filesystem;

static void put(const std::string &p, const std::string &c) {
    stdfs::create_directories(stdfs::path(p).parent_path());
    std::ofstream f(p, std::ios::binary);
    f << c;
}
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static bool has(const std::string &s, const std::string &n) { return s.find(n) != std::string::npos; }
static bool anyWarning(const MigrateResult &r, const std::string &n) {
    for (const std::string &w : r.warnings)
        if (has(w, n)) return true;
    return false;
}

static void testMaxProfilesPriorities(const std::string &root) {
    std::string conf = "[global]\n";
    for (int i = 0; i < 255; i++) {
        conf += "[app:p" + std::to_string(1000 + i) + "]\nZOOM_SENS = " + std::to_string(1 + i % 9) + "\n";
        if (i % 7 == 0) conf += "PRIORITY = " + std::to_string(i % 5) + "\n";
    }
    const std::string d = root + "/max";
    put(d + "/dofscroll.conf", conf);
    ProfileSet before;
    assert(loadLegacyProfileFile(d + "/dofscroll.conf", &before, nullptr) && before.sections.size() == 256);
    const MigrateResult r = migrateToSplit(d);
    assert(r.status == MigrateStatus::Migrated && r.profilesMoved == 255);
    ProfileSet after;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &after, &diag) && after.sections.size() == 256);
    for (const FileDiag &x : diag) assert(!x.fatal && x.warnings.empty());
    for (const Section &s : after.sections)
        if (s.kind == Section::APP) assert(sectionPriority(s) >= kMinPriority && sectionPriority(s) <= kMaxPriority);
    for (int a = 0; a < 255; a += 5)
        for (int b = a + 1; b < 255; b += 37) {
            const std::string app = "p" + std::to_string(1000 + a) + " p" + std::to_string(1000 + b);
            assert(sameSettings(before.resolve("", app), after.resolve("", app)));
        }
}

static void testShadowedAndNotes(const std::string &root) {
    // a profile file exists already: the legacy section with other values is reported, an equal one is not
    const std::string d = root + "/shadow";
    put(d + "/dofscroll.conf",
        "[global]\n[device:Pro]\nSCROLL_V_SENS = 9\n[device:Same]\nSCROLL_V_SENS = 2\n[app:x]\nZOOM_SENS = 3\n");
    put(d + "/devices/pro.conf", "[device:Pro]\nSCROLL_V_SENS = 1\n");
    put(d + "/devices/same.conf", "[device:Same]\nSCROLL_V_SENS = 2\n");
    const MigrateResult r = migrateToSplit(d);
    assert(r.status == MigrateStatus::Migrated && r.profilesAlreadyFiled == 2);
    assert(anyWarning(r, "[device:Pro]") && !anyWarning(r, "[device:Same]"));
    assert(anyWarning(r, "only in the backup"));

    // parser notes of the old file are labelled
    const std::string e = root + "/notes";
    put(e + "/dofscroll.conf", "[global]\nSENS = 1\n[unknown]\nX = 1\n[app:y]\nZOOM_SENS = 3\n");
    const MigrateResult n = migrateToSplit(e);
    assert(n.status == MigrateStatus::Migrated);
    size_t labelled = 0;
    for (const std::string &w : n.warnings) labelled += has(w, "in the old file: ") ? 1 : 0;
    assert(labelled >= 2);

    // comments inside a block stay only in the backup: said once; none inside: no note
    const std::string c = root + "/inner";
    put(c + "/dofscroll.conf", "[global]\n# above the header, moves along\n[app:a]\nZOOM_SENS = 3  # inline\n# inside\nENABLED = 1\n");
    const MigrateResult ci = migrateToSplit(c);
    assert(ci.status == MigrateStatus::Migrated);
    size_t said = 0;
    for (const std::string &w : ci.warnings) said += has(w, "inside the profile blocks") ? 1 : 0;
    assert(said == 1);
    const std::string c2 = root + "/outer";
    put(c2 + "/dofscroll.conf", "[global]\n# above\n[app:a]\nZOOM_SENS = 3\n");
    assert(!anyWarning(migrateToSplit(c2), "inside the profile blocks"));
}

static void testInvalidUtf8Name(const std::string &root) {
    const std::string d = root + "/utf8";
    put(d + "/dofscroll.conf", std::string("[global]\n[app:bad\xFF\xFEname]\nZOOM_SENS = 3\n[app:ok]\nZOOM_SENS = 4\n"));
    const MigrateResult r = migrateToSplit(d);
    assert(r.status == MigrateStatus::Migrated && r.ok());
    ProfileSet ps;
    assert(loadProfileTree(d, &ps, nullptr));
    assert(ps.find(Section::APP, std::string("bad\xFF\xFEname")) != ProfileSet::npos);
}

static void testUnreadableDirectory(const std::string &root) {
    (void)root;
#ifndef _WIN32
    if (::geteuid() == 0) return;  // root reads everything
    const std::string d = root + "/eacces";
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/devices/a.conf", "[device:A]\n");
    ::chmod((d + "/devices").c_str(), 0);
    ProfileSet ps;
    std::vector<FileDiag> diag;
    const bool ok = loadProfileTree(d, &ps, &diag);
    ::chmod((d + "/devices").c_str(), 0700);
    assert(!ok);
    bool fatalDir = false;
    for (const FileDiag &x : diag) fatalDir = fatalDir || (x.origin == FileDiag::DIRECTORY && x.fatal);
    assert(fatalDir);
    assert(loadProfileTree(d, &ps, &diag) && ps.find(Section::DEVICE, "A") != ProfileSet::npos);
    // a file instead of a directory is an error too, a missing directory is not
    put(d + "/apps", "x");
    assert(!loadProfileTree(d, &ps, &diag));
    stdfs::remove(d + "/apps");
    assert(loadProfileTree(d, &ps, &diag));
#endif
}

static void testLegacyLocationHasNoProfileDirs(const std::string &root) {
    const std::string d = root + "/legacyloc";
    put(d + "/spnav-scroll.conf", "[global]\nTICK_HZ = 100\n");
    put(d + "/devices/foreign.conf", "[device:Foreign]\nENABLED = 0\n");
    put(d + "/apps/foreign.conf", "[app:Foreign]\nENABLED = 0\n");
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTreeAt(d + "/spnav-scroll.conf", &ps, &diag) && ps.sections.size() == 1);
    assert(!has(profileTreeStampAt(d + "/spnav-scroll.conf"), "foreign"));
    put(d + "/dofscroll.conf", "[global]\n");
    assert(loadProfileTree(d, &ps, &diag) && ps.sections.size() == 3);
    assert(has(profileTreeStampAt(d + "/dofscroll.conf"), "foreign.conf"));
}

static void testCheckedGlobalReplace(const std::string &root) {
    const std::string d = root + "/global";
    put(d + "/dofscroll.conf", "[global]\nTICK_HZ = 100\n");
    const std::string stamp = profileFileStamp(d + "/dofscroll.conf", true);
    put(d + "/dofscroll.conf", "[global]\nTICK_HZ = 120 # edited by hand meanwhile\n");
    const WriteResult c = saveGlobalText(d, "[global]\n", stamp);
    assert(c.status == WriteStatus::Conflict);
    assert(has(get(d + "/dofscroll.conf"), "120"));
    const WriteResult ok = saveGlobalText(d, "[global]\n", profileFileStamp(d + "/dofscroll.conf", true));
    assert(ok.ok() && !ok.stamp.empty());
}

static void testSlugsAreWindowsSafe() {
    for (const char *n : {"con", "CON .x", "conin$", "CONOUT$", "nul .txt", "aux", "prn", "com1", "LPT9", "com\xC2\xB9", "lpt\xC2\xB2",
                          "COM\xC2\xB3.x", "con .", "con. "}) {
        assert(windowsPathHazard(profileSlug(n) + ".conf") == PathHazard::None);
        assert(windowsPathHazard(profileSlugHashed(n) + ".conf") == PathHazard::None);
    }
    // a fast subset of the property run: short names over a small alphabet
    const char alpha[] = {'c', 'o', 'n', 'm', '1', ' ', '.', '$', 'x'};
    std::string s;
    for (size_t a = 0; a < sizeof alpha; a++)
        for (size_t b = 0; b < sizeof alpha; b++)
            for (size_t c = 0; c < sizeof alpha; c++)
                for (size_t e = 0; e < sizeof alpha; e += 2) {
                    s = {alpha[a], alpha[b], alpha[c], alpha[e]};
                    assert(windowsPathHazard(profileSlug(s) + ".conf") == PathHazard::None);
                }
    // the writer files such a profile under a safe name
    const std::string root = tu::makeTempDir("pf-slug");
    Section sec;
    sec.kind = Section::APP;
    sec.pattern = "CON";
    sec.kv.emplace_back("ZOOM_SENS", "3");
    const WriteResult w = saveProfileFile(root, sec, nullptr, "");
    assert(w.ok() && windowsPathHazard(w.path.substr(w.path.rfind('/') + 1)) == PathHazard::None);
    tu::removeAll(root);
}

static void testStaleTemps(const std::string &root) {
    const std::string d = root + "/tmps";
    stdfs::create_directories(d + "/apps");
    const std::string oldTmp = d + "/apps/a.conf.tmp.0123456789abcdef", newTmp = d + "/apps/b.conf.tmp.0123456789abcdef",
                      other = d + "/apps/c.conf.tmp.notahexvalue123", keep = d + "/apps/d.conf.bak";
    for (const std::string &p : {oldTmp, newTmp, other, keep}) put(p, "x");
    for (const std::string &p : {oldTmp, other, keep})
        stdfs::last_write_time(p, stdfs::file_time_type::clock::now() - std::chrono::hours(72));
    Section sec;
    sec.kind = Section::APP;
    sec.pattern = "z";
    sec.kv.emplace_back("ZOOM_SENS", "3");
    assert(saveProfileFile(d, sec, nullptr, "").ok());
    assert(!stdfs::exists(oldTmp) && stdfs::exists(newTmp) && stdfs::exists(other) && stdfs::exists(keep));
}

// The UI's per-section view (effectiveWithMode) and the daemon's resolve (with the mode's overrides) apply the
// layers through the same functions: the two give the same settings for a program profile.
static void testUiAndResolveAgree(const std::string &root) {
    const std::string md = root + "/uimodes";
    put(md + "/m1.conf", "NAME = M1\nZOOM_SENS = 7\nBUTTON_1 = middleclick\n");
    ModeSet modes = loadModes(md, nullptr);
    const ModeDef *m1 = modes.find("m1");
    assert(m1);
    ProfileSet profs;
    profs.sections[0].kv = {{"MODE", "m1"}, {"V_ACTION", "zoom"}, {"BUTTON_1", "click"}};
    const size_t app = profs.findOrAdd(Section::APP, "ed");
    profs.sections[app].kv = {{"BUTTON_1", "rightclick"}, {"V_ACTION", "wheel_h"}, {"ZOOM_SENS", "2"}};
    const size_t app2 = profs.findOrAdd(Section::APP, "other");
    profs.sections[app2].kv = {{"ZOOM_SENS", "4"}};
    ui::SettingsModel m;
    m.setProfiles(profs, "");
    m.setModes(modes, md);
    for (const char *name : {"ed", "other"}) {
        const size_t i = profs.find(Section::APP, name);
        assert(sameSettings(profs.resolve("", name, &m1->overrides), m.effectiveWithMode(i)));
    }
    const Settings s = m.effectiveWithMode(app);
    assert(s.app_buttons.count(1) && s.app_buttons.at(1) == "rightclick" && s.actions[kLayerApp].chanSet[0]);
}

// A chord with Alt/Super in a profile's channel action is loaded and noted.
static void testProfileChordNote() {
    ProfileSet profs;
    std::vector<std::string> w;
    parseProfilesText("[app:x]\nV_ACTION = keys:alt+left,alt+right\nH_ACTION = keys:ctrl+pageup,ctrl+pagedown\n", "t", &profs, &w);
    assert(w.size() == 1 && has(w[0], "V_ACTION") && has(w[0], "chord"));
    assert(profs.resolve("", "x").actions[kLayerApp].chanSet[ProfileActions::V]);
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-hardening");
    testMaxProfilesPriorities(root);
    testShadowedAndNotes(root);
    testInvalidUtf8Name(root);
    testUnreadableDirectory(root);
    testLegacyLocationHasNoProfileDirs(root);
    testCheckedGlobalReplace(root);
    testSlugsAreWindowsSafe();
    testStaleTemps(root);
    testUiAndResolveAgree(root);
    testProfileChordNote();
    tu::removeAll(root);
    std::puts("test_profile_hardening: ok");
    return 0;
}
