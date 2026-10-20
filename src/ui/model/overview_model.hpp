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
// not show: every profile of the configuration has a card, in file order.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "mode_list.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/strings.hpp"
#include "ui/model/tray_menu_model.hpp"

namespace spn::ui {

constexpr size_t kMaxSummaryItems = 3;
constexpr size_t kMaxSummaryValueBytes = 40;

struct OverviewProfile {
    size_t section = 0;                  // index in the settings model (what the Profiles page selects)
    Section::Kind kind = Section::APP;   // DEVICE or APP
    std::string name;                    // device or program pattern
    std::string mode;                    // MODE of the profile; "" = it uses the default mode
    std::vector<std::string> overrides;  // "KEY = value" of every setting it sets, except MODE, in file order
    std::string summary;                 // the one line for the card (translated)
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
    if (!p.mode.empty()) parts.push_back(format(s.tr("Mode: %1"), {clipValue(p.mode)}));
    for (size_t i = 0; i < p.overrides.size() && i < kMaxSummaryItems; ++i) parts.push_back(clipValue(p.overrides[i]));
    if (p.overrides.size() > kMaxSummaryItems)
        parts.push_back(format(s.tr("+%1 more"), {std::to_string(p.overrides.size() - kMaxSummaryItems)}));
    if (parts.empty()) return s.tr("Uses the default settings");
    std::string out;
    for (const std::string &part : parts) out += (out.empty() ? "" : ", ") + part;
    return out;
}

inline std::vector<OverviewAction> overviewActions() {
    return {{"release_keys", "Release all keys", TrayAction::ReleaseAllKeys},
            {"restart_daemon", "Restart daemon", TrayAction::RestartDaemon},
            {"restart_interface", "Restart interface", TrayAction::RestartInterface}};
}

inline OverviewData buildOverview(const SettingsModel &settings, const StatusModel &status, const StringProvider &s) {
    OverviewData d;
    d.daemonRunning = status.daemonRunning();
    d.statusLine = status.mainLine(s);
    d.infoLine = status.infoLine(s);
    d.warningLine = status.warningLine(s);
    d.defaultMode = modelist::globalModeId(settings.profiles());  // as the tray: MODE of [global]
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
        p.summary = profileSummary(p, s);
        (sec.kind == Section::DEVICE ? d.deviceProfiles : d.appProfiles)++;
        d.profiles.push_back(std::move(p));
    }
    return d;
}

// First-run guidance: what a new user does first. Shown on the Overview until the device is seen and
// the first profile exists; every step says in words whether it is done (not by colour alone) and
// names the page that does it.
struct GettingStartedStep {
    std::string id;    // "device", "axes", "profile"
    std::string text;  // translated
    bool done = false;
    std::string page;  // page id that does it; "" = nothing to open
};

inline std::vector<GettingStartedStep> gettingStartedSteps(const OverviewData &d, const StatusModel &status,
                                                           const StringProvider &s) {
    const bool deviceSeen = status.daemonRunning() && (status.status().devices_seen > 0 || !status.status().device.empty());
    std::vector<GettingStartedStep> out;
    out.push_back({"device", s.tr("Connect your SpaceMouse. The daemon has to see it."), deviceSeen, ""});
    out.push_back({"axes", s.tr("Check on the Axes page that the axes move as you expect (the assistant helps)."), false, "axes"});
    out.push_back({"profile", s.tr("Add a profile for a program you use."), !d.profiles.empty(), "profiles"});
    return out;
}

// The guidance goes away by itself once the device is seen and a profile exists.
inline bool showGettingStarted(const std::vector<GettingStartedStep> &steps) {
    for (const GettingStartedStep &st : steps)
        if ((st.id == "device" || st.id == "profile") && !st.done) return true;
    return false;
}

}  // namespace spn::ui
