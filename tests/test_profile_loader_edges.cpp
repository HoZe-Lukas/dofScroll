// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Edge cases of the profile file loader and the migration that the neighbours (test_profile_files.cpp,
// test_profile_migrate.cpp, test_profile_hardening.cpp) do not reach: empty and comment-only files, UTF-8 BOM and CRLF
// line endings in every file kind, the exact size limit, the 101st profile of a kind, ASCII case collisions between
// differently named files, PRIORITY ties between legacy sections and files and between kinds, and the migration of a
// BOM/CRLF file (idempotent, byte exact backup).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "profile_files.hpp"
#include "profile_migrate.hpp"
#include "testutil.hpp"

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
static const FileDiag *diagOf(const std::vector<FileDiag> &d, const std::string &path) {
    for (const FileDiag &x : d)
        if (x.path == path) return &x;
    return nullptr;
}
static const std::string kBom = "\xEF\xBB\xBF";

static void testEmptyAndCommentOnly(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/empty.conf", "");
    put(d + "/apps/comment.conf", "# nothing here\n\n   \n");
    put(d + "/devices/blank.conf", "\n\n");
    put(d + "/apps/ok.conf", "[app:ok]\nENABLED = 0\n");
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    // the defective ones drop only themselves and are reported as fatal (the UI must not overwrite them)
    for (const char *f : {"/apps/empty.conf", "/apps/comment.conf", "/devices/blank.conf"}) {
        const FileDiag *x = diagOf(diag, d + f);
        assert(x && x->fatal && x->section == ProfileSet::npos && !x->warnings.empty());
    }
    assert(ps.sections.size() == 2 && ps.find(Section::APP, "ok") == 1);
    // an empty dofscroll.conf is a valid, empty [global]
    put(d + "/dofscroll.conf", "");
    ProfileSet ps2;
    assert(loadProfileTree(d, &ps2, &diag) && ps2.sections.size() == 2);
    // so is a missing configuration directory
    ProfileSet ps3;
    assert(loadProfileTree(d + "/does-not-exist", &ps3, &diag) && ps3.sections.size() == 1);
}

static void testBomAndCrlf(const std::string &d) {
    put(d + "/dofscroll.conf", kBom + "[global]\r\nSCROLL_V_SENS = 7\r\n");
    put(d + "/devices/pro.conf", kBom + "[device:Pro]\r\nZOOM_SENS = 4\r\n");
    put(d + "/apps/code.conf", "[app:Code]\r\nZOOM_SENS = 2\r\nBUTTON_1 = key:space\r\nPRIORITY = 3\r\n");
    put(d + "/apps/lf.conf", "[app:Lf]\nZOOM_SENS = 6\n");
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    for (const FileDiag &x : diag) assert(!x.fatal && x.warnings.empty());
    assert(ps.sections.size() == 4);
    const Settings s = ps.resolve("SpaceMouse Pro", "code");
    assert(s.scroll_v.sensitivity == 7 && s.zoom.sensitivity == 2 && s.buttons.at(1) == "key:space");
    assert(ps.resolve("SpaceMouse Pro", "lf").zoom.sensitivity == 6);
    assert(ps.resolve("SpaceMouse Pro", "zzz").zoom.sensitivity == 4);
    // the name has no trailing CR
    assert(ps.find(Section::APP, "Code") != ProfileSet::npos && ps.find(Section::DEVICE, "Pro") != ProfileSet::npos);
    // a rewrite through the writer is LF only and BOM free
    const size_t i = ps.find(Section::APP, "Code");
    const WriteResult w = saveProfileFile(d, ps.sections[i], diagOf(diag, d + "/apps/code.conf"));
    assert(w.ok());
    const std::string text = get(w.path);
    assert(text.find('\r') == std::string::npos && text.compare(0, 3, kBom) != 0);
}

static void testExactSizeLimit(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\n");
    const std::string head = "[app:Edge]\nENABLED = 0\n# ";
    std::string atLimit = head;
    atLimit.append(kMaxProfileFileBytes - atLimit.size() - 1, 'x');
    atLimit += "\n";
    assert(atLimit.size() == kMaxProfileFileBytes);
    put(d + "/apps/at.conf", atLimit);
    put(d + "/apps/over.conf", atLimit.substr(0, atLimit.size() - 1) + "xx\n");  // one byte too many
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    const FileDiag *at = diagOf(diag, d + "/apps/at.conf"), *over = diagOf(diag, d + "/apps/over.conf");
    assert(at && !at->fatal && at->section != ProfileSet::npos);
    assert(over && over->fatal && over->section == ProfileSet::npos);
    assert(ps.find(Section::APP, "Edge") != ProfileSet::npos);
}

static void testManyProfilesPerKind(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\n");
    for (int i = 0; i < 101; i++) {
        char n[16];
        std::snprintf(n, sizeof n, "d%03d", i);
        put(d + "/devices/" + n + ".conf", std::string("[device:") + n + "]\nZOOM_SENS = " + std::to_string(1 + i % 9) + "\n");
        put(d + "/apps/" + n + ".conf", std::string("[app:") + n + "]\nPRIORITY = " + std::to_string(i) + "\n");
    }
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    for (const FileDiag &x : diag) assert(!x.fatal);
    assert(ps.sections.size() == 1 + 202);
    // devices before apps, inside a kind in file name order, the 101st (d100) last
    assert(ps.sections[1].kind == Section::DEVICE && ps.sections[1].pattern == "d000");
    assert(ps.sections[101].pattern == "d100" && ps.sections[102].kind == Section::APP && ps.sections[202].pattern == "d100");
    // migration priorities with more than 100 profiles stay in range and keep the order
    ProfileSet legacy;
    for (int i = 0; i < 150; i++) legacy.findOrAdd(Section::APP, "a" + std::to_string(i));
    assignMigrationPriorities(&legacy);
    int prev = -100000;
    for (size_t i = 1; i < legacy.sections.size(); i++) {
        int p = -1;
        for (const KV &kv : legacy.sections[i].kv)
            if (kv.first == "PRIORITY") p = std::stoi(kv.second);
        assert(p > prev && p >= kMinPriority && p <= kMaxPriority);
        prev = p;
    }
}

static void testCaseCollisionsBetweenFiles(const std::string &d) {
    put(d + "/dofscroll.conf", "[global]\n");
    // different file names, names equal up to ASCII case: the first file in byte order wins, the other is fatal
    put(d + "/apps/1-first.conf", "[app:Firefox]\nZOOM_SENS = 1\n");
    put(d + "/apps/2-second.conf", "[app:FIREFOX]\nZOOM_SENS = 9\n");
    // the same name in devices/ and apps/ is not a collision (kinds are separate)
    put(d + "/devices/x.conf", "[device:firefox]\nZOOM_SENS = 5\n");
    // non-ASCII case is not folded: two different profiles
    put(d + "/apps/3-umlaut-a.conf", "[app:\xC3\x84x]\n");
    put(d + "/apps/4-umlaut-b.conf", "[app:\xC3\xA4x]\n");
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    assert(!diagOf(diag, d + "/apps/1-first.conf")->fatal && diagOf(diag, d + "/apps/2-second.conf")->fatal);
    assert(!diagOf(diag, d + "/devices/x.conf")->fatal);
    assert(!diagOf(diag, d + "/apps/3-umlaut-a.conf")->fatal && !diagOf(diag, d + "/apps/4-umlaut-b.conf")->fatal);
    assert(ps.resolve("", "firefox").zoom.sensitivity == 1);
    // the writer never targets the loser's file (fatal) and a new profile of that name is a Conflict or lands elsewhere
    Section sec;
    sec.kind = Section::APP;
    sec.pattern = "firefox";
    const WriteResult w = saveProfileFile(d, sec, nullptr);
    assert(!w.ok() ? w.status == WriteStatus::Conflict : stdfs::exists(w.path));
    assert(get(d + "/apps/2-second.conf").find("ZOOM_SENS = 9") != std::string::npos);
}

static void testPriorityTies(const std::string &d) {
    // legacy sections of dofscroll.conf come before files of the same PRIORITY; a file with a smaller one comes first
    put(d + "/dofscroll.conf", "[global]\n[app:legacy1]\n[app:legacy2]\nPRIORITY = 7\n[device:ld]\n");
    put(d + "/apps/a.conf", "[app:fa]\nPRIORITY = 7\n");
    put(d + "/apps/b.conf", "[app:fb]\nPRIORITY = -1\n");
    put(d + "/apps/c.conf", "[app:fc]\nPRIORITY = 1000\n");
    put(d + "/apps/d.conf", "[app:fd]\nPRIORITY = -1000\n");
    put(d + "/devices/z.conf", "[device:fz]\nPRIORITY = -5\n");
    ProfileSet ps;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &ps, &diag));
    std::vector<std::string> order;
    for (size_t i = 1; i < ps.sections.size(); i++) order.push_back(ps.sections[i].pattern);
    const std::vector<std::string> want = {"fz", "ld", "fd", "fb", "legacy1", "legacy2", "fa", "fc"};
    // devices first (priority -5 before the legacy device with the default 0), then the apps
    // by PRIORITY: fd -1000, fb -1, legacy1 0, legacy2 7, fa 7, fc 1000 (tie 7: legacy before the file)
    assert(order == want);
    // out of range: the key is dropped with a warning, the profile stays at the default priority
    put(d + "/apps/e.conf", "[app:fe]\nPRIORITY = 1001\n");
    put(d + "/apps/f.conf", "[app:ff]\nPRIORITY = abc\n");
    assert(loadProfileTree(d, &ps, &diag));
    assert(!diagOf(diag, d + "/apps/e.conf")->fatal && !diagOf(diag, d + "/apps/e.conf")->warnings.empty());
    assert(!diagOf(diag, d + "/apps/f.conf")->fatal && !diagOf(diag, d + "/apps/f.conf")->warnings.empty());
    assert(ps.find(Section::APP, "fe") != ProfileSet::npos && ps.find(Section::APP, "ff") != ProfileSet::npos);
    // PRIORITY in [global] has no effect and no warning that makes the file fatal
    put(d + "/dofscroll.conf", "[global]\nPRIORITY = 9\n");
    assert(loadProfileTree(d, &ps, &diag) && !diagOf(diag, d + "/dofscroll.conf")->fatal);
}

static void testMigrateBomCrlf(const std::string &d) {
    const std::string legacy = kBom + "[global]\r\nSCROLL_V_SENS = 7\r\n[device:Pro]\r\nZOOM_SENS = 4\r\n[app:b]\r\nZOOM_SENS = 2\r\n[app:a]\r\nZOOM_SENS = 3\r\n";
    put(d + "/dofscroll.conf", legacy);
    ProfileSet before;
    assert(loadLegacyProfileFile(d + "/dofscroll.conf", &before, nullptr));
    const Settings sBefore = before.resolve("SpaceMouse Pro", "ab");
    const MigrateResult r = migrateToSplit(d);
    assert(r.status == MigrateStatus::Migrated && r.profilesMoved == 3);
    assert(get(r.backupPath) == legacy);  // byte for byte, BOM and CRLF included
    ProfileSet after;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(d, &after, &diag));
    for (const FileDiag &x : diag) assert(!x.fatal);
    const Settings sAfter = after.resolve("SpaceMouse Pro", "ab");
    assert(sAfter.zoom.sensitivity == sBefore.zoom.sensitivity && sAfter.scroll_v.sensitivity == 7);
    // idempotent: a second call changes nothing and makes no second backup
    const std::string g = get(d + "/dofscroll.conf"), fa = get(d + "/apps/a.conf");
    const MigrateResult r2 = migrateToSplit(d);
    assert(r2.status == MigrateStatus::NothingToDo && r2.backupPath.empty() && r2.profilesMoved == 0);
    assert(get(d + "/dofscroll.conf") == g && get(d + "/apps/a.conf") == fa);
    assert(!stdfs::exists(d + "/backup/dofscroll.conf.pre-split.1"));
}

static void testMigrateTrivialInputs(const std::string &d) {
    // no file, empty file, only [global] (also with BOM): nothing to do, no backup, no directories
    assert(migrateToSplit(d).status == MigrateStatus::NothingToDo);
    put(d + "/dofscroll.conf", "");
    assert(migrateToSplit(d).status == MigrateStatus::NothingToDo);
    put(d + "/dofscroll.conf", kBom + "[global]\r\nTICK_HZ = 100\r\n");
    const MigrateResult r = migrateToSplit(d);
    assert(r.status == MigrateStatus::NothingToDo && r.backupPath.empty());
    assert(!stdfs::exists(d + "/backup") && !stdfs::exists(d + "/apps") && !stdfs::exists(d + "/devices"));
    assert(get(d + "/dofscroll.conf") == kBom + "[global]\r\nTICK_HZ = 100\r\n");
}

int main() {
    int n = 0;
    auto fresh = [&] { return tu::makeTempDir("pf-edges" + std::to_string(n++)); };
    for (auto f : {testEmptyAndCommentOnly, testBomAndCrlf, testExactSizeLimit, testManyProfilesPerKind, testCaseCollisionsBetweenFiles,
                   testPriorityTies, testMigrateBomCrlf, testMigrateTrivialInputs}) {
        const std::string d = fresh();
        f(d);
        tu::removeAll(d);
    }
    std::puts("test_profile_loader_edges: OK");
    return 0;
}
