// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/ui/model/settings_model.hpp.
#include <cassert>
#include <cstdio>
#include <string>

#include "testutil.hpp"
#include "ui/model/settings_model.hpp"

using namespace spn;
using namespace spn::ui;

static ModeSet makeModes() {
    ModeSet ms;
    ModeDef n;
    n.id = "normal";
    n.label = "normal";
    n.builtin = true;
    ms.modes.push_back(n);
    ModeDef off;
    off.id = "off";
    off.label = "off";
    off.builtin = true;
    off.v = off.h = off.twist = {ChannelActionType::Off};
    ms.modes.push_back(off);
    ModeDef t;
    t.id = "timeline";
    t.label = "Timeline";
    t.builtin = true;
    t.v = {ChannelActionType::Off};
    t.h = {ChannelActionType::Keys, Key::Left, Key::Right};
    t.twist = {ChannelActionType::Keys, Key::Left, Key::Right};
    t.buttons.push_back({1, ButtonActionType::Key, Key::Space, ""});
    ms.modes.push_back(t);
    return ms;
}

static bool hasGroup(const SettingsModel::Visible &v, const std::string &id) {
    for (const auto &g : v.groups)
        if (g.id == id) return true;
    return false;
}
static bool has(const std::vector<std::string> &v, const std::string &k) {
    for (const auto &s : v)
        if (s == k) return true;
    return false;
}

int main() {
    IdentityStrings id;
    const std::string dir = tu::makeTempDir("dofscroll-ui");
    const std::string path = dir + "/dofscroll.conf";
    std::string err;
    int reloads = 0;
    bool reloadOk = true;

    SettingsModel m;
    m.setReloadCallback([&](std::string *e) {
        ++reloads;
        if (!reloadOk && e) *e = "reload failed";
        return reloadOk;
    });
    std::vector<std::string> warn;
    assert(m.load(path, &warn, &err) && warn.empty());  // missing file = defaults
    assert(m.sectionCount() == 1 && !m.dirty());

    // ---- sections ----
    size_t app = m.addSection(Section::APP, "firefox", &err);
    size_t dev = m.addSection(Section::DEVICE, "SpaceMouse Pro", &err);
    assert(app == 1 && dev == 2 && m.dirty());
    assert(m.addSection(Section::APP, "FireFox", &err) == app);  // case-insensitive find
    assert(m.addSection(Section::APP, "  ", &err) == ProfileSet::npos && !err.empty());
    assert(m.addSection(Section::APP, "a\x01" "b", &err) == ProfileSet::npos);
    assert(m.addSection(Section::GLOBAL, "", &err) == ProfileSet::npos);
    assert(m.sections()[1].title == "[app:firefox]" && m.sections()[0].title == "[global]");

    // ---- values: override vs inherited ----
    SettingsModel::Value v;
    assert(m.value(0, "SCROLL_V_SENS", &v) && v.text == "5" && !v.overridden && v.inherited == "5");
    assert(m.setValue(0, "scroll_v_sens", "7", &err) && m.value(0, "SCROLL_V_SENS", &v));
    assert(v.text == "7" && v.overridden);
    assert(m.value(app, "SCROLL_V_SENS", &v) && v.text == "7" && !v.overridden && v.inherited == "7");
    assert(m.setValue(app, "SCROLL_V_SENS", "9", &err) && m.value(app, "SCROLL_V_SENS", &v));
    assert(v.text == "9" && v.overridden && v.inherited == "7" && m.isOverridden(app, "SCROLL_V_SENS"));
    assert(m.effective(app).scroll_v.sensitivity == 9 && m.effective(0).scroll_v.sensitivity == 7);
    assert(m.clearOverride(app, "SCROLL_V_SENS") && !m.isOverridden(app, "SCROLL_V_SENS"));
    assert(m.effective(app).scroll_v.sensitivity == 7);
    // Channel options.
    assert(m.setValue(0, "SCROLL_V_MIX", "z + 0.5*rx", &err) && m.setValue(0, "SCROLL_V_CURVE", "0:0 50:0.1 200:1", &err));
    assert(m.setValue(0, "ZOOM_SIGN", "-1", &err) && m.setValue(0, "SCROLL_H_SMOOTH_MS", "20", &err));
    assert(m.setValue(0, "SCROLL_H_DEADZONE", "25", &err) && m.setValue(0, "ZOOM_LATERAL", "x z", &err));
    assert(m.effective(0).zoom.sign == -1 && m.effective(0).scroll_h.smooth_ms == 20);
    // Validation errors leave everything unchanged.
    assert(!m.setValue(0, "SCROLL_V_SENS", "abc", &err) && !err.empty());
    assert(!m.setValue(0, "SCROLL_V_MIX", "q + z", &err));
    assert(!m.setValue(0, "NOPE", "1", &err) && !m.setValue(0, "BUTTON_1", "click", &err));
    assert(!m.setValue(99, "SCROLL_V_SENS", "1", &err));
    assert(m.effective(0).scroll_v.sensitivity == 7);
    // Replacing keeps one entry.
    assert(m.setValue(0, "SCROLL_V_SENS", "8", &err));
    int n = 0;
    for (auto &kv : m.profiles().sections[0].kv) n += kv.first == "SCROLL_V_SENS";
    assert(n == 1);

    // ---- buttons ----
    assert(m.buttonLimit() == 32);
    m.setDeviceInfo("SpaceMouse Pro", 15);
    const int lim = m.buttonLimit();
    assert(lim >= 1 && lim <= 32);
    assert(m.setButton(0, 1, "key:space", &err) && m.setButton(0, 2, "nav", &err));
    assert(!m.setButton(0, 0, "click", &err) && !m.setButton(0, lim + 1, "click", &err));
    assert(!m.setButton(0, 1, "bogus", &err));
    auto rows = m.buttonRows(0);
    assert(int(rows.size()) == lim && rows[0].action == "key:space" && rows[0].overridden && rows[2].action == "none");
    assert(m.setButton(app, 1, "click", &err));
    rows = m.buttonRows(app);
    assert(rows[0].action == "click" && rows[0].overridden && rows[1].action == "nav" && !rows[1].overridden);
    assert(m.clearButton(app, 1) && m.buttonRows(app)[0].action == "key:space");
    // A binding beyond the limit (from a file) is kept and flagged.
    if (lim < 32) {
        ProfileSet ps = m.profiles();
        ps.sections[0].kv.emplace_back("BUTTON_32", "click");
        m.setProfiles(ps, path);
        rows = m.buttonRows(0);
        assert(int(rows.size()) == lim + 1 && rows.back().number == 32 && rows.back().beyondLimit);
        assert(m.clearButton(0, 32));
    }

    // ---- modes ----
    const ModeSet ms = makeModes();
    m.setModes(ms, dir + "/modes");
    assert(m.effectiveMode(0) == "normal" && m.effectiveMode(app) == "normal");
    // [global]: no MODE.
    assert(!m.setValue(0, "MODE", "timeline", &err) && err.find("[global]") != std::string::npos);
    assert(!m.setMode(0, "timeline", &err) && !m.setMode(0, 1, &err));
    assert(m.modeOptions(0).empty() && m.selectedModeOption(0) == -1);
    auto gv = m.visibleSettings(0);
    assert(!gv.canChooseMode && !hasGroup(gv, "mode") && gv.overrides.keys.empty() && gv.addableOverrides.empty());
    assert(hasGroup(gv, "buttons") && gv.isDefaultMode);
    // Channel groups are global and always available.
    auto cg = SettingsModel::globalChannelGroups();
    assert(cg.size() == 5 && cg[0].id == "scroll_v" && has(cg[0].keys, "SCROLL_V_CURVE") && has(cg[3].keys, "ZOOM_ENTER_MS"));
    assert(has(cg[4].keys, "TICK_HZ") && !has(cg[4].keys, "MODE"));

    // Dropdown.
    auto opt = m.modeOptions(app);
    assert(opt.size() == 4 && opt[0].id.empty() && opt[0].label == "Default mode" && opt[0].secondary.empty());
    assert(opt[1].id == "normal" && opt[2].id == "off" && opt[3].label == "Timeline" && opt[3].secondary == "timeline");
    assert(m.selectedModeOption(app) == 0);
    bool changed = false;
    assert(m.setMode(app, 3, &err, &changed) && changed);
    assert(m.selectedModeOption(app) == 3 && m.effectiveMode(app) == "timeline" && m.mode(app) == "timeline");
    assert(m.setMode(app, 3, &err, &changed) && !changed);  // same again
    assert(m.setMode(app, 0, &err, &changed) && changed);   // Default mode clears the key
    assert(m.selectedModeOption(app) == 0 && !m.isOverridden(app, "MODE") && m.effectiveMode(app) == "normal");
    assert(!m.setMode(app, 4, &err) && !m.setMode(app, -1, &err));
    assert(m.setMode(app, "off", &err, &changed) && changed && m.selectedModeOption(app) == 2);
    assert(!m.setMode(app, "Bad Id", &err));
    // Explicit "normal" is a real selection (index 1), distinct from "Default mode".
    assert(m.setMode(app, 1, &err) && m.selectedModeOption(app) == 1 && m.isOverridden(app, "MODE"));
    // Unknown mode id from a file stays selectable.
    assert(m.setMode(app, "gone", &err));
    opt = m.modeOptions(app);
    assert(opt.size() == 5 && !opt[4].known && m.selectedModeOption(app) == 4);
    assert(!m.visibleSettings(app).modeLoaded);

    // Visible settings per mode.
    assert(m.setMode(app, 3, &err));  // timeline: v off, h/twist keys
    auto tv = m.visibleSettings(app);
    assert(tv.modeId == "timeline" && !tv.isDefaultMode && tv.canChooseMode && hasGroup(tv, "mode"));
    assert(!hasGroup(tv, "buttons"));  // channel actions are not all default
    assert(tv.mode.h == "keys:left,right" && tv.mode.v == "none" && tv.mode.buttons.size() == 1);
    assert(tv.mode.buttons[0].second == "key:space" && !tv.mode.hasFile);  // timeline fixture has no file
    assert(!has(tv.addableOverrides, "SCROLL_V_SENS") && has(tv.addableOverrides, "SCROLL_H_SENS"));
    assert(!has(tv.addableOverrides, "ZOOM_LATERAL"));  // twist is not "default"
    assert(tv.overrides.keys.empty());
    assert(m.addOverride(app, "SCROLL_H_SENS", &err) && m.isOverridden(app, "SCROLL_H_SENS"));
    tv = m.visibleSettings(app);
    assert(has(tv.overrides.keys, "SCROLL_H_SENS") && !has(tv.addableOverrides, "SCROLL_H_SENS"));
    assert(m.clearOverride(app, "SCROLL_H_SENS") && m.visibleSettings(app).overrides.keys.empty());
    assert(!m.addOverride(0, "SCROLL_H_SENS", &err) && !m.addOverride(app, "NOPE", &err));
    assert(m.setMode(app, 2, &err));  // off: nothing relevant
    auto ov = m.visibleSettings(app);
    assert(ov.addableOverrides.empty());
    assert(m.setMode(app, 0, &err));
    auto nv = m.visibleSettings(app);
    assert(nv.isDefaultMode && hasGroup(nv, "buttons") && has(nv.addableOverrides, "ZOOM_LATERAL"));
    assert(has(nv.addableOverrides, "SCROLL_V_MIX"));

    // Edit mode file (built-in has none; a file mode does).
    RecordingActionRunner rr;
    assert(!m.editModeFile(app, rr, &err));
    ModeSet ms2 = ms;
    ms2.modes[2].builtin = false;
    m.setModes(ms2, dir + "/modes");
    assert(m.setMode(app, 3, &err));
    assert(m.visibleSettings(app).mode.hasFile);
    assert(m.editModeFile(app, rr, &err) && rr.calls.size() == 1 && rr.calls[0].path == dir + "/modes/timeline.conf");
    m.setModes(ms2, "");
    assert(!m.editModeFile(app, rr, &err));
    m.setModes(ms2, dir + "/modes");
    assert(m.setMode(app, 0, &err));

    // ---- validate, unknown modes, save, reload ----
    for (auto &e : m.validate(id)) fprintf(stderr, "%s\n", e.c_str());
    assert(m.validate(id).empty());
    assert(m.setMode(dev, "gone", &err));
    auto um = m.unknownModes(ms2);
    assert(um.size() == 1 && um[0] == "gone");
    assert(m.setMode(dev, 0, &err));
    assert(m.unknownModes(ms2).empty());
    {
        ProfileSet bad = m.profiles();
        bad.sections[0].kv.emplace_back("SCROLL_V_SENS", "oops");
        SettingsModel bm;
        bm.setProfiles(bad, dir + "/bad.conf");
        assert(!bm.validate(id).empty());
        auto r = bm.save(id);
        assert(!r.saved && !r.error.empty() && !tu::exists(dir + "/bad.conf"));
    }
    assert(m.dirty());
    auto r = m.save(id);
    assert(r.saved && r.reloaded && reloads == 1 && !m.dirty() && tu::exists(path));
    reloadOk = false;
    r = m.save(id);
    assert(r.saved && !r.reloaded && r.error == "reload failed" && reloads == 2);
    reloadOk = true;
    {
        SettingsModel back;
        warn.clear();
        bool okl = back.load(path, &warn, &err);
        for (auto &w : warn) fprintf(stderr, "W: %s\n", w.c_str());
        assert(okl && warn.empty());
        assert(back.sectionCount() == 3 && back.effective(0).scroll_v.sensitivity == 8);
        assert(back.effective(0).buttons.at(1) == "key:space" && back.effective(0).zoom.sign == -1);
        assert(back.sections()[2].kind == Section::DEVICE);
        // Reading never needs a reload callback.
        assert(back.save(id).reloaded);
    }
    // Remove a section.
    assert(m.removeSection(app, &err) && m.sectionCount() == 2 && m.dirty());
    assert(!m.removeSection(0, &err) && !m.removeSection(9, &err));

    // ---- legacy MODE in [global] ----
    {
        const std::string p2 = dir + "/legacy.conf";
        assert(fs::writeFileReplace(p2, "MODE = timeline\nSCROLL_V_SENS = 4\n", &err));
        SettingsModel lm;
        warn.clear();
        assert(lm.load(p2, &warn, &err));
        assert(warn.size() == 1 && warn[0].find("move it") != std::string::npos);
        assert(lm.notices(id).size() == 1 && lm.validate(id).empty());
        assert(lm.isOverridden(0, "MODE"));  // kept
        assert(lm.save(id).saved);
        SettingsModel again;
        warn.clear();
        again.load(p2, &warn, &err);
        assert(warn.size() == 1);  // still kept in the file
        assert(again.clearOverride(0, "MODE") && again.notices(id).empty());
        assert(again.save(id).saved);
    }

    tu::removeAll(dir);
    puts("test_ui_settings: ok");
    return 0;
}
