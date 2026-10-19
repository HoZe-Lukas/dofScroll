// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for the Profiles page models: key list, binding editor, profile tree, page layout.
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

#include "platform/running_apps.hpp"
#include "testutil.hpp"
#include "ui/model/binding_editor_model.hpp"
#include "ui/model/key_list.hpp"
#include "ui/model/profile_tree_model.hpp"

using namespace spn;
using namespace spn::ui;

static void testKeyList() {
    const std::vector<std::string> all = allKeyNames();
    assert(all.size() == kKeyCount);
    std::set<std::string> seen;
    size_t n = 0;
    for (const KeyGroup &g : keyGroups()) {
        assert(!g.keys.empty() && !g.title.empty());
        for (const std::string &k : g.keys) {
            assert(seen.insert(k).second);  // each key in exactly one group
            Key key;
            assert(parseKeyName(k, &key));
            ++n;
        }
    }
    assert(n == kKeyCount);
    assert(seen.count("plus") && seen.count("kp_plus") && seen.count("kp_minus") && seen.count("space"));
}

static void testBindingParse() {
    Binding b;
    assert(parseBinding("none", &b) && b.kind == BindingKind::None);
    assert(parseBinding(" Click ", &b) && b.kind == BindingKind::Click);
    assert(parseBinding("rightclick", &b) && b.kind == BindingKind::RightClick);
    assert(parseBinding("middleclick", &b) && b.kind == BindingKind::MiddleClick);
    assert(parseBinding("nav", &b) && b.kind == BindingKind::Nav);
    assert(parseBinding("key:kp_plus", &b) && b.kind == BindingKind::Key && b.arg == "kp_plus");
    assert(parseBinding("mode:next", &b) && b.kind == BindingKind::ModeNext);
    assert(parseBinding("mode:timeline", &b) && b.kind == BindingKind::Mode && b.arg == "timeline");
    assert(!parseBinding("key:nosuchkey", &b));
    assert(!parseBinding("mode:Bad Id", &b));
    assert(!parseBinding("jump", &b));

    // compose is the inverse of parse for every action
    for (const char *t : {"none", "click", "rightclick", "middleclick", "nav", "key:plus", "mode:next",
                          "mode:off"}) {
        assert(parseBinding(t, &b));
        assert(composeBinding(b) == t);
    }
    assert(composeBinding(Binding{BindingKind::Key, ""}).empty());
    assert(composeBinding(Binding{BindingKind::Mode, ""}).empty());
}

static void testPrimaryAdvanced() {
    // Primary: only what the vendor driver cannot do.
    const std::vector<BindingKindInfo> prim = bindingKindsFor(BindingContext::Global, false);
    std::set<BindingKind> p;
    for (const auto &k : prim) p.insert(k.kind);
    assert(p == (std::set<BindingKind>{BindingKind::None, BindingKind::Nav, BindingKind::Mode,
                                       BindingKind::ModeNext}));
    // All capabilities stay available behind "Advanced".
    assert(bindingKindsFor(BindingContext::Global, true).size() == 8);
    for (BindingKind k : {BindingKind::Click, BindingKind::RightClick, BindingKind::MiddleClick, BindingKind::Key})
        assert(isAdvancedKind(k));
    for (BindingKind k : {BindingKind::None, BindingKind::Nav, BindingKind::Mode, BindingKind::ModeNext})
        assert(!isAdvancedKind(k));
    // primary entries come first in the full list
    const auto full = bindingKindsFor(BindingContext::Global, true);
    for (size_t i = 0; i < prim.size(); ++i) assert(full[i].kind == prim[i].kind);

    // Specific profiles (device/app): everything first-class, no advanced section, no hint.
    assert(bindingContextOf(0) == BindingContext::Global && hasAdvancedSection(BindingContext::Global));
    assert(bindingContextOf(3) == BindingContext::Specific && !hasAdvancedSection(BindingContext::Specific));
    assert(bindingKindsFor(BindingContext::Specific, false).size() == 8);
    for (BindingKind k : {BindingKind::Click, BindingKind::RightClick, BindingKind::MiddleClick, BindingKind::Key})
        assert(!isAdvancedIn(BindingContext::Specific, k) && isAdvancedIn(BindingContext::Global, k));
    {
        std::vector<BindingRow> rows(1);
        rows[0].binding.kind = BindingKind::Key;
        assert(anyAdvancedBinding(rows) && !anyAdvancedBinding(rows, BindingContext::Specific));
    }

    const std::vector<std::string> modes = {"normal", "timeline"};
    Binding cur;
    Binding k = withKind(cur, BindingKind::Key, modes);
    assert(k.arg == "space");
    assert(withKind(k, BindingKind::Key, modes).arg == "space");
    Binding m = withKind(cur, BindingKind::Mode, modes);
    assert(m.arg == "normal");
    m.arg = "timeline";
    assert(withKind(m, BindingKind::Mode, modes).arg == "timeline");
    assert(withKind(m, BindingKind::Nav, modes).arg.empty());
}

static ModeSet modes() { return loadModes("", nullptr); }

static void testBindingRowsAndApply() {
    SettingsModel sm;
    ProfileSet ps;
    sm.setProfiles(ps, "");
    sm.setModes(modes(), "");
    sm.setDeviceInfo("", 0);
    std::string err;
    assert(applyBinding(sm, 0, 1, Binding{BindingKind::ModeNext, ""}, &err));
    std::vector<BindingRow> rows = bindingRows(sm, 0);
    assert(!rows.empty() && rows[0].number == 1 && rows[0].binding.kind == BindingKind::ModeNext);
    assert(rows[0].overridden && !anyAdvancedBinding(rows));
    assert(applyBinding(sm, 0, 2, Binding{BindingKind::Key, "kp_plus"}, &err));
    rows = bindingRows(sm, 0);
    assert(rows[1].text == "key:kp_plus" && anyAdvancedBinding(rows));
    assert(!applyBinding(sm, 0, 3, Binding{BindingKind::Key, ""}, &err) && !err.empty());
    assert(!applyBinding(sm, 0, 99, Binding{BindingKind::Nav, ""}, &err));
}

static void testTree() {
    SettingsModel sm;
    ProfileSet ps;
    sm.setProfiles(ps, "");
    sm.setModes(modes(), "");
    std::string err;
    assert(ProfileTreeModel::add(sm, Section::APP, "  zeta app ", &err) != ProfileSet::npos);
    assert(ProfileTreeModel::add(sm, Section::APP, "Alpha", &err) != ProfileSet::npos);
    assert(ProfileTreeModel::add(sm, Section::DEVICE, "Some Mouse", &err) != ProfileSet::npos);
    assert(ProfileTreeModel::add(sm, Section::APP, "bad]name", &err) == ProfileSet::npos && !err.empty());
    assert(ProfileTreeModel::add(sm, Section::APP, "   ", &err) == ProfileSet::npos);

    std::vector<ProfileEntry> e = ProfileTreeModel::entries(sm);
    assert(e.size() == 4);
    assert(e[0].isDefault && ProfileTreeModel::label(e[0]) == "Default mode");
    assert(e[1].kind == Section::DEVICE && e[1].pattern == "Some Mouse");
    assert(e[2].pattern == "Alpha" && e[3].pattern == "zeta app");
    assert(ProfileTreeModel::patterns(sm, Section::APP).size() == 2);

    // rename: validation, duplicates, [global]
    assert(ProfileTreeModel::rename(sm, e[2].section, "Beta", &err));
    assert(!ProfileTreeModel::rename(sm, e[2].section, "ZETA APP", &err));  // duplicate, case-insensitive
    assert(!ProfileTreeModel::rename(sm, e[2].section, "", &err));
    assert(!ProfileTreeModel::rename(sm, 0, "x", &err));
    assert(sm.profiles().find(Section::APP, "Beta") == e[2].section);
    assert(sm.profiles().find(Section::APP, "Alpha") == ProfileSet::npos);
    assert(ProfileTreeModel::add(sm, Section::APP, "alpha", &err) != ProfileSet::npos);  // name is free again

    // remove
    assert(!ProfileTreeModel::remove(sm, 0, &err));
    e = ProfileTreeModel::entries(sm);
    assert(ProfileTreeModel::remove(sm, e[1].section, &err));
    assert(ProfileTreeModel::entries(sm).size() == e.size() - 1);
}

static void testPickerWiring() {
    // The pickers flag existing profiles through the tree model's patterns.
    SettingsModel sm;
    ProfileSet ps;
    sm.setProfiles(ps, "");
    std::string err;
    ProfileTreeModel::add(sm, Section::APP, "firefox", &err);
    AppPickerModel pm;
    RunningApp a;
    a.matchString = "Firefox";
    a.displayName = "Firefox";
    RunningApp b;
    b.matchString = "gimp";
    b.displayName = "GIMP";
    pm.setApps({a, b});
    pm.setConfigured(ProfileTreeModel::patterns(sm, Section::APP));
    const auto rows = pm.rows("");
    assert(rows.size() == 2);
    assert(rows[0].app.displayName == "Firefox" && rows[0].hasProfile);
    assert(!rows[1].hasProfile);
}

static void testLayout() {
    SettingsModel sm;
    ProfileSet ps;
    sm.setProfiles(ps, "");
    sm.setModes(modes(), "");
    std::string err;
    const size_t app = ProfileTreeModel::add(sm, Section::APP, "viewer", &err);

    ProfilePageLayout g = profilePageLayout(sm, 0);
    assert(!g.modeSelector && g.bindingEditor && !g.modeInfo && !g.overridesSection && !g.editModeFile);

    ProfilePageLayout l = profilePageLayout(sm, app);
    assert(l.modeSelector && l.bindingEditor && l.overridesSection && !l.modeInfo);
    // first option is "Default mode", then the built-in modes
    const auto opts = sm.modeOptions(app);
    assert(opts.size() >= 2 && opts[0].id.empty() && opts[0].label == "Default mode");

    // Choosing another mode switches the layout (info instead of editor) ...
    int idx = -1;
    for (size_t i = 1; i < opts.size(); ++i)
        if (opts[i].id != "normal") idx = int(i);
    assert(idx > 0);
    bool changed = false;
    assert(sm.setMode(app, idx, &err, &changed) && changed);
    l = profilePageLayout(sm, app);
    assert(l.modeInfo && !l.bindingEditor && l.modeId == opts[size_t(idx)].id);
    assert(!l.editModeFile);  // built-in: no file
    // ... and "Default mode" clears MODE again.
    assert(sm.setMode(app, 0, &err, &changed) && changed);
    assert(!sm.isOverridden(app, "MODE"));
    assert(profilePageLayout(sm, app).bindingEditor);

    // unknown mode id in the file: treated like the default mode, flagged
    assert(sm.setMode(app, std::string("ghostmode"), &err));
    l = profilePageLayout(sm, app);
    assert(l.unknownMode && l.bindingEditor && !l.modeInfo);
    // [global] never takes a mode
    assert(!sm.setMode(0, 1, &err));
}

// Non-ASCII names, spaces and special characters in profile names (UTF-8, byte-exact).
static void testUtf8() {
    const std::vector<std::string> names = {
        "Pr\xc3\xa4sentation Manager",               // umlaut + space
        "caf\xc3\xa9.exe",                            // accent
        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e\xe3\x82\xa2\xe3\x83\x97\xe3\x83\xaa",  // CJK
        "rocket \xf0\x9f\x9a\x80 app",              // emoji (4-byte)
        "a#b;c=d [x",                                   // '#', ';', '=', '[' (no ']')
        "  padded name  "};
    SettingsModel sm;
    ProfileSet ps;
    sm.setProfiles(ps, "");
    std::string err;
    for (const std::string &n : names) {
        const size_t i = ProfileTreeModel::add(sm, Section::APP, n, &err);
        assert(i != ProfileSet::npos);
    }
    // Stored byte-identical (only ASCII outer whitespace is trimmed).
    for (size_t k = 0; k + 1 < names.size(); ++k) assert(sm.profiles().find(Section::APP, names[k]) != ProfileSet::npos);
    assert(sm.profiles().find(Section::APP, "padded name") != ProfileSet::npos);

    // Rejected with a message: ']' and line breaks (cannot be stored), blank.
    for (const char *bad : {"x]y", "a\nb", "a\rb", "", "   "}) {
        assert(ProfileTreeModel::add(sm, Section::APP, bad, &err) == ProfileSet::npos);
        assert(!err.empty());
    }

    // Save and load round trip keeps every name byte-exact.
    const std::string dir = tu::makeTempDir("profiles-utf8");
    const std::string path = dir + "/dofscroll.conf";
    sm.setProfiles(sm.profiles(), path);
    IdentityStrings strs;
    const SettingsModel::SaveResult r = sm.save(strs);
    assert(r.saved);
    SettingsModel sm2;
    assert(sm2.load(path, nullptr, &err));
    for (size_t k = 0; k + 1 < names.size(); ++k) {
        const bool found = sm2.profiles().find(Section::APP, names[k]) != ProfileSet::npos;
        if (!found) std::printf("UTF-8 round trip lost profile name #%zu\n", k);
        assert(found);
    }
    tu::removeAll(dir);

    // Tree order and picker filter work on UTF-8 text (substring on the bytes; case folding is
    // ASCII only).
    AppPickerModel pm;
    std::vector<RunningApp> apps;
    for (const std::string &n : names) {
        RunningApp a;
        a.matchString = n;
        a.displayName = n;
        apps.push_back(a);
    }
    pm.setApps(apps);
    assert(pm.rows("\xc3\xa4sent").size() == 1);                     // umlaut substring
    assert(pm.rows("\xf0\x9f\x9a\x80").size() == 1);                // emoji
    assert(pm.rows("\xe6\x97\xa5\xe6\x9c\xac").size() == 1);      // CJK prefix
    assert(pm.rows("A#B").size() == 1);                               // ASCII case-insensitive
    assert(pm.rows("rocket app").empty());                            // substring, not words
    assert(pm.rows("").size() == names.size());
    pm.setConfigured({"caf\xc3\xa9.exe"});
    for (const AppRow &row : pm.rows("caf")) assert(row.hasProfile);

    DevicePickerModel dm;
    DeviceInfo d;
    d.name = "Ger\xc3\xa4t \xe2\x84\xa2 [1";
    dm.setDevices({d});
    assert(dm.rows("\xe2\x84\xa2").size() == 1);
    assert(ProfileTreeModel::add(sm, Section::DEVICE, d.name, &err) != ProfileSet::npos);
    dm.setConfigured(ProfileTreeModel::patterns(sm, Section::DEVICE));
    assert(dm.rows("")[0].hasProfile);
}

int main() {
    testUtf8();
    testKeyList();
    testBindingParse();
    testPrimaryAdvanced();
    testBindingRowsAndApply();
    testTree();
    testPickerWiring();
    testLayout();
    std::puts("test_ui_profiles: OK");
    return 0;
}
