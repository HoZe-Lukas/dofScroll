// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Data of the Overview page, without any toolkit: the state of the daemon, what is active right
// now, one card per device or program profile (name, mode and a one-line summary of what the
// profile overrides), and the quick actions. The view only draws it.
//
//   const OverviewData d = buildOverview(settings, status, strings);
//
// The summary of a profile lists the mode first, then up to kMaxSummaryItems of the settings the
// profile sets itself ("SCROLL_V_SENS = 9"), then "+N more". A profile without a mode and without
// any setting says that it uses the default settings. Nothing is configured that this page does
// not show: every profile of the configuration has a card, in file order. Button overrides are
// settings like the others ("BUTTON_1 = key:space"); the tooltip of a card lists all of them.
//
// Quick actions: the buttons (overviewActions), the task entries that open a page
// (overviewTasks), the mode switch (modeChoices, requestActiveMode: the same runtime request as the
// tray's mode menu) and the filter of the profile list (filterOverviewProfiles).
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "mode_list.hpp"
#include "mode_request.hpp"
#include "modes.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/strings.hpp"
#include "ui/model/tray_menu_model.hpp"
#include "ui/model/ui_filter.hpp"

namespace spn::ui {

constexpr size_t kMaxSummaryItems = 3;
constexpr size_t kMaxSummaryValueBytes = 40;

struct OverviewProfile {
    size_t section = 0;                  // index in the settings model (what the Profiles page selects)
    Section::Kind kind = Section::APP;   // DEVICE or APP
    std::string name;                    // device or program pattern
    std::string mode;                    // MODE of the profile; "" = it uses the default mode
    std::vector<std::string> overrides;  // "KEY = value" of every setting it sets, except MODE, in file order
    size_t overrideCount = 0;            // settings the profile sets itself (MODE and buttons included)
    std::string overrideText;            // "Overrides 3 settings" / "Overrides nothing: everything is inherited" (translated)
    std::string summary;                 // the one line for the card (translated)
    std::string details;                 // every setting on its own line (tooltip of the card, translated)
};

struct OverviewAction {
    std::string id;     // stable identifier
    std::string label;  // English source string (the view translates it)
    TrayAction action;  // what to run, same as the tray
};

struct OverviewData {
    bool daemonRunning = false;
    std::string statusLine;    // "Profile: global  |  Mode: normal", or that the daemon does not run
    std::string infoLine;      // "Program: x  |  Device: y"
    std::string warningLine;   // several devices connected
    std::string defaultMode;   // MODE of [global]
    std::string activeMode;    // mode the daemon runs now (status), else the default mode
    std::vector<modelist::MenuEntry> modeChoices;  // loaded modes for the mode switch; activeMode is marked
    std::vector<OverviewProfile> profiles;
    size_t deviceProfiles = 0, appProfiles = 0;
};

// At most `max` bytes, never cut inside a UTF-8 sequence, "..." appended when cut.
inline std::string clipValue(const std::string &v, size_t max = kMaxSummaryValueBytes) {
    if (v.size() <= max) return v;
    size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(v[n]) & 0xC0) == 0x80) --n;  // v[n] starts a character
    return v.substr(0, n) + "...";
}

inline std::string profileSummary(const OverviewProfile &p, const StringProvider &s) {
    std::vector<std::string> parts;
    if (!p.mode.empty()) parts.push_back(format(s.tr("Joystick mode: %1"), {clipValue(p.mode)}));
    for (size_t i = 0; i < p.overrides.size() && i < kMaxSummaryItems; ++i) parts.push_back(clipValue(p.overrides[i]));
    if (p.overrides.size() > kMaxSummaryItems)
        parts.push_back(format(s.tr("+%1 more"), {std::to_string(p.overrides.size() - kMaxSummaryItems)}));
    if (parts.empty()) return s.tr("Uses the default settings");
    std::string out;
    for (const std::string &part : parts) out += (out.empty() ? "" : ", ") + part;
    return out;
}

inline std::string profileDetails(const OverviewProfile &p, const StringProvider &s) {
    std::string out;
    if (!p.mode.empty()) out += format(s.tr("Joystick mode: %1"), {p.mode});
    for (const std::string &o : p.overrides) out += (out.empty() ? "" : "\n") + o;
    return out.empty() ? s.tr("Uses the default settings") : out;
}

// Entries that open a page for a task (the view shows them as buttons and emits openPage).
struct OverviewTask {
    std::string id;     // stable identifier
    std::string label;  // English source string
    std::string tip;    // English source string: what the page offers
    std::string page;   // page id (page_registry.cpp)
};

inline std::vector<OverviewTask> overviewTasks() {
    return {{"tune", "Tune scroll and zoom", "Sensitivity, deadzone, smoothing and curve of the channels (the Global profile, Channels tab)", "profile-channels"},
            {"axes", "Check the axes", "Move the cap and see which axis reacts; the assistant sets the axis mixes", "axes"},
            {"modes", "Switch joystick modes", "Joystick modes change what the axes and buttons do", "modes"}};
}

inline std::vector<OverviewAction> overviewActions() {
    return {{"open_log", "Open log", TrayAction::OpenLog},
            {"release_keys", "Release all keys", TrayAction::ReleaseAllKeys},
            {"restart_daemon", "Restart daemon", TrayAction::RestartDaemon},
            {"restart_interface", "Restart interface", TrayAction::RestartInterface}};
}

// Switches the mode of the running daemon (not the configuration): the runtime request of the
// tray's mode menu, then a reload so that the daemon reads it. false + *err on failure.
inline bool requestActiveMode(const std::string &runtimeDir, ActionRunner &runner, const std::string &id,
                              std::string *err) {
    return writeModeRequest(runtimeDir, id, err) && runner.requestReload(err);
}

// `modes`: the loaded modes (ModeCatalog::modeSet()); without it there is nothing to switch to.
inline OverviewData buildOverview(const SettingsModel &settings, const StatusModel &status, const StringProvider &s,
                                  const ModeSet *modes = nullptr) {
    OverviewData d;
    d.daemonRunning = status.daemonRunning();
    d.statusLine = status.mainLine(s);
    d.infoLine = status.infoLine(s);
    d.warningLine = status.warningLine(s);
    d.defaultMode = modelist::globalModeId(settings.profiles());  // as the tray: MODE of [global]
    d.activeMode = status.daemonRunning() && validModeId(status.status().mode) ? status.status().mode : d.defaultMode;
    if (modes) d.modeChoices = modelist::buildMenuEntries(*modes, d.activeMode);
    const std::vector<Section> &sections = settings.profiles().sections;
    for (size_t i = 0; i < sections.size(); ++i) {
        const Section &sec = sections[i];
        if (sec.kind == Section::GLOBAL) continue;
        OverviewProfile p;
        p.section = i;
        p.kind = sec.kind;
        p.name = sec.pattern;
        for (const auto &kv : sec.kv) {
            if (kv.first == "MODE") p.mode = kv.second;
            else p.overrides.push_back(kv.first + " = " + kv.second);
        }
        p.overrideCount = sec.kv.size();
        p.overrideText = profileOverrideCountText(p.overrideCount, s);
        p.summary = profileSummary(p, s);
        p.details = profileDetails(p, s);
        (sec.kind == Section::DEVICE ? d.deviceProfiles : d.appProfiles)++;
        d.profiles.push_back(std::move(p));
    }
    return d;
}

// Indices (into d.profiles) of the cards that match the filter box: every word of the filter must
// occur in the name, the mode, a setting or the kind ("device" / "program").
inline std::vector<size_t> filterOverviewProfiles(const OverviewData &d, const std::string &filter) {
    std::vector<size_t> out;
    for (size_t i = 0; i < d.profiles.size(); ++i) {
        const OverviewProfile &p = d.profiles[i];
        std::vector<std::string> fields = {p.name, p.mode, p.kind == Section::DEVICE ? "device" : "program"};
        fields.insert(fields.end(), p.overrides.begin(), p.overrides.end());
        if (matchesFilter(filter, fields)) out.push_back(i);
    }
    return out;
}

// First-run guidance: what a new user does first. Shown on the Overview until the device is seen and
// the first profile exists; every step says in words whether it is done (not by colour alone) and
// names the page that does it.
struct GettingStartedStep {
    std::string id;    // "device", "axes", "profile"
    std::string text;  // translated
    bool done = false;
    std::string page;  // page id that does it; "" = nothing to open
    std::string hint;  // what to do when it is not done and there is a known reason (translated); "" = none
};

// axisAssistantApplied: the axis assistant wrote its result once (ui_progress.hpp).
inline std::vector<GettingStartedStep> gettingStartedSteps(const OverviewData &d, const StatusModel &status,
                                                           const StringProvider &s, bool axisAssistantApplied = false) {
    const bool deviceSeen = status.daemonRunning() && (status.status().devices_seen > 0 || !status.status().device.empty());
    std::vector<GettingStartedStep> out;
    // The Axes page shows what the daemon sees of the device; it is where the user checks the connection.
    GettingStartedStep device{"device", s.tr("Connect your SpaceMouse. The daemon has to see it."), deviceSeen, "axes", ""};
    if (!deviceSeen && !status.daemonRunning())
        device.hint = s.tr("The daemon is not running. Start dofscroll (or use Restart daemon) and see Troubleshooting.");
    out.push_back(device);
    out.push_back({"axes", s.tr("Check on the Axes page that the axes move as you expect (the assistant helps)."),
                   axisAssistantApplied, "axes", ""});
    out.push_back({"profile", s.tr("Add a profile for a program you use."), !d.profiles.empty(), "profiles", ""});
    return out;
}

// The guidance goes away by itself once the device is seen and a profile exists.
inline bool showGettingStarted(const std::vector<GettingStartedStep> &steps) {
    for (const GettingStartedStep &st : steps)
        if ((st.id == "device" || st.id == "profile") && !st.done) return true;
    return false;
}

}  // namespace spn::ui
