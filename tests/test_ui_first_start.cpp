// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// First start of the interface: seeding the example configuration, never overwriting, log text.
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "platform/fs.hpp"
#include "testutil.hpp"
#include "ui/model/first_start.hpp"

static void put(const std::string &p, const std::string &c) { std::ofstream(p, std::ios::binary) << c; }
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-first-start"), ex = root + "/ex";
    std::filesystem::create_directories(root + "/cfg");
    std::filesystem::create_directories(ex + "/default/" + spn::seedOsName());
    std::filesystem::create_directories(ex + "/modes");  // built in: never copied
    put(ex + "/default/" + spn::seedOsName() + "/dofscroll.conf", "[global]\nSENSITIVITY = 7\n");
    put(ex + "/modes/slides.conf", "NAME = Slides\n");
    const std::string cfg = tu::useConfigBase(root + "/cfg");
    tu::setEnv("DOFSCROLL_EXAMPLES", ex);
    const std::string conf = cfg + "/dofscroll/dofscroll.conf", mode = cfg + "/dofscroll/modes/mine.conf";

    // a fresh account: the example configuration appears (the modes are built in)
    spn::ui::FirstStart r = spn::ui::runFirstStart();
    assert(r.seeded && !r.migrated);
    assert(spn::fs::pathExists(conf) && !spn::fs::pathExists(cfg + "/dofscroll/modes"));
    assert(get(conf) == "[global]\nSENSITIVITY = 7\n");
    const std::string t = r.text("dofscroll-ui");
    assert(t.find("dofscroll-ui: config migration: not needed or not possible") == 0);
    assert(t.find("dofscroll-ui: seeded defaults: ") != std::string::npos && t.back() == '\n');

    // a second start changes nothing, and the user's own edits are never overwritten
    put(conf, "[global]\nSENSITIVITY = 99\n");
    r = spn::ui::runFirstStart();
    assert(!r.seeded);
    assert(get(conf) == "[global]\nSENSITIVITY = 99\n" && !spn::fs::pathExists(mode));
    assert(r.text("x").find("seeded defaults") == std::string::npos);

    // text(): no seeding line without seeding, the migration log is appended
    spn::ui::FirstStart f;
    f.migrated = true;
    f.migrateLog = "moved old.conf";
    assert(f.text("p") == "p: config migration: done - moved old.conf\n");
    f.seeded = true;
    f.seedLog = "default config";
    assert(f.text("p") == "p: config migration: done - moved old.conf\np: seeded defaults: default config\n");

    // split layout (on in the interface, the settings model writes per file): an upgrade moves the
    // profiles of the old single file into files, once, and says so
    assert(spn::ui::kSplitLayoutInUi);
    put(conf, "[global]\nSENSITIVITY = 5\n[app:firefox]\nZOOM_SENS = 3\n[app:vlc]\nZOOM_SENS = 4\n");
    r = spn::ui::runFirstStart(false);
    assert(r.split.status == spn::MigrateStatus::NothingToDo && spn::fs::pathExists(conf));
    assert(get(conf).find("[app:firefox]") != std::string::npos);  // off: untouched
    r = spn::ui::runFirstStart(true);
    assert(r.split.status == spn::MigrateStatus::Migrated && !r.seeded);
    assert(get(cfg + "/dofscroll/apps/vlc.conf").find("ZOOM_SENS = 4") != std::string::npos);
    assert(get(conf).find("[app:") == std::string::npos);
    assert(r.text("p").find("p: Your profiles are now one file each, backup in ") != std::string::npos);
    r = spn::ui::runFirstStart(true);  // second start: silent
    assert(r.split.status == spn::MigrateStatus::NothingToDo && r.text("p").find("one file each") == std::string::npos);

    std::filesystem::remove_all(root);
    return 0;
}
