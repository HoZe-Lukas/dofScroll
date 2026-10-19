// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for the models of the Channels and General pages (ui_settings, general_page_model,
// channels_page_model).
#include <cassert>
#include <cstdio>
#include <string>

#include "testutil.hpp"
#include "ui/model/channels_page_model.hpp"
#include "ui/model/general_page_model.hpp"
#include "ui/model/ui_settings.hpp"

using namespace spn;
using namespace spn::ui;

int main() {
    const IdentityStrings id;

    // ---- UiSettings ----
    assert(languageFromId("en") == Language::English && languageFromId("de") == Language::German);
    assert(languageFromId("xx") == Language::System && languageFromId("") == Language::System);
    assert(parseUiSettings("").language == Language::System);
    assert(parseUiSettings("# c\n  language =  de \r\nfoo=1\n").language == Language::German);
    assert(parseUiSettings("language=en\nlanguage=de\n").language == Language::German);  // last wins
    UiSettings en;
    en.language = Language::English;
    assert(parseUiSettings(serializeUiSettings(en)).language == Language::English);
    assert(languageLocaleName(Language::System).empty() && languageLocaleName(Language::German) == "de");

    const std::string root = tu::makeTempDir("ui_pages");
    const std::string base = tu::useConfigBase(root);
    const std::string path = uiSettingsPath();
    assert(path == base + "/dofscroll/ui.conf");
    assert(loadUiSettings(path).language == Language::System);  // missing file
    std::string err;
    UiSettings de;
    de.language = Language::German;
    assert(saveUiSettings(path, de, &err));
    assert(loadUiSettings(path).language == Language::German);
    assert(saveUiSettings(path, en, &err));
    assert(loadUiSettings(path).language == Language::English);
#ifndef _WIN32
    struct stat st;
    assert(::stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
#endif

    // ---- autostart view ----
    using autostart::AutostartState;
    AutostartView v = autostartView(AutostartState::Enabled, "", id);
    assert(v.checked && v.canToggle && !v.partial && !v.isError && v.text.empty() && !v.tooltip.empty());
    v = autostartView(AutostartState::Disabled, "", id);
    assert(!v.checked && v.canToggle && !v.isError);
    v = autostartView(AutostartState::Partial, "dofscroll-ui is off", id);
    assert(v.partial && v.canToggle && v.text.find("dofscroll-ui is off") != std::string::npos);
    v = autostartView(AutostartState::Unavailable, "dofscroll is not installed", id);
    assert(!v.canToggle && v.isError && v.text.find("not installed") != std::string::npos);
    v = autostartView(AutostartState::Error, std::string("bad\nline") + std::string(500, 'x'), id);
    assert(!v.canToggle && v.isError && v.text.find('\n') == std::string::npos && v.text.size() < 300);

    // ---- status block ----
    StatusModel sm;
    assert(statusBlockLines(sm, id).size() == 1 && statusBlockLines(sm, id)[0] == "Daemon: not running");
    sm.update("profile=global\ndevice=Pad\nmode=normal\nenabled=1\npaused=0\nupdated=100\n", true, 101);
    const auto lines = statusBlockLines(sm, id);
    assert(lines.size() == 3 && lines[0] == "Daemon: running");
    assert(lines[1].find("Profile: global") != std::string::npos && lines[2] == "Device: Pad");

    // ---- channels page ----
    const auto groups = channelPageGroups();
    assert(groups.size() == 4 && groups[0].id == "scroll_v" && groups[1].id == "scroll_h");
    assert(groups[2].id == "zoom" && groups[2].keys.size() == 9 && groups[3].id == "daemon");
    for (const auto &g : groups)
        for (const auto &k : g.keys) assert(k != "ENABLED" && k != "MODE");

    SettingsModel m;
    m.setProfiles(ProfileSet(), root + "/dofscroll.conf");
    assert(!resetGroup(m, groups[0]));  // nothing to reset
    assert(m.setValue(0, "SCROLL_V_SENS", "9", &err) && m.setValue(0, "SCROLL_H_SENS", "8", &err));
    assert(m.setValue(0, "ZOOM_ENTER_MS", "100", &err) && m.setValue(0, "TICK_HZ", "120", &err));
    assert(!m.setValue(0, "SCROLL_V_DEADZONE", "abc", &err) && !err.empty());
    assert(resetGroup(m, groups[0]) && !m.isOverridden(0, "SCROLL_V_SENS"));
    assert(m.isOverridden(0, "SCROLL_H_SENS"));  // other group untouched
    assert(resetGroup(m, groups[2]) && !m.isOverridden(0, "ZOOM_ENTER_MS"));
    assert(restoreAllDefaults(m) && !m.isOverridden(0, "SCROLL_H_SENS") && !m.isOverridden(0, "TICK_HZ"));
    assert(!restoreAllDefaults(m));

    // save writes the file and calls the reload callback
    int reloads = 0;
    m.setReloadCallback([&](std::string *) { ++reloads; return true; });
    assert(m.setValue(0, "SCROLL_V_SENS", "7", &err));
    const SettingsModel::SaveResult r = m.save(id);
    assert(r.saved && r.reloaded && reloads == 1);

    // lateral axes
    Settings s;
    s.zoom_lateral = {AX_X, AX_RZ};
    assert(lateralMask(s) == ((1u << AX_X) | (1u << AX_RZ)));
    assert(lateralText(lateralMask(s)) == "x rz" && lateralText(0).empty());
    assert(m.setValue(0, "ZOOM_LATERAL", lateralText((1u << AX_Y) | (1u << AX_RX)), &err));
    assert(lateralMask(m.effective(0)) == ((1u << AX_Y) | (1u << AX_RX)));

    tu::removeAll(root);
    puts("test_ui_pages: ok");
    return 0;
}
