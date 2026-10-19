// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure parts of the OS autostart implementations (no OS calls, unit-tested on Linux in
// tests/test_autostart_os.cpp): state combination, Windows Run-key value quoting, LaunchAgent
// plist rendering with XML escaping, launchctl and systemctl argument vectors. Strings are UTF-8.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "../autostart.hpp"
#include "autostart_platform.hpp"

namespace spn {
namespace autostart {

// ---- state combination (all three systems) ----------------------------------------------------

struct Entry {
    enum Kind {
        Present,    // exists and does what we would write
        Absent,     // not set up
        Different,  // exists but points elsewhere / has other content
        Missing,    // the program or unit is not installed
        Blocked,    // exists but cannot be toggled (masked, static, enabled until reboot)
        Failed      // reading failed
    };
    std::string name;       // for messages: "dofscroll", "dofscroll-ui", unit or label
    Kind kind = Absent;
    bool optional = false;  // the UI: Missing is then ignored
    std::string why;        // OS message or reason for Blocked / Failed
};

inline AutostartState combineEntries(const std::vector<Entry> &es, std::string *detail) {
    auto say = [&](const std::string &s) { if (detail) *detail = s; };
    say("");
    size_t present = 0, absent = 0, considered = 0;
    bool different = false;
    std::string diffName;
    for (const Entry &e : es) {
        if (e.kind == Entry::Missing && e.optional) continue;
        considered++;
        switch (e.kind) {
            case Entry::Missing: say(e.name + " is not installed"); return AutostartState::Unavailable;
            case Entry::Blocked: say(e.name + ": " + (e.why.empty() ? "cannot be changed" : e.why));
                                 return AutostartState::Unavailable;
            case Entry::Failed:  say(e.name + ": " + (e.why.empty() ? "cannot be read" : e.why));
                                 return AutostartState::Error;
            case Entry::Present: present++; break;
            case Entry::Absent: absent++; break;
            case Entry::Different: if (!different) diffName = e.name; different = true; break;
        }
    }
    if (considered == 0) { say("nothing to manage"); return AutostartState::Unavailable; }
    if (present == considered) return AutostartState::Enabled;
    if (absent == considered) return AutostartState::Disabled;
    if (different) say(diffName + " points to a different program");
    else say("only some entries are enabled");
    return AutostartState::Partial;
}

struct AutostartUi {
    bool checked;
    bool can_toggle;
};

// Checkbox of the UI: checked only if fully enabled; Partial can be toggled (it enables both).
inline AutostartUi autostartUi(AutostartState s) {
    switch (s) {
        case AutostartState::Enabled: return {true, true};
        case AutostartState::Disabled:
        case AutostartState::Partial: return {false, true};
        default: return {false, false};
    }
}

// ---- Windows: Run-key value --------------------------------------------------------------------

constexpr const char *kWinDaemonValue = "dofscroll";
constexpr const char *kWinUiValue = "dofscroll-ui";
constexpr const char *kWinDaemonExe = "dofscroll.exe";
constexpr const char *kWinUiExe = "dofscroll-ui.exe";

// The value is the full path in double quotes (spaces are safe, no arguments). Refused: empty,
// not absolute (drive letter or UNC), a double quote, control characters. false + *err if refused.
inline bool quoteRunValue(const std::string &path, std::string *out, std::string *err = nullptr) {
    auto fail = [&](const char *m) { if (err) *err = m; return false; };
    if (path.empty()) return fail("empty path");
    const bool drive = path.size() > 3 &&
                       ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
                       path[1] == ':' && (path[2] == '\\' || path[2] == '/');
    const bool unc = path.size() > 2 && path[0] == '\\' && path[1] == '\\';
    if (!drive && !unc) return fail("path is not absolute");
    for (unsigned char c : path)
        if (c < 0x20 || c == 0x7f || c == '"') return fail("unsupported character in path");
    if (out) *out = "\"" + path + "\"";
    return true;
}

// Run value -> program path, for comparing ("" if the value is not exactly one quoted path).
inline std::string unquoteRunValue(const std::string &v) {
    if (v.size() < 3 || v.front() != '"' || v.back() != '"') return "";
    const std::string p = v.substr(1, v.size() - 2);
    if (p.find('"') != std::string::npos) return "";
    return p;
}

// ---- macOS: LaunchAgent -----------------------------------------------------------------------

constexpr const char *kMacDaemonLabel = "io.github.dofscroll.daemon";
constexpr const char *kMacUiLabel = "io.github.dofscroll.ui";

inline std::string xmlEscape(const std::string &s) {
    std::string r;
    for (char c : s) {
        switch (c) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            case '\'': r += "&apos;"; break;
            default: r += c;
        }
    }
    return r;
}

// false for paths that cannot go into a plist: empty, relative, control characters (XML 1.0
// cannot hold most of them even escaped).
inline bool plistPathOk(const std::string &p) {
    if (p.empty() || p[0] != '/') return false;
    for (unsigned char c : p) if (c < 0x20 || c == 0x7f) return false;
    return true;
}

// daemon: restart after a crash only (same keys as packaging/macos/*.plist.in); UI: started once
// at login, no KeepAlive (quitting the UI must stay quit).
inline std::string renderLaunchAgentPlist(const std::string &label, const std::string &program,
                                          bool daemon) {
    std::string s =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
        "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "    <key>Label</key>\n    <string>" + xmlEscape(label) + "</string>\n"
        "    <key>ProgramArguments</key>\n    <array>\n        <string>" + xmlEscape(program) +
        "</string>\n    </array>\n"
        "    <key>RunAtLoad</key>\n    <true/>\n";
    if (daemon)
        s += "    <key>KeepAlive</key>\n    <dict>\n        <key>SuccessfulExit</key>\n"
             "        <false/>\n    </dict>\n    <key>ThrottleInterval</key>\n    <integer>10</integer>\n";
    s += "    <key>ProcessType</key>\n    <string>Interactive</string>\n"
         "    <key>LimitLoadToSessionType</key>\n    <string>Aqua</string>\n";
    if (daemon)
        s += "    <key>StandardOutPath</key>\n    <string>/dev/null</string>\n"
             "    <key>StandardErrorPath</key>\n    <string>/dev/null</string>\n";
    s += "</dict>\n</plist>\n";
    return s;
}

inline std::string launchctlDomain(unsigned uid) { return "gui/" + std::to_string(uid); }
inline std::vector<std::string> launchctlBootstrapArgv(unsigned uid, const std::string &plist) {
    return {"launchctl", "bootstrap", launchctlDomain(uid), plist};
}
inline std::vector<std::string> launchctlBootoutArgv(unsigned uid, const std::string &label) {
    return {"launchctl", "bootout", launchctlDomain(uid) + "/" + label};
}
inline std::vector<std::string> launchctlPrintArgv(unsigned uid, const std::string &label) {
    return {"launchctl", "print", launchctlDomain(uid) + "/" + label};
}

// bootout is asynchronous: waits until `loaded()` turns false (install script: 50 x 0.2 s).
template <class Loaded, class Sleep>
bool waitUntilGone(Loaded loaded, Sleep sleepStep, int maxTries = 50) {
    for (int i = 0; i <= maxTries; i++) {
        if (!loaded()) return true;
        if (i < maxTries) sleepStep();
    }
    return false;
}

// ---- Linux: systemd ---------------------------------------------------------------------------

constexpr const char *kLinuxDaemonUnit = "dofscroll.service";
constexpr const char *kLinuxUiUnit = "dofscroll-ui.service";

// Never --now: only the next login is affected.
inline std::vector<std::string> systemctlArgv(const char *verb, const std::vector<std::string> &units) {
    std::vector<std::string> a = {"systemctl", "--user", verb};
    for (const auto &u : units) a.push_back(u);
    return a;
}

// One `systemctl --user is-enabled <unit>` answer -> Entry (parser from autostart.hpp).
inline Entry entryFromIsEnabled(const std::string &unit, const std::string &out,
                                const std::string &err, bool optional) {
    Entry e;
    e.name = unit;
    e.optional = optional;
    switch (parseIsEnabled(out, err, 1)[0]) {
        case Unit::Enabled: e.kind = Entry::Present; break;
        case Unit::Disabled: e.kind = Entry::Absent; break;
        case Unit::EnabledRuntime: e.kind = Entry::Blocked; e.why = "enabled only until reboot"; break;
        case Unit::Static: e.kind = Entry::Blocked; e.why = "has no [Install] section to toggle"; break;
        case Unit::Masked: e.kind = Entry::Blocked; e.why = "masked"; break;
        case Unit::NotFound: e.kind = Entry::Missing; break;
        default: e.kind = Entry::Failed; e.why = "unexpected systemctl answer"; break;
    }
    return e;
}

}  // namespace autostart
}  // namespace spn
