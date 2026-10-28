// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The settings model on the split layout (docs/plan-config-split.md, stage 4): per-file load and save, only
// changed files written, conflicts, defective files, PRIORITY order, delete, rename, import and export, the
// single-file mode of a symlinked dofscroll.conf, the migration at the first start. Host independent: temp
// directories only.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "platform/fs.hpp"
#include "profile_files.hpp"
#include "testutil.hpp"
#include "ui/model/display_text.hpp"
#include "ui/model/first_device.hpp"
#include "ui/model/first_start.hpp"
#include "ui/model/profile_export.hpp"
#include "ui/model/profile_import.hpp"
#include "ui/model/profile_tree_model.hpp"
#include "ui/model/profile_undo.hpp"

using namespace spn;
using namespace spn::ui;

static IdentityStrings id;

static void put(const std::string &p, const std::string &c) {
    std::string err;
    assert(fs::writeFileReplace(p, c, &err));
}
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static bool has(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }
static std::string stampOf(const std::string &p) { return profileFileStamp(p, false); }

// A fresh configuration directory with a global file and the given profile files.
static std::string makeDir(const char *tag) {
    const std::string d = tu::makeTempDir(tag);
    std::filesystem::create_directories(d + "/devices");
    std::filesystem::create_directories(d + "/apps");
    return d;
}

static SettingsModel load(const std::string &dir, std::vector<std::string> *warn = nullptr) {
    SettingsModel m;
    std::string err;
    assert(m.load(dir + "/dofscroll.conf", warn, &err));
    return m;
}

// Order of the names of a kind in the model.
static std::string order(const SettingsModel &m, Section::Kind k) {
    std::string s;
    for (const SettingsModel::SectionInfo &i : m.sections())
        if (i.index != 0 && i.kind == k) s += (s.empty() ? "" : ",") + i.pattern;
    return s;
}

static void testLoadSaveOnlyChanged() {
    const std::string d = makeDir("split-model-save");
    put(d + "/dofscroll.conf", "[global]\nSCROLL_V_SENS = 7\n");
    put(d + "/devices/spacemouse pro.conf", "[device:SpaceMouse Pro]\nBUTTON_1 = nav\n");
    put(d + "/apps/firefox.conf", "[app:firefox]\nZOOM_SENS = 3\n");
    put(d + "/apps/vlc.conf", "[app:vlc]\nZOOM_SENS = 4\n");
    SettingsModel m = load(d);
    assert(m.splitLayout() && m.sectionCount() == 4 && !m.dirty());
    const size_t ff = m.profiles().find(Section::APP, "firefox");
    assert(!m.profileFile(ff).empty() && !m.inSingleFile(ff));
    const std::string vlcBefore = stampOf(d + "/apps/vlc.conf"), pro = stampOf(d + "/devices/spacemouse pro.conf"),
                      glob = profileFileStamp(d + "/dofscroll.conf", true);
    std::string err;
    int reloads = 0;
    m.setReloadCallback([&](std::string *) {
        ++reloads;
        return true;
    });
    assert(m.setValue(ff, "ZOOM_SENS", "5", &err) && m.dirty() && m.profileDirty(ff));
    const SettingsModel::SaveResult r = m.save(id);
    assert(r.saved && r.reloaded && !r.conflict && reloads == 1 && !m.dirty());
    // only the edited file changed
    assert(has(get(d + "/apps/firefox.conf"), "ZOOM_SENS = 5"));
    assert(stampOf(d + "/apps/vlc.conf") == vlcBefore && stampOf(d + "/devices/spacemouse pro.conf") == pro);
    assert(profileFileStamp(d + "/dofscroll.conf", true) == glob);
    // a global edit writes only dofscroll.conf
    const std::string ffStamp = stampOf(d + "/apps/firefox.conf");
    assert(m.setValue(0, "SCROLL_V_SENS", "8", &err) && m.save(id).saved);
    assert(has(get(d + "/dofscroll.conf"), "SCROLL_V_SENS = 8"));
    assert(stampOf(d + "/apps/firefox.conf") == ffStamp && stampOf(d + "/apps/vlc.conf") == vlcBefore);
    // a second edit of the same profile right after the first: the stamp of our own write is known
    assert(m.setValue(ff, "ZOOM_SENS", "6", &err) && m.save(id).saved);
    assert(has(get(d + "/apps/firefox.conf"), "ZOOM_SENS = 6"));
    // a new profile gets its own file, nothing else is touched
    const size_t nu = m.addSection(Section::APP, "gimp", &err);
    assert(nu != ProfileSet::npos && m.setValue(nu, "ZOOM_SENS", "2", &err) && m.save(id).saved);
    assert(has(get(d + "/apps/gimp.conf"), "[app:gimp]") && has(get(d + "/apps/gimp.conf"), "ZOOM_SENS = 2"));
    assert(stampOf(d + "/apps/vlc.conf") == vlcBefore);
    SettingsModel back = load(d);
    assert(back.sectionCount() == 5 && back.profiles().find(Section::APP, "gimp") != ProfileSet::npos);
    tu::removeAll(d);
}

static void testConflict() {
    const std::string d = makeDir("split-model-conflict");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/firefox.conf", "[app:firefox]\nZOOM_SENS = 3\n");
    put(d + "/apps/vlc.conf", "[app:vlc]\nZOOM_SENS = 4\n");
    SettingsModel m = load(d);
    const size_t ff = m.profiles().find(Section::APP, "firefox");
    const size_t vlc = m.profiles().find(Section::APP, "vlc");
    std::string err;
    // somebody edits firefox.conf by hand after the model read it
    put(d + "/apps/firefox.conf", "[app:firefox]\nZOOM_SENS = 9\n# by hand\n");
    assert(m.setValue(ff, "ZOOM_SENS", "5", &err) && m.setValue(vlc, "ZOOM_SENS", "6", &err));
    const SettingsModel::SaveResult r = m.save(id);
    assert(!r.saved && r.conflict && has(r.error, "firefox") && has(r.error, "changed outside"));
    assert(has(get(d + "/apps/firefox.conf"), "by hand") && has(get(d + "/apps/firefox.conf"), "ZOOM_SENS = 9"));  // never overwritten
    assert(has(get(d + "/apps/vlc.conf"), "ZOOM_SENS = 6"));  // the other profile was written
    // the model shows what is on disk now, with the message next to the profile
    const size_t ff2 = m.profiles().find(Section::APP, "firefox");
    assert(ff2 != ProfileSet::npos && m.effective(ff2).zoom.sensitivity == 9 && !m.dirty());
    assert(has(m.profileProblem(ff2), "changed outside"));
    // the next successful save of that profile clears the message
    assert(m.setValue(ff2, "ZOOM_SENS", "5", &err) && m.save(id).saved && m.profileProblem(ff2).empty());
    // a hand edit of the global file
    put(d + "/dofscroll.conf", "[global]\nSCROLL_V_SENS = 2\n");
    assert(m.setValue(0, "SCROLL_V_SENS", "4", &err));
    put(d + "/dofscroll.conf", "[global]\nSCROLL_V_SENS = 3\n# hand\n");
    const SettingsModel::SaveResult g = m.save(id);
    assert(!g.saved && g.conflict && has(g.error, "dofscroll.conf") && has(get(d + "/dofscroll.conf"), "# hand"));
    assert(m.effective(0).scroll_v.sensitivity == 3);
    tu::removeAll(d);
}

static void testDefectiveAndLegacy() {
    const std::string d = makeDir("split-model-defective");
    put(d + "/dofscroll.conf", "[global]\n[app:firefox]\nZOOM_SENS = 3\n[app:vlc]\nZOOM_SENS = 4\n");
    put(d + "/apps/firefox.conf", "[app:firefox]\nZOOM_SENS = 3\nthis line is broken [[\n[app:second]\n");  // two sections
    put(d + "/apps/vlc.conf", "[device:vlc]\nZOOM_SENS = 4\n");                                           // wrong kind
    put(d + "/apps/broken.conf", "[global]\nSCROLL_V_SENS = 3\n");                                       // global keys
    put(d + "/apps/copy.conf", "[app:other]\nZOOM_SENS = 1\n");
    put(d + "/apps/copy2.conf", "[app:Other]\nZOOM_SENS = 2\n");  // the same header name as copy.conf
    SettingsModel m = load(d);
    // the sections of the old file stay active because their files are defective
    const size_t ff = m.profiles().find(Section::APP, "firefox");
    assert(ff != ProfileSet::npos && m.inSingleFile(ff));
    assert(has(m.legacyHint(ff, id), "still read from dofscroll.conf") && has(m.legacyHint(ff, id), "firefox.conf"));
    const std::vector<SettingsModel::Defect> def = m.defects();
    assert(def.size() >= 4);
    bool dup = false, legacyActive = false, wrongKind = false;
    for (const SettingsModel::Defect &x : def) {
        dup = dup || (x.duplicate && x.file.find("copy2.conf") != std::string::npos);
        legacyActive = legacyActive || (x.legacyActive && x.file.find("firefox.conf") != std::string::npos);
        wrongKind = wrongKind || (x.file.find("vlc.conf") != std::string::npos && !x.message.empty());
    }
    assert(dup && legacyActive && wrongKind);
    // they appear in the tree as entries of their own, with the file name
    int defective = 0;
    for (const ProfileEntry &e : ProfileTreeModel::entries(m))
        if (e.defective) {
            ++defective;
            assert(!e.file.empty() && !e.message.empty() && e.section == ProfileSet::npos);
        }
    assert(defective == (int)def.size());
    // editing the profile keeps it in dofscroll.conf and never touches the defective file
    const std::string before = get(d + "/apps/firefox.conf"), stamp = stampOf(d + "/apps/firefox.conf");
    std::string err;
    assert(m.setValue(ff, "ZOOM_SENS", "8", &err) && m.save(id).saved);
    assert(get(d + "/apps/firefox.conf") == before && stampOf(d + "/apps/firefox.conf") == stamp);
    assert(has(get(d + "/dofscroll.conf"), "[app:firefox]") && has(get(d + "/dofscroll.conf"), "ZOOM_SENS = 8"));
    // a new profile with the name of a defective file is refused, the file stays
    assert(m.addSection(Section::APP, "second", &err) != ProfileSet::npos);  // "second" has no file of its own
    SettingsModel m2 = load(d);
    put(d + "/apps/mine.conf", "[app:mine]\nthis is [not valid\n");
    put(d + "/apps/mine2.conf", "nothing here\n");
    m2 = load(d);
    // a name taken only by a broken file (header usable, no section kept): not the case for "mine" (it parses)
    assert(m2.profiles().find(Section::APP, "mine") != ProfileSet::npos);
    // a legacy profile edited while its file is fine moves into its own file
    put(d + "/dofscroll.conf", "[global]\n[app:solo]\nZOOM_SENS = 3\n[app:duo]\nZOOM_SENS = 4\n");
    tu::removeAll(d + "/apps");
    std::filesystem::create_directories(d + "/apps");
    SettingsModel m3 = load(d);
    const size_t solo = m3.profiles().find(Section::APP, "solo"), duo = m3.profiles().find(Section::APP, "duo");
    assert(m3.inSingleFile(solo) && m3.inSingleFile(duo));
    assert(m3.setValue(solo, "ZOOM_SENS", "9", &err) && m3.save(id).saved);
    assert(has(get(d + "/apps/solo.conf"), "ZOOM_SENS = 9") && !tu::exists(d + "/apps/duo.conf"));
    assert(!has(get(d + "/dofscroll.conf"), "solo") && has(get(d + "/dofscroll.conf"), "[app:duo]"));  // duo stays where it is
    SettingsModel m4 = load(d);
    assert(m4.sectionCount() == 3 && m4.effective(m4.profiles().find(Section::APP, "solo")).zoom.sensitivity == 9);
    tu::removeAll(d);
}

static void testDefectiveNameBlocks() {
    const std::string d = makeDir("split-model-block");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/kind.conf", "[device:kinddev]\nZOOM_SENS = 3\n");  // wrong kind for apps/: defective, name "kinddev"
    put(d + "/devices/wrongdir.conf", "[app:wrongapp]\n");
    SettingsModel m = load(d);
    std::string err;
    // the name of a defective file is not given to a new or renamed profile of its kind; the file stays
    assert(m.addSection(Section::APP, "KindDev", &err) == ProfileSet::npos && has(err, "kind.conf") && has(err, "defective"));
    assert(m.addSection(Section::DEVICE, "kinddev", &err) != ProfileSet::npos);  // the same name as a device is free
    assert(m.addSection(Section::DEVICE, "wrongapp", &err) == ProfileSet::npos && has(err, "wrongdir.conf"));
    const size_t other = m.addSection(Section::APP, "other", &err);
    assert(other != ProfileSet::npos && !m.renameSection(other, "kinddev", &err) && has(err, "defective"));
    assert(m.save(id).saved && has(get(d + "/apps/kind.conf"), "[device:kinddev]") && tu::exists(d + "/apps/other.conf"));
    // fixing the file makes the name usable again
    put(d + "/apps/kind.conf", "[app:kinddev]\nZOOM_SENS = 3\n");
    m = load(d);
    assert(m.profiles().find(Section::APP, "kinddev") != ProfileSet::npos && m.defects().size() == 1);  // wrongdir.conf
    tu::removeAll(d);
}

static void testPriorityMoves() {
    const std::string d = makeDir("split-model-order");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/a.conf", "[app:a]\nPRIORITY = 10\n");
    put(d + "/apps/b.conf", "[app:b]\nPRIORITY = 20\n");
    put(d + "/apps/c.conf", "[app:c]\nPRIORITY = 30\n");
    SettingsModel m = load(d);
    std::string err;
    assert(order(m, Section::APP) == "a,b,c");
    size_t idx = 0;
    // move c up: the two swap their numbers, the gap stays, a is untouched
    const std::string aStamp = stampOf(d + "/apps/a.conf");
    assert(m.canMove(m.profiles().find(Section::APP, "c"), true) && !m.canMove(m.profiles().find(Section::APP, "c"), false));
    assert(m.moveProfile(m.profiles().find(Section::APP, "c"), true, &idx, &err));
    assert(order(m, Section::APP) == "a,c,b" && m.profiles().sections[idx].pattern == "c");
    assert(m.priority(m.profiles().find(Section::APP, "c")) == 20 && m.priority(m.profiles().find(Section::APP, "b")) == 30);
    assert(m.save(id).saved && stampOf(d + "/apps/a.conf") == aStamp);
    assert(order(load(d), Section::APP) == "a,c,b");
    // move a down twice
    assert(m.moveProfile(m.profiles().find(Section::APP, "a"), false, &idx, &err));
    assert(m.moveProfile(idx, false, &idx, &err));
    assert(order(m, Section::APP) == "c,b,a" && !m.canMove(idx, false));
    assert(!m.moveProfile(idx, false, &idx, &err) && !err.empty());
    assert(m.save(id).saved && order(load(d), Section::APP) == "c,b,a");
    // the resolved settings follow the order: later wins
    assert(has(m.orderText(m.profiles().find(Section::APP, "b"), id), "Applied after \"c\""));
    assert(has(m.orderText(m.profiles().find(Section::APP, "b"), id), "\"a\" is applied after it"));
    assert(has(m.orderText(m.profiles().find(Section::APP, "c"), id), "Applied first"));
    assert(has(m.orderText(m.profiles().find(Section::APP, "a"), id), "Nothing is applied after it"));

    // ties and hand-written numbers: equal values are numbered again, the order stays what it was shown as
    put(d + "/apps/a.conf", "[app:a]\n");
    put(d + "/apps/b.conf", "[app:b]\n");
    put(d + "/apps/c.conf", "[app:c]\nPRIORITY = 500\n");
    m = load(d);
    assert(order(m, Section::APP) == "a,b,c");
    assert(has(m.orderText(m.profiles().find(Section::APP, "a"), id), "same priority"));
    assert(m.moveProfile(m.profiles().find(Section::APP, "b"), true, &idx, &err));  // b before a, both 0
    assert(order(m, Section::APP) == "b,a,c");
    assert(m.priority(m.profiles().find(Section::APP, "b")) < m.priority(m.profiles().find(Section::APP, "a")) &&
           m.priority(m.profiles().find(Section::APP, "a")) < m.priority(m.profiles().find(Section::APP, "c")));
    assert(m.save(id).saved && order(load(d), Section::APP) == "b,a,c");
    // a profile with a large hand-written number moves down by swapping numbers when they differ
    assert(m.moveProfile(m.profiles().find(Section::APP, "a"), false, &idx, &err));
    assert(order(m, Section::APP) == "b,c,a" && m.save(id).saved && order(load(d), Section::APP) == "b,c,a");
    // device profiles have their own order, apps are not involved
    assert(m.addSection(Section::DEVICE, "d1", &err) != ProfileSet::npos && m.addSection(Section::DEVICE, "d2", &err) != ProfileSet::npos);
    assert(m.save(id).saved);
    m = load(d);
    assert(order(m, Section::DEVICE) == "d1,d2");
    assert(m.moveProfile(m.profiles().find(Section::DEVICE, "d2"), true, &idx, &err));
    assert(order(m, Section::DEVICE) == "d2,d1" && order(m, Section::APP) == "b,c,a");
    assert(m.save(id).saved && order(load(d), Section::DEVICE) == "d2,d1");
    // the tree lists them in applied order
    std::string tree;
    for (const ProfileEntry &e : ProfileTreeModel::entries(m))
        if (e.kind == Section::APP) tree += e.pattern;
    assert(tree == "bca");
    // global is no profile of a kind
    assert(!m.canMove(0, true) && !m.moveProfile(0, true, &idx, &err) && m.orderText(0, id).empty());
    // a new profile is applied last when the others have an explicit order
    const size_t nu = m.addSection(Section::APP, "z-new", &err);
    assert(order(m, Section::APP) == "b,c,a,z-new" && m.priority(nu) > m.priority(m.profiles().find(Section::APP, "c")));
    // many profiles: renumbering stays inside the PRIORITY range
    SettingsModel big;
    big.setProfiles(ProfileSet(), d + "/dofscroll.conf");
    for (int i = 0; i < 120; ++i) big.addSection(Section::APP, "p" + std::to_string(100 + i), &err);
    assert(big.moveProfile(big.profiles().find(Section::APP, "p150"), true, &idx, &err));
    for (const SettingsModel::SectionInfo &s : big.sections()) assert(big.priority(s.index) >= -1000 && big.priority(s.index) <= 1000);
    tu::removeAll(d);
}

static void testDeleteAndRename() {
    const std::string d = makeDir("split-model-delete");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/firefox.conf", "[app:firefox]\nZOOM_SENS = 3\n");
    put(d + "/apps/vlc.conf", "[app:vlc]\nZOOM_SENS = 4\n");
    put(d + "/apps/gimp.conf", "[app:gimp]\nZOOM_SENS = 5\n");
    SettingsModel m = load(d);
    std::string err;
    // delete: the file goes with the next save, no other file is touched
    const std::string vlcStamp = stampOf(d + "/apps/vlc.conf");
    assert(m.removeSection(m.profiles().find(Section::APP, "firefox"), &err) && m.dirty());
    assert(tu::exists(d + "/apps/firefox.conf"));
    assert(m.save(id).saved && !tu::exists(d + "/apps/firefox.conf") && !m.dirty() && stampOf(d + "/apps/vlc.conf") == vlcStamp);
    // delete of a file that was changed by hand: kept, reported, the profile comes back
    assert(m.removeSection(m.profiles().find(Section::APP, "vlc"), &err));
    put(d + "/apps/vlc.conf", "[app:vlc]\nZOOM_SENS = 4\n# edited\n");
    SettingsModel::SaveResult r = m.save(id);
    assert(!r.saved && r.conflict && has(r.error, "vlc") && has(r.error, "not removed") && tu::exists(d + "/apps/vlc.conf"));
    assert(m.profiles().find(Section::APP, "vlc") != ProfileSet::npos && has(m.profileProblem(m.profiles().find(Section::APP, "vlc")), "not removed"));
    // delete a profile that is gone already
    assert(m.removeSection(m.profiles().find(Section::APP, "gimp"), &err));
    std::filesystem::remove(d + "/apps/gimp.conf");
    assert(m.save(id).saved);
    // delete and add the same name again before the save: the file is taken over, no conflict
    assert(m.removeSection(m.profiles().find(Section::APP, "vlc"), &err));
    const size_t again = m.addSection(Section::APP, "VLC", &err);
    assert(again != ProfileSet::npos);
    r = m.save(id);
    assert(r.saved || r.conflict);  // the file was edited by hand above: the stamp is the one of the reload
    tu::removeAll(d + "/apps");
    std::filesystem::create_directories(d + "/apps");

    // rename: the new file first, then the old one is removed; the content moves with it
    put(d + "/apps/old.conf", "[app:old]\nZOOM_SENS = 3\n");
    put(d + "/apps/keep.conf", "[app:keep]\nZOOM_SENS = 4\n");
    m = load(d);
    const size_t o = m.profiles().find(Section::APP, "old");
    assert(m.renameSection(o, "newname", &err) && m.dirty());
    assert(!m.renameSection(m.profiles().find(Section::APP, "newname"), "KEEP", &err) && has(err, "already exists"));
    assert(m.save(id).saved);
    assert(!tu::exists(d + "/apps/old.conf") && has(get(d + "/apps/newname.conf"), "[app:newname]") &&
           has(get(d + "/apps/newname.conf"), "ZOOM_SENS = 3"));
    assert(order(load(d), Section::APP).find("newname") != std::string::npos);
    // a rename that only changes the case keeps the file name (same slug) and the settings
    m = load(d);
    assert(m.renameSection(m.profiles().find(Section::APP, "newname"), "NewName", &err) && m.save(id).saved);
    assert(has(get(d + "/apps/newname.conf"), "[app:NewName]") && has(get(d + "/apps/newname.conf"), "ZOOM_SENS = 3"));
    // the order (PRIORITY) moves with the profile
    put(d + "/apps/keep.conf", "[app:keep]\nPRIORITY = 10\n");
    put(d + "/apps/newname.conf", "[app:NewName]\nPRIORITY = 20\n");
    m = load(d);
    assert(order(m, Section::APP) == "keep,NewName");
    assert(m.renameSection(m.profiles().find(Section::APP, "keep"), "zzz", &err) && m.save(id).saved);
    assert(order(load(d), Section::APP) == "zzz,NewName");
    // a rename re-sorts at once (ties of PRIORITY go by file name) and the caller finds the profile by its name
    put(d + "/apps/keep.conf", "[app:keep]\n");
    put(d + "/apps/zzz.conf", "[app:zzz]\n");
    std::filesystem::remove(d + "/apps/newname.conf");
    m = load(d);
    assert(order(m, Section::APP) == "keep,zzz");
    size_t moved = ProfileSet::npos;
    assert(spn::ui::ProfileTreeModel::rename(m, m.profiles().find(Section::APP, "keep"), "zzzz", &err, &moved));
    assert(order(m, Section::APP) == "zzz,zzzz");
    assert(moved == m.profiles().find(Section::APP, "zzzz") && m.sections()[moved].pattern == "zzzz");
    assert(spn::ui::ProfileTreeModel::resolve(m, Section::APP, "keep") == ProfileSet::npos &&
           spn::ui::ProfileTreeModel::resolve(m, Section::APP, "ZZZ") == m.profiles().find(Section::APP, "zzz"));
    std::filesystem::remove(d + "/apps/keep.conf");  // back to the state the next checks expect
    put(d + "/apps/zzz.conf", "[app:zzz]\nPRIORITY = 10\n");
    put(d + "/apps/newname.conf", "[app:NewName]\nPRIORITY = 20\n");
    // the old file was changed by hand in between: it is left in place and the user is told
    m = load(d);
    const size_t z =m.profiles().find(Section::APP, "zzz");
    assert(m.renameSection(z, "yyy", &err));
    put(d + "/apps/zzz.conf", "[app:zzz]\nPRIORITY = 10\n# hand\n");
    r = m.save(id);
    assert(has(get(d + "/apps/yyy.conf"), "[app:yyy]") && tu::exists(d + "/apps/zzz.conf"));
    assert(r.saved && has(m.profileProblem(m.profiles().find(Section::APP, "yyy")), "left in place"));  // told, nothing lost
    // a rename of a profile in the old single file
    tu::removeAll(d + "/apps");
    std::filesystem::create_directories(d + "/apps");
    put(d + "/dofscroll.conf", "[global]\n[app:legacy]\nZOOM_SENS = 3\n");
    m = load(d);
    assert(m.renameSection(m.profiles().find(Section::APP, "legacy"), "renamed", &err) && m.save(id).saved);
    assert(has(get(d + "/apps/renamed.conf"), "ZOOM_SENS = 3") && !has(get(d + "/dofscroll.conf"), "legacy") &&
           !has(get(d + "/dofscroll.conf"), "[app:renamed"));
    tu::removeAll(d);
}

static void testImportExport() {
    const std::string d = makeDir("split-model-import");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/firefox.conf", "[app:firefox]\nZOOM_SENS = 3\nPRIORITY = 10\n");
    put(d + "/apps/vlc.conf", "[app:vlc]\nZOOM_SENS = 4\nPRIORITY = 20\n");
    SettingsModel m = load(d);
    std::string err;
    // export is a copy of the file, and importing it into another configuration gives the same profile
    const std::string out = d + "/export.conf";
    assert(exportProfileTo(m, m.profiles().find(Section::APP, "firefox"), out, &err));
    assert(get(out) == get(d + "/apps/firefox.conf"));
    const std::string d2 = makeDir("split-model-import2");
    put(d2 + "/dofscroll.conf", "[global]\n");
    SettingsModel m2 = load(d2);
    ImportPlan p = planProfileImport(m2, out, id);
    assert(p.ok && p.kind == Section::APP && p.name == "firefox" && !p.collision && p.keys.size() == 1);
    ImportResult r = applyProfileImport(m2, p, ImportChoice::Keep, id);
    assert(r.done && has(r.message, "imported") && m2.save(id).saved);
    assert(has(get(d2 + "/apps/firefox.conf"), "ZOOM_SENS = 3") && !has(get(d2 + "/apps/firefox.conf"), "PRIORITY"));
    // a device profile goes to devices/
    put(d + "/dev.conf", "[device:SpaceMouse Pro]\nBUTTON_1 = nav\n");
    p = planProfileImport(m2, d + "/dev.conf", id);
    assert(p.ok && p.kind == Section::DEVICE);
    assert(applyProfileImport(m2, p, ImportChoice::Replace, id).done && m2.save(id).saved);
    assert(has(get(d2 + "/devices/spacemouse pro.conf"), "BUTTON_1 = nav"));
    // a collision: Keep leaves the existing profile (and says so), Replace changes it only on request
    put(d + "/other.conf", "[app:FireFox]\nZOOM_SENS = 9\n");
    p = planProfileImport(m2, d + "/other.conf", id);
    assert(p.ok && p.collision && p.existing == m2.profiles().find(Section::APP, "firefox"));
    r = applyProfileImport(m2, p, ImportChoice::Keep, id);
    assert(!r.done && has(r.message, "was kept") && !m2.dirty() && m2.effective(p.existing).zoom.sensitivity == 3);
    r = applyProfileImport(m2, p, ImportChoice::Replace, id);
    assert(r.done && has(r.message, "replaced") && m2.save(id).saved);
    assert(has(get(d2 + "/apps/firefox.conf"), "ZOOM_SENS = 9") && has(get(d2 + "/apps/firefox.conf"), "[app:firefox]"));
    // refused without any change: global file, several profiles, no profile, bad value, unreadable
    put(d + "/g.conf", "[global]\nSCROLL_V_SENS = 3\n");
    assert(!planProfileImport(m2, d + "/g.conf", id).ok && has(planProfileImport(m2, d + "/g.conf", id).error, "Global"));
    put(d + "/two.conf", "[app:a]\n[app:b]\n");
    assert(!planProfileImport(m2, d + "/two.conf", id).ok && has(planProfileImport(m2, d + "/two.conf", id).error, "several"));
    put(d + "/none.conf", "# nothing\n");
    assert(!planProfileImport(m2, d + "/none.conf", id).ok);
    put(d + "/bad.conf", "[app:bad]\nZOOM_SENS = abc\nSCROLL_V_SENS = 4\n");
    p = planProfileImport(m2, d + "/bad.conf", id);
    assert(p.ok && !p.warnings.empty() && p.keys.size() == 1);  // the invalid line is dropped and reported
    assert(!planProfileImport(m2, d + "/missing.conf", id).ok);
    assert(!m2.dirty());
    // exporting a profile with unsaved edits renders the edited section; Global exports as [global]
    assert(m.setValue(m.profiles().find(Section::APP, "firefox"), "ZOOM_SENS", "6", &err));
    assert(has(profileExportText(m, m.profiles().find(Section::APP, "firefox")), "ZOOM_SENS = 6"));
    assert(has(profileExportText(m, 0), "[global]"));
    assert(profileExportText(m, 99).empty());
    tu::removeAll(d);
    tu::removeAll(d2);
}

// Undo of a removal: all settings and the place in the order come back, before and after the autosave deleted the file.
static void testRemoveUndo() {
    const std::string d = makeDir("split-model-undo");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/a.conf", "[app:a]\nZOOM_SENS = 3\nPRIORITY = 10\n");
    put(d + "/apps/b.conf", "[app:b]\nZOOM_SENS = 4\nBUTTON_1 = key:space\nPRIORITY = 20\n");
    put(d + "/apps/c.conf", "[app:c]\nZOOM_SENS = 5\nPRIORITY = 30\n");
    SettingsModel m = load(d);
    std::string err;
    for (int round = 0; round < 2; ++round) {  // 0: undo before the save, 1: after the file was deleted
        const size_t b = m.profiles().find(Section::APP, "b");
        const RemovedProfile snap = captureProfile(m, b);
        assert(snap.valid && snap.place == 1 && snap.pattern == "b");
        assert(m.removeSection(b, &err));
        if (round == 1) {
            assert(m.save(id).saved && !tu::exists(d + "/apps/b.conf"));
        }
        size_t idx = 0;
        assert(restoreProfile(m, snap, &idx, &err) && idx == m.profiles().find(Section::APP, "b"));
        assert(order(m, Section::APP) == "a b c" || order(m, Section::APP) == "a, b, c" || has(order(m, Section::APP), "b"));
        assert(m.effective(idx).zoom.sensitivity == 4);
        assert(m.save(id).saved);
        assert(has(get(d + "/apps/b.conf"), "ZOOM_SENS = 4") && has(get(d + "/apps/b.conf"), "BUTTON_1 = key:space"));
        assert(m.profiles().find(Section::APP, "a") < m.profiles().find(Section::APP, "b") &&
               m.profiles().find(Section::APP, "b") < m.profiles().find(Section::APP, "c"));
    }
    // No time limit: 60 s, 10 minutes and 1 hour later the same copy restores the same profile (the copy holds no
    // clock; only a change of the list, a reload or Dismiss ends the offer, which is the page's rule).
    for (int elapsed : {60, 600, 3600}) {
        (void)elapsed;
        const size_t c = m.profiles().find(Section::APP, "c");
        const RemovedProfile snapC = captureProfile(m, c);
        assert(m.removeSection(c, &err));
        size_t idx = 0;
        assert(restoreProfile(m, snapC, &idx, &err) && m.effective(idx).zoom.sensitivity == 5);
    }
    // a profile with this name exists again: nothing is restored; nothing to restore for Global or a bad index
    const RemovedProfile snap = captureProfile(m, m.profiles().find(Section::APP, "a"));
    assert(!restoreProfile(m, snap, nullptr, &err) && has(err, "exists"));
    assert(!captureProfile(m, 0).valid && !captureProfile(m, 99).valid && !restoreProfile(m, RemovedProfile(), nullptr, &err));
    tu::removeAll(d);
}

#ifndef _WIN32
static void testSymlinkSingleFile() {
    const std::string d = makeDir("split-model-link"), repo = tu::makeTempDir("split-model-repo");
    put(repo + "/real.conf", "[global]\nSCROLL_V_SENS = 7\n[app:firefox]\nZOOM_SENS = 3\n");
    std::filesystem::create_symlink(repo + "/real.conf", d + "/dofscroll.conf");
    SettingsModel m = load(d);
    std::string err;
    assert(!m.splitLayout());
    const size_t ff = m.profiles().find(Section::APP, "firefox");
    assert(ff != ProfileSet::npos && m.inSingleFile(ff));
    // edits and new profiles stay in the single file; the link is kept
    assert(m.setValue(ff, "ZOOM_SENS", "5", &err));
    const size_t nu = m.addSection(Section::APP, "vlc", &err);
    assert(nu != ProfileSet::npos && m.setValue(nu, "ZOOM_SENS", "2", &err));
    assert(m.save(id).saved);
    assert(std::filesystem::is_symlink(d + "/dofscroll.conf"));
    const std::string txt = get(repo + "/real.conf");
    assert(has(txt, "[app:firefox]") && has(txt, "ZOOM_SENS = 5") && has(txt, "[app:vlc]") && has(txt, "SCROLL_V_SENS = 7"));
    assert(!tu::exists(d + "/apps/firefox.conf") && !tu::exists(d + "/apps/vlc.conf"));
    // a profile file next to a symlinked dofscroll.conf is still read and written as a file
    put(d + "/apps/gimp.conf", "[app:gimp]\nZOOM_SENS = 4\n");
    m = load(d);
    const size_t gi = m.profiles().find(Section::APP, "gimp");
    assert(gi != ProfileSet::npos && !m.inSingleFile(gi) && m.setValue(gi, "ZOOM_SENS", "6", &err) && m.save(id).saved);
    assert(has(get(d + "/apps/gimp.conf"), "ZOOM_SENS = 6") && std::filesystem::is_symlink(d + "/dofscroll.conf"));
    // the migration skips a link, and the model keeps the single-file mode
    const MigrateResult mr = migrateToSplit(d);
    assert(mr.status == MigrateStatus::SkippedSymlink || mr.status == MigrateStatus::NothingToDo);
    // a differently named file is a single file too
    put(d + "/other-name.conf", "[global]\n[app:x]\nZOOM_SENS = 1\n");
    SettingsModel o;
    assert(o.load(d + "/other-name.conf", nullptr, &err) && !o.splitLayout());
    assert(o.setValue(o.profiles().find(Section::APP, "x"), "ZOOM_SENS", "2", &err) && o.save(id).saved);
    assert(has(get(d + "/other-name.conf"), "ZOOM_SENS = 2") && !tu::exists(d + "/apps/x.conf"));
    tu::removeAll(d);
    tu::removeAll(repo);
}
#endif

static void testFirstStartAndFirstDevice() {
    const std::string root = tu::makeTempDir("split-model-first");
    std::filesystem::create_directories(root + "/cfg");
    const std::string cfg = tu::useConfigBase(root + "/cfg");
    const std::string dir = cfg + "/dofscroll", conf = dir + "/dofscroll.conf";
    std::filesystem::create_directories(dir);
    put(conf, "[global]\nSCROLL_V_SENS = 7\n[app:firefox]\nZOOM_SENS = 3\n[app:vlc]\nZOOM_SENS = 4\n");
    // the first start migrates, says so once, and the model shows the profiles from their files in the old order
    FirstStart r = runFirstStart();
    assert(r.split.status == MigrateStatus::Migrated && has(r.text("dofscroll-ui"), "one file each"));
    assert(!has(get(conf), "[app:") && tu::exists(dir + "/apps/firefox.conf") && tu::exists(dir + "/backup/dofscroll.conf.pre-split"));
    assert(runFirstStart().text("dofscroll-ui").find("one file each") == std::string::npos);  // once
    SettingsModel m;
    std::string err;
    assert(m.load(conf, nullptr, &err) && m.splitLayout() && order(m, Section::APP) == "firefox,vlc");
    assert(m.effective(m.profiles().find(Section::APP, "vlc")).zoom.sensitivity == 4 && !m.dirty());
    // the first device profile is written as devices/<slug>.conf, once
    assert(!tu::exists(dir + "/devices/spacemouse pro.conf"));
    assert(writeFirstDeviceProfileFile(dir, "SpaceMouse Pro", &err));
    assert(has(get(dir + "/devices/spacemouse pro.conf"), "BUTTON_1 = nav"));
    assert(!writeFirstDeviceProfileFile(dir, "SpaceMouse Compact", &err));
    assert(m.load(conf, nullptr, &err) && m.profiles().find(Section::DEVICE, "SpaceMouse Pro") != ProfileSet::npos);
    // an edit after the migration writes the one file
    const size_t vlc = m.profiles().find(Section::APP, "vlc");
    const std::string ffStamp = stampOf(dir + "/apps/firefox.conf");
    assert(m.setValue(vlc, "ZOOM_SENS", "9", &err) && m.save(id).saved);
    assert(has(get(dir + "/apps/vlc.conf"), "ZOOM_SENS = 9") && stampOf(dir + "/apps/firefox.conf") == ffStamp);
    tu::removeAll(root);
}

static void testHostileImportAndDisplay() {
    const std::string d = makeDir("split-model-hostile");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/mine.conf", "[app:mine]\nPRIORITY = 50\n");
    SettingsModel m = load(d);
    std::string err;
    put(d + "/h.conf", "[app:e]\nPRIORITY = 1000\nENABLED = 0\nTICK_HZ = 30\nBACKEND = x11\nBUTTON_1 = key:ctrl+alt+delete\n");
    ImportPlan p = planProfileImport(m, d + "/h.conf", id);
    assert(p.ok && p.needsConfirm() && p.risks.size() == 2 && p.notes.size() == 4 && p.keys.size() == 1);
    assert(has(p.risks[0] + p.risks[1], "very short") && has(p.risks[0] + p.risks[1], "chords"));
    assert(applyProfileImport(m, p, ImportChoice::Replace, id).done);
    const size_t e = m.profiles().find(Section::APP, "e");
    assert(m.priority(e) == 40 && m.priority(m.profiles().find(Section::APP, "mine")) == 50);
    assert(order(m, Section::APP) == "e,mine");  // never applied after (above) the user's own profile
    assert(m.save(id).saved && !has(get(d + "/apps/e.conf"), "ENABLED") && !has(get(d + "/apps/e.conf"), "TICK_HZ"));
    assert(!m.chordNote(e, id).empty() && m.chordNote(m.profiles().find(Section::APP, "mine"), id).empty());
    // spoofing characters are not displayed
    assert(displayText(std::string("a\xE2\x80\xAE" "b\xE2\x80\x8B" "c\x1b[31m")) == "abc[31m");
    tu::removeAll(d);
}

static void testStateAcrossEdits() {
    // removing and adding keep the file bookkeeping straight (section indexes shift)
    const std::string d = makeDir("split-model-state");
    put(d + "/dofscroll.conf", "[global]\n");
    for (const char *n : {"a", "b", "c", "d"}) put(d + "/apps/" + n + ".conf", std::string("[app:") + n + "]\nZOOM_SENS = 3\n");
    SettingsModel m = load(d);
    std::string err;
    assert(m.removeSection(m.profiles().find(Section::APP, "b"), &err));
    assert(m.setValue(m.profiles().find(Section::APP, "c"), "ZOOM_SENS", "7", &err));
    assert(m.addSection(Section::APP, "e", &err) != ProfileSet::npos);
    assert(m.renameSection(m.profiles().find(Section::APP, "d"), "dd", &err));
    assert(m.save(id).saved && !m.dirty());
    assert(!tu::exists(d + "/apps/b.conf") && !tu::exists(d + "/apps/d.conf") && tu::exists(d + "/apps/dd.conf") &&
           tu::exists(d + "/apps/e.conf") && has(get(d + "/apps/c.conf"), "ZOOM_SENS = 7") &&
           has(get(d + "/apps/a.conf"), "ZOOM_SENS = 3"));
    // setProfiles (tests, other sources) writes everything on the first save
    SettingsModel s;
    ProfileSet ps;
    ps.findOrAdd(Section::APP, "q");
    s.setProfiles(ps, d + "/dofscroll.conf");
    assert(!s.dirty() && s.save(id).saved && tu::exists(d + "/apps/q.conf"));
    tu::removeAll(d);
}

// Writes a one-profile file and plans its import into m.
static ImportPlan planOf(const SettingsModel &m, const std::string &dir, const std::string &body) {
    put(dir + "/imp.conf", body);
    return planProfileImport(m, dir + "/imp.conf", id);
}

static void testImportRiskUsesParser() {
    const std::string d = makeDir("split-model-risk");
    put(d + "/dofscroll.conf", "[global]\n");
    SettingsModel m = load(d);
    ModeSet ms;
    ModeDef normal;
    normal.id = "normal";
    normal.builtin = true;
    ModeDef own;
    own.id = "custom";
    ms.modes = {normal, own};
    m.setModes(ms, "");
    // the parser trims around '+': none of these is harmless
    for (const char *line : {"BUTTON_1 = key:ctrl +w", "BUTTON_1 = key:alt + f4", "V_ACTION = step:ctrl +w,ctrl +t",
                             "BUTTON_1 = key:shift+delete", "BUTTON_1 = key:delete", "BUTTON_1 = key:f4", "BUTTON_1 = key:super",
                             "BUTTON_1 = key:enter", "BUTTON_1 = key:esc", "BUTTON_2 = click", "BUTTON_2 = RightClick",
                             "BUTTON_2 = middleclick", "CLICK_BUTTON = 3", "MODE = custom", "BUTTON_3 = mode:custom",
                             "BUTTON_4 = buttonmode:my-own"}) {
        const ImportPlan p = planOf(m, d, std::string("[app:programname]\n") + line + "\n");
        if (!p.ok || p.risks.empty()) {
            fprintf(stderr, "not flagged: %s\n", line);
            assert(false);
        }
    }
    for (const char *line : {"BUTTON_1 = key:space", "BUTTON_1 = nav", "BUTTON_1 = key:ctrl+pageup", "SCROLL_V_SENS = 3", "MODE = normal",
                             "V_ACTION = keys:ctrl+pageup,ctrl+pagedown", "BUTTON_1 = none", "BUTTON_4 = buttonmode:next-mode"}) {
        const ImportPlan p = planOf(m, d, std::string("[app:programname]\n") + line + "\n");
        if (!p.ok || !p.risks.empty()) {
            fprintf(stderr, "flagged: %s\n", line);
            assert(false);
        }
    }
    // the canonical value is written, not the raw text
    ImportPlan p = planOf(m, d, "[app:programname]\nBUTTON_1 = key:ctrl +w\nV_ACTION = step:ctrl +w,ctrl +t\n");
    assert(p.ok && applyProfileImport(m, p, ImportChoice::Replace, id).done && m.save(id).saved);
    const std::string f = get(d + "/apps/programname.conf");
    assert(has(f, "BUTTON_1 = key:ctrl+w") && !has(f, "ctrl +w"));
    // and the profile note sees it
    assert(!m.chordNote(m.profiles().find(Section::APP, "programname"), id).empty());
    tu::removeAll(d);
}

// A Button mode of the user's own (also one that overrides a built-in id) is flagged when it is bound (issue #268).
static void testImportRiskSeesOwnButtonModes() {
    const std::string d = makeDir("split-model-riskbm");
    SettingsModel m = load(d);
    assert(!m.bindingIsRisky("BUTTON_4", "buttonmode:next-mode"));  // without a catalog: the built-in library
    assert(m.bindingIsRisky("BUTTON_4", "buttonmode:my-own"));
    std::filesystem::create_directories(d + "/buttons");
    put(d + "/buttons/my-own.conf", "NAME = Mine\nACTION = key:delete\n");
    put(d + "/buttons/next-mode.conf", "NAME = Not the library one\nACTION = click\n");
    const ButtonModeSet set = loadButtonModes(d + "/buttons", nullptr);
    assert(set.find("my-own") && !set.find("my-own")->builtin);
    assert(set.find("next-mode") && !set.find("next-mode")->builtin);
    assert(set.find("prev-mode") && set.find("prev-mode")->builtin);
    m.setButtonModes(&set);
    assert(m.bindingIsRisky("BUTTON_4", "buttonmode:my-own"));
    assert(m.bindingIsRisky("BUTTON_4", "ButtonMode:Next-Mode"));  // the user's file replaces the built-in one
    assert(!m.bindingIsRisky("BUTTON_4", "buttonmode:prev-mode"));
    assert(!m.bindingIsRisky("BUTTON_4", "buttonmode:latch-nav"));
    const ImportPlan p = planOf(m, d, "[app:programname]\nBUTTON_4 = buttonmode:next-mode\n");
    assert(p.ok && !p.risks.empty());
    tu::removeAll(d);
}

static void testNewAndImportedPriority() {
    const std::string d = makeDir("split-model-newprio");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/neg1.conf", "[app:neg1]\nPRIORITY = -30\n");
    put(d + "/apps/neg2.conf", "[app:neg2]\nPRIORITY = -10\n");
    put(d + "/devices/solo.conf", "[device:solo]\nZOOM_SENS = 2\n");
    SettingsModel m = load(d);
    std::string err;
    // a new profile of the user is applied last, also when every priority is negative or there is one profile
    size_t n = m.addSection(Section::APP, "newone", &err);
    assert(n != ProfileSet::npos && m.priority(n) == 0 && order(m, Section::APP) == "neg1,neg2,newone");
    assert(m.priority(m.profiles().find(Section::APP, "neg2")) < m.priority(n));
    n = m.addSection(Section::DEVICE, "second", &err);
    assert(m.priority(n) > m.priority(m.profiles().find(Section::DEVICE, "solo")) && order(m, Section::DEVICE) == "solo,second");
    // an imported one is applied before every profile of the user
    ImportPlan p = planOf(m, d, "[app:imported]\nZOOM_SENS = 3\n");
    assert(applyProfileImport(m, p, ImportChoice::Replace, id).done);
    assert(order(m, Section::APP) == "imported,neg1,neg2,newone" && m.priority(m.profiles().find(Section::APP, "imported")) == -40);
    assert(m.save(id).saved);
    assert(order(load(d), Section::APP) == "imported,neg1,neg2,newone");
    // all priorities 0 (hand-written): import goes first, new goes last
    tu::removeAll(d + "/apps");
    std::filesystem::create_directories(d + "/apps");
    put(d + "/apps/b.conf", "[app:b]\n");
    put(d + "/apps/c.conf", "[app:c]\n");
    m = load(d);
    p = planOf(m, d, "[app:a]\nPRIORITY = 1000\n");
    assert(applyProfileImport(m, p, ImportChoice::Replace, id).done && order(m, Section::APP) == "a,b,c");
    n = m.addSection(Section::APP, "z0", &err);
    assert(order(m, Section::APP) == "a,b,c,z0" && m.save(id).saved && order(load(d), Section::APP) == "a,b,c,z0");
    // the range ends: the kind is numbered again instead of overflowing
    tu::removeAll(d + "/apps");
    std::filesystem::create_directories(d + "/apps");
    put(d + "/apps/hi.conf", "[app:hi]\nPRIORITY = 995\n");
    put(d + "/apps/lo.conf", "[app:lo]\nPRIORITY = -995\n");
    m = load(d);
    n = m.addSection(Section::APP, "last", &err);
    assert(order(m, Section::APP) == "lo,hi,last");
    assert(m.priority(m.profiles().find(Section::APP, "last")) > m.priority(m.profiles().find(Section::APP, "hi")));
    p = planOf(m, d, "[app:first]\n");
    assert(applyProfileImport(m, p, ImportChoice::Replace, id).done && order(m, Section::APP) == "first,lo,hi,last");
    assert(m.priority(m.profiles().find(Section::APP, "first")) < m.priority(m.profiles().find(Section::APP, "lo")));
    for (const SettingsModel::SectionInfo &si : m.sections())
        if (si.index) assert(m.priority(si.index) >= kMinPriority && m.priority(si.index) <= kMaxPriority);
    // the UI order agrees with the loader on equal priorities (legacy first, then the file name)
    tu::removeAll(d + "/apps");
    std::filesystem::create_directories(d + "/apps");
    put(d + "/dofscroll.conf", "[global]\n[app:legacy]\n");
    put(d + "/apps/m.conf", "[app:m]\nPRIORITY = 5\n");
    put(d + "/apps/k.conf", "[app:k]\nPRIORITY = 5\n");
    m = load(d);
    assert(m.renameSection(m.profiles().find(Section::APP, "m"), "a-first", &err));
    // the text about the order is the order of the daemon, also before anything is saved or sorted
    assert(has(m.orderText(m.profiles().find(Section::APP, "k"), id), "Applied after \"a-first\""));
    assert(m.save(id).saved);
    n = m.addSection(Section::APP, "zz", &err);  // sorts the model
    assert(order(m, Section::APP) == "legacy,a-first,k,zz" && order(m, Section::APP) == order(load(d), Section::APP) + ",zz");
    tu::removeAll(d);
}

static void testStaleImportPlan() {
    const std::string d = makeDir("split-model-stale");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/mine.conf", "[app:mine]\nZOOM_SENS = 7\n");
    SettingsModel m = load(d);
    std::string err;
    // planned without a collision, then a profile of this name appears (reload during the dialog)
    ImportPlan p = planOf(m, d, "[app:other]\nZOOM_SENS = 9\n");
    assert(p.ok && !p.collision);
    put(d + "/apps/other.conf", "[app:other]\nZOOM_SENS = 2\n");
    assert(m.load(d + "/dofscroll.conf", nullptr, &err));
    ImportResult r = applyProfileImport(m, p, ImportChoice::Replace, id);
    assert(!r.done && !r.message.empty() && !m.dirty());
    assert(m.profiles().find(Section::APP, "other") != ProfileSet::npos && m.profiles().find(Section::APP, "mine") != ProfileSet::npos);
    assert(m.effective(m.profiles().find(Section::APP, "other")).zoom.sensitivity == 2);
    // planned with a collision, then the profile is gone: nothing else is touched
    p = planOf(m, d, "[app:other]\nZOOM_SENS = 9\n");
    assert(p.ok && p.collision);
    std::filesystem::remove(d + "/apps/other.conf");
    assert(m.load(d + "/dofscroll.conf", nullptr, &err));
    r = applyProfileImport(m, p, ImportChoice::Replace, id);
    assert(!r.done && !m.dirty() && m.profiles().find(Section::APP, "other") == ProfileSet::npos);
    assert(m.effective(m.profiles().find(Section::APP, "mine")).zoom.sensitivity == 7);
    // an unchanged plan still works; so does one whose section index moved
    p = planOf(m, d, "[app:aaa]\nZOOM_SENS = 4\n");
    assert(applyProfileImport(m, p, ImportChoice::Replace, id).done);
    p = planOf(m, d, "[app:MINE]\nZOOM_SENS = 5\n");
    assert(p.collision);
    r = applyProfileImport(m, p, ImportChoice::Replace, id);
    assert(r.done && m.effective(m.profiles().find(Section::APP, "mine")).zoom.sensitivity == 5 &&
           m.profiles().find(Section::APP, "aaa") != ProfileSet::npos);
    tu::removeAll(d);
}

static void testImportNameChecks() {
    const std::string d = makeDir("split-model-names");
    put(d + "/dofscroll.conf", "[global]\n");
    SettingsModel m = load(d);
    for (const char *nm : {"a\xE2\x80\xAE" "b" "longname", "zero\xE2\x80\x8Bwidth", "bom\xEF\xBB\xBFname", "ctl\x01name",
                           "bidi\xE2\x81\xA7" "name"}) {
        const ImportPlan p = planOf(m, d, std::string("[app:") + nm + "]\nZOOM_SENS = 3\n");
        if (p.ok) fprintf(stderr, "accepted: %s\n", nm);
        assert(!p.ok && !p.error.empty());
    }
    assert(planOf(m, d, "[app:firefox]\n").ok);
    // four code points or fewer: the warning; five: none
    assert(!planOf(m, d, "[app:abcd]\n").risks.empty());
    assert(planOf(m, d, "[app:abcde]\n").risks.empty());
    assert(!planOf(m, d, "[app:\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F]\n").risks.empty());
    tu::removeAll(d);
}

static void testRenameOntoRemoved() {
    const std::string d = makeDir("split-model-rename-removed");
    put(d + "/dofscroll.conf", "[global]\n");
    put(d + "/apps/vlc.conf", "[app:vlc]\nZOOM_SENS = 4\n");
    put(d + "/apps/foo.conf", "[app:foo]\nZOOM_SENS = 6\n");
    SettingsModel m = load(d);
    std::string err;
    assert(m.removeSection(m.profiles().find(Section::APP, "vlc"), &err));
    assert(m.renameSection(m.profiles().find(Section::APP, "foo"), "vlc", &err));
    const SettingsModel::SaveResult r = m.save(id);
    assert(r.saved && !r.conflict);
    assert(tu::exists(d + "/apps/vlc.conf") && !tu::exists(d + "/apps/foo.conf"));
    assert(has(get(d + "/apps/vlc.conf"), "[app:vlc]") && has(get(d + "/apps/vlc.conf"), "ZOOM_SENS = 6"));
    const SettingsModel m2 = load(d);
    assert(m2.profiles().find(Section::APP, "vlc") != ProfileSet::npos && m2.profiles().find(Section::APP, "foo") == ProfileSet::npos);
    tu::removeAll(d);
}

int main() {
    testLoadSaveOnlyChanged();
    testConflict();
    testDefectiveAndLegacy();
    testDefectiveNameBlocks();
    testPriorityMoves();
    testDeleteAndRename();
    testImportExport();
    testRemoveUndo();
#ifndef _WIN32
    testSymlinkSingleFile();
#endif
    testFirstStartAndFirstDevice();
    testStateAcrossEdits();
    testHostileImportAndDisplay();
    testImportRiskUsesParser();
    testImportRiskSeesOwnButtonModes();
    testNewAndImportedPriority();
    testStaleImportPlan();
    testImportNameChecks();
    testRenameOntoRemoved();
    return 0;
}
