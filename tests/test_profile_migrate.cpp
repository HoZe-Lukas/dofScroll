// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Migration of a single-file configuration to the split layout (src/profile_migrate.hpp): every shipped example,
// comments, unknown lines, backup naming, symlink exception, idempotence, crash and failure injection at every
// step, both layouts present, limits, read-only directory, first-start seeding and the first device file.
// Host independent: temp directories only (the symlink and read-only parts are skipped where they cannot be made).
#undef NDEBUG
#include <cassert>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include "config.hpp"
#include "platform/fs.hpp"
#include "profile_files.hpp"
#include "profile_migrate.hpp"
#include "seed.hpp"
#include "testutil.hpp"
#include "ui/model/first_device.hpp"

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
static bool has(const std::string &s, const std::string &needle) { return s.find(needle) != std::string::npos; }
static size_t countFiles(const std::string &dir) {
    std::vector<std::string> n;
    bool capped = false;
    if (!fs::listDir(dir, 0, &n, &capped)) return 0;
    size_t c = 0;
    for (const std::string &e : n)
        if (e != "." && e != "..") c++;
    return c;
}

// Probe names for the resolve comparison (every pattern, pairs, empty).
static std::vector<std::string> probes(const ProfileSet &ps, Section::Kind k) {
    std::vector<std::string> pats, out{""};
    for (const Section &s : ps.sections)
        if (s.kind == k) pats.push_back(s.pattern);
    for (const std::string &p : pats) out.push_back(p);
    for (size_t i = 0; i < pats.size() && out.size() < 40; i++)
        for (size_t j = i + 1; j < pats.size() && out.size() < 40; j++) out.push_back(pats[i] + " " + pats[j]);
    return out;
}
// The effective settings of the old single file and of the migrated directory are identical.
static void assertEquivalent(const std::string &oldBytesFile, const std::string &dir) {
    ProfileSet a, b;
    assert(loadLegacyProfileFile(oldBytesFile, &a, nullptr));
    assert(loadProfileTree(dir, &b, nullptr));
    assert(a.sections.size() == b.sections.size());
    for (const std::string &d : probes(a, Section::DEVICE))
        for (const std::string &p : probes(a, Section::APP)) assert(allKeys(a.resolve(d, p)) == allKeys(b.resolve(d, p)));
    for (size_t i = 1; i < a.sections.size(); i++) {  // same keys and values (PRIORITY aside)
        const size_t j = b.find(a.sections[i].kind, a.sections[i].pattern);
        assert(j != ProfileSet::npos);
        std::vector<KV> x = a.sections[i].kv, y = b.sections[j].kv;
        for (auto *v : {&x, &y})
            v->erase(std::remove_if(v->begin(), v->end(), [](const KV &e) { return e.first == "PRIORITY"; }), v->end());
        assert(x == y);
    }
}

static const char *kSample =
    "# my setup\n"
    "[global]\n"
    "SENSITIVITY = 4\n"
    "\n"
    "# the pro\n"
    "; second comment line\n"
    "[device:SpaceMouse Pro]\n"
    "SCROLL_V_SENS = 7\n"
    "BOGUS_KEY = 1\n"
    "\n"
    "[app:firefox]\n"
    "ZOOM_SENS = 3\n"
    "\n"
    "# not attached (blank line above)\n"
    "\n"
    "[app:fire]\n"
    "ZOOM_SENS = 9\n"
    "[unknown thing]\n"
    "X = 1\n";

int main() {
    const std::string root = tu::makeTempDir("dofscroll-migrate");
    int n = 0;
    auto fresh = [&](const std::string &conf) {
        const std::string d = root + "/c" + std::to_string(++n);
        put(d + "/dofscroll.conf", conf);
        return d;
    };

    // ---- every shipped example ----
    {
        std::vector<std::string> files;
        for (const char *dir : {"examples", "examples/default/linux", "examples/default/windows", "examples/default/macos"})
            for (const auto &e : stdfs::directory_iterator(dir))
                if (e.path().extension() == ".conf") files.push_back(e.path().generic_string());
        if (stdfs::exists("examples/split")) files.push_back("examples/split/dofscroll.conf");
        assert(files.size() >= 6);
        for (const std::string &f : files) {
            const std::string d = fresh(get(f));
            const std::string orig = get(f);
            ProfileSet ps;
            assert(loadLegacyProfileFile(f, &ps, nullptr));
            const MigrateResult r = migrateToSplit(d);
            if (ps.sections.size() <= 1) {
                assert(r.status == MigrateStatus::NothingToDo && get(d + "/dofscroll.conf") == orig);
                continue;
            }
            assert(r.status == MigrateStatus::Migrated && r.ok());
            assert(r.profilesMoved == ps.sections.size() - 1 && r.filesWritten.size() == r.profilesMoved);
            assert(has(r.message, "one file each") && has(r.message, "backup/dofscroll.conf.pre-split"));
            assert(get(d + "/backup/dofscroll.conf.pre-split") == orig);
            ProfileSet g;
            assert(loadLegacyProfileFile(d + "/dofscroll.conf", &g, nullptr) && g.sections.size() == 1);
            assertEquivalent(f, d);
            const std::string after = get(d + "/dofscroll.conf");
            assert(migrateToSplit(d).status == MigrateStatus::NothingToDo && get(d + "/dofscroll.conf") == after);  // idempotent
        }
    }

    // ---- comments move, unknown lines are reported and kept in the backup ----
    {
        const std::string d = fresh(kSample);
        const MigrateResult r = migrateToSplit(d);
        assert(r.status == MigrateStatus::Migrated);
        assert(r.profilesMoved == 3 && r.profilesAlreadyFiled == 0);
        const std::string pro = get(d + "/devices/spacemouse pro.conf");
        assert(has(pro, "# the pro\n# second comment line\n[device:SpaceMouse Pro]\n") && has(pro, "SCROLL_V_SENS = 7"));
        assert(!has(pro, "BOGUS_KEY"));
        assert(!has(get(d + "/apps/firefox.conf"), "# the pro") && !has(get(d + "/apps/fire.conf"), "not attached"));
        bool warned = false;
        for (const std::string &w : r.warnings) warned = warned || has(w, "BOGUS_KEY") || has(w, "unknown section");
        assert(warned);
        assert(has(get(d + "/backup/dofscroll.conf.pre-split"), "BOGUS_KEY = 1"));
        // the global file keeps its comment and the unknown section, no profile
        const std::string g = get(d + "/dofscroll.conf");
        assert(has(g, "# my setup") && has(g, "SENSITIVITY = 4") && has(g, "[unknown thing]") && !has(g, "[device:") && !has(g, "[app:"));
        assert(!has(g, "# the pro") && !has(g, "ZOOM_SENS"));
        // overlapping patterns: "fire" (later in the file) wins over "firefox" in the old file and still does
        ProfileSet t;
        assert(loadProfileTree(d, &t, nullptr));
        assert(allKeys(t.resolve("", "firefox")) == allKeys(t.resolve("", "fire")));  // later wins, as in the old file
        assertEquivalent(d + "/backup/dofscroll.conf.pre-split", d);
        assert(has(get(d + "/devices/spacemouse pro.conf"), "SCROLL_V_SENS") && has(get(d + "/apps/firefox.conf"), "PRIORITY = 10") &&
               has(get(d + "/apps/fire.conf"), "PRIORITY = 20"));
        assert(!has(pro, "PRIORITY"));  // a kind with one profile gets none
    }

    // ---- symlink: nothing is done ----
#ifndef _WIN32
    {
        const std::string d = root + "/link";
        put(d + "/real/dotfile.conf", kSample);
        std::error_code ec;
        stdfs::create_symlink(d + "/real/dotfile.conf", d + "/dofscroll.conf", ec);
        if (!ec) {
            const MigrateResult r = migrateToSplit(d);
            assert(r.status == MigrateStatus::SkippedSymlink && r.ok() && !r.message.empty());
            assert(get(d + "/real/dotfile.conf") == kSample && !tu::exists(d + "/devices") && !tu::exists(d + "/backup"));
        }
    }
#endif

    // ---- backup naming: identical copy reused, different one numbered ----
    {
        const std::string d = fresh(kSample);
        put(d + "/backup/dofscroll.conf.pre-split", "something else\n");
        const MigrateResult r = migrateToSplit(d);
        assert(r.status == MigrateStatus::Migrated && has(r.backupPath, "dofscroll.conf.pre-split.1"));
        assert(get(d + "/backup/dofscroll.conf.pre-split") == "something else\n" && get(r.backupPath) == kSample);
        const std::string d2 = fresh(kSample);
        put(d2 + "/backup/dofscroll.conf.pre-split", kSample);
        put(d2 + "/backup/dofscroll.conf.pre-split.1", "x");
        const MigrateResult r2 = migrateToSplit(d2);
        assert(r2.status == MigrateStatus::Migrated && has(r2.backupPath, "pre-split") && !has(r2.backupPath, "pre-split.1"));
        assert(!tu::exists(d2 + "/backup/dofscroll.conf.pre-split.2"));
    }

    // ---- crash and failure injection at every step ----
    {
        // reference: the clean result
        const std::string ref = fresh(kSample);
        assert(migrateToSplit(ref).status == MigrateStatus::Migrated);
        ProfileSet refTree;
        assert(loadProfileTree(ref, &refTree, nullptr));
        auto sameAsRef = [&](const std::string &d) {
            ProfileSet t;
            assert(loadProfileTree(d, &t, nullptr));
            assert(t.sections.size() == refTree.sections.size());
            for (size_t i = 0; i < t.sections.size(); i++)
                assert(t.sections[i].pattern == refTree.sections[i].pattern && t.sections[i].kv == refTree.sections[i].kv);
            assert(get(d + "/dofscroll.conf") == get(ref + "/dofscroll.conf"));
        };
        // count the steps
        size_t steps = 0;
        {
            MigrateOptions o;
            o.fault = [&](const std::string &) { steps++; return MigrateFault::None; };
            assert(migrateToSplit(fresh(kSample), o).status == MigrateStatus::Migrated);
        }
        assert(steps >= 7);  // backup, 3 files, readback, replace-global, final-check
        for (size_t at = 0; at < steps; at++) {
            for (MigrateFault kind : {MigrateFault::Crash, MigrateFault::Fail}) {
                const std::string d = fresh(kSample);
                size_t i = 0;
                MigrateOptions o;
                o.fault = [&](const std::string &) { return i++ == at ? kind : MigrateFault::None; };
                const MigrateResult r = migrateToSplit(d, o);
                assert(r.status == MigrateStatus::Failed && has(r.error, "simulated"));
                // the old file is never damaged: it is the original, or (after replace) the finished global part
                const std::string g = get(d + "/dofscroll.conf");
                assert(g == kSample || !has(g, "[device:"));
                if (kind == MigrateFault::Fail) {  // a failure cleans up after itself
                    assert(g == kSample);
                    assert(countFiles(d + "/devices") == 0 && countFiles(d + "/apps") == 0);
                    assert(!tu::exists(d + "/devices") && !tu::exists(d + "/apps"));
                }
                // every profile key is in the old file, in the backup or in a complete new file at every point
                if (g == kSample) {
                    ProfileSet t;
                    assert(loadProfileTree(d, &t, nullptr) && t.sections.size() == 4);
                }
                // the next run finishes the job with the same result as an uninterrupted one
                const MigrateResult r2 = migrateToSplit(d);
                assert(r2.ok());
                sameAsRef(d);
                assert(get(d + "/backup/dofscroll.conf.pre-split") == kSample && !tu::exists(d + "/backup/dofscroll.conf.pre-split.1"));
                assert(migrateToSplit(d).status == MigrateStatus::NothingToDo);
            }
        }
    }

    // ---- both layouts present: files win, only the leftovers move ----
    {
        const std::string d = fresh("[global]\nSENSITIVITY = 2\n[app:firefox]\nZOOM_SENS = 1\n[app:vlc]\nZOOM_SENS = 2\n");
        put(d + "/apps/firefox.conf", "[app:firefox]\nZOOM_SENS = 8\n");
        const MigrateResult r = migrateToSplit(d);
        assert(r.status == MigrateStatus::Migrated && r.profilesMoved == 1 && r.profilesAlreadyFiled == 1);
        assert(has(get(d + "/apps/firefox.conf"), "ZOOM_SENS = 8") && has(get(d + "/apps/vlc.conf"), "ZOOM_SENS = 2"));
        assert(!has(get(d + "/dofscroll.conf"), "[app:"));
        ProfileSet t;
        assert(loadProfileTree(d, &t, nullptr) && t.sections.size() == 3);
        assert(t.resolve("", "firefox").zoom.sensitivity == 8);
        // only files, no section left in dofscroll.conf: nothing to do
        assert(migrateToSplit(d).status == MigrateStatus::NothingToDo);
    }

    // ---- limits and bad inputs ----
    {
        const std::string d = fresh(std::string("[app:a]\nZOOM_SENS = 1\n") + std::string(kMaxConfigBytes + 10, '#'));
        const MigrateResult r = migrateToSplit(d);
        assert(r.status == MigrateStatus::Failed && has(r.error, "too large") && !tu::exists(d + "/apps") && !tu::exists(d + "/backup"));
        const std::string d2 = root + "/missing";
        assert(migrateToSplit(d2).status == MigrateStatus::NothingToDo && !tu::exists(d2));
        const std::string d3 = fresh("[global]\nSENSITIVITY = 2\n");
        assert(migrateToSplit(d3).status == MigrateStatus::NothingToDo && get(d3 + "/dofscroll.conf") == "[global]\nSENSITIVITY = 2\n");
        // a file where the devices directory should be: the write fails, the old file stays, no litter
        const std::string d4 = fresh("[device:A]\nSCROLL_V_SENS = 3\n[app:b]\nZOOM_SENS = 2\n");
        put(d4 + "/devices", "in the way");
        const MigrateResult r4 = migrateToSplit(d4);
        assert(r4.status == MigrateStatus::Failed);
        assert(get(d4 + "/dofscroll.conf") == "[device:A]\nSCROLL_V_SENS = 3\n[app:b]\nZOOM_SENS = 2\n");
        assert(!tu::exists(d4 + "/apps") || countFiles(d4 + "/apps") == 0);
    }

    // ---- read-only directory: reported, nothing changes ----
#ifndef _WIN32
    {
        const std::string d = fresh(kSample);
        stdfs::permissions(d, stdfs::perms::owner_read | stdfs::perms::owner_exec, stdfs::perm_options::replace);
        std::ofstream probe(d + "/probe");
        const bool canWrite = probe.good();
        probe.close();
        if (!canWrite) {
            const MigrateResult r = migrateToSplit(d);
            assert(r.status == MigrateStatus::Failed && !r.error.empty());
            assert(get(d + "/dofscroll.conf") == kSample && !tu::exists(d + "/devices"));
        }
        stdfs::permissions(d, stdfs::perms::owner_all, stdfs::perm_options::replace);
    }
#endif

    // ---- seeding writes the new layout, counts devices/ and apps/ as configured ----
    {
        const std::string sroot = tu::makeTempDir("dofscroll-migrate-seed"), ex = sroot + "/ex";
        put(ex + std::string("/default/") + seedOsName() + "/dofscroll.conf",
            "# example\n[global]\nSENSITIVITY = 6\n[app:demo]\nZOOM_SENS = 2\n[app:demo2]\nZOOM_SENS = 3\n");
        std::filesystem::create_directories(sroot + "/cfg");
        const std::string cfg = tu::useConfigBase(sroot + "/cfg");
        tu::setEnv("DOFSCROLL_EXAMPLES", ex);
        const std::string dir = cfg + "/dofscroll";
        // an existing split configuration without dofscroll.conf is not seeded over
        put(dir + "/apps/mine.conf", "[app:mine]\nZOOM_SENS = 5\n");
        std::string log;
        assert(!seedDefaults(&log) && !tu::exists(dir + "/dofscroll.conf"));
        stdfs::remove(dir + "/apps/mine.conf");
        assert(seedDefaults(&log) && has(log, "profiles written as files"));
        assert(has(get(dir + "/dofscroll.conf"), "SENSITIVITY = 6") && !has(get(dir + "/dofscroll.conf"), "[app:"));
        assert(has(get(dir + "/apps/demo.conf"), "PRIORITY = 10") && has(get(dir + "/apps/demo2.conf"), "PRIORITY = 20"));
        assert(!tu::exists(dir + "/backup"));  // the source is the shipped example
        assert(!seedDefaults(&log));
        // splitLayout = false keeps the example as one file
        tu::removeAll(dir);
        assert(seedDefaults(&log, false) && has(get(dir + "/dofscroll.conf"), "[app:demo]") && !tu::exists(dir + "/apps"));

        // first device profile as devices/<slug>.conf, once
        tu::removeAll(dir);
        put(dir + "/dofscroll.conf", "[global]\nSENSITIVITY = 1\n");
        std::string err;
        assert(!ui::writeFirstDeviceProfileFile(dir, "", &err));
        assert(ui::writeFirstDeviceProfileFile(dir, "SpaceMouse Pro", &err));
        assert(has(get(dir + "/devices/spacemouse pro.conf"), "[device:SpaceMouse Pro]") &&
               has(get(dir + "/devices/spacemouse pro.conf"), "BUTTON_1 = nav"));
        assert(!ui::writeFirstDeviceProfileFile(dir, "Other Device", &err));
        assert(countFiles(dir + "/devices") == 1);
        tu::removeAll(sroot);
    }

    tu::removeAll(root);
    std::puts("test_profile_migrate OK");
    return 0;
}
