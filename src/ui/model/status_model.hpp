// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// StatusModel: parses the daemon's status file (key=value lines, see src/common.hpp) and derives
// staleness and display strings. Pure logic: the view reads the file (bounded, kStatusMaxBytes)
// and calls update() about once per second.
//
//   StatusModel m;
//   m.update(fileContent, fileReadable, nowUnixSeconds);
//   m.state();                 // NoStatus / Stale / Running / Paused / Disabled
//   m.mainLine(strings);       // "Profile: global (paused)  |  Mode: normal"
//   m.infoLine(strings);       // "Program: firefox  |  Device: SpaceMouse Pro"
//   m.warningLine(strings);    // device cannot be read (input_issue), several devices connected, or ""
// Texts from the file are untrusted: control characters are replaced and long values truncated.
#pragma once
#include <cstdint>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "log_sanitize.hpp"
#include "platform/input_diag.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct DaemonStatus {
    std::string profile, device, app, mode;
    std::string input;   // "driver", "hid" or empty (Linux, older daemon)
    std::string toggle;  // what button toggles hold: "nav", "mode:<id>", both separated by a space, or empty
    bool enabled = true, paused = false;
    int devices_seen = 0;
    // Several devices, one lane each (docs/multi-device.md): the daemon writes lanes=N, active_device=<the one
    // that moved last> and device<n>/profile<n>/mode<n>/toggle<n>/enabled<n> for the lanes 2..N. The plain keys
    // above describe the first lane. Absent with one device.
    int lanes = 1;
    std::string active_device;
    struct Lane {
        std::string device, profile, mode, toggle;
        bool enabled = true;
    };
    std::vector<Lane> more;  // lane 2, 3, ...
    InputIssue input_issue = InputIssue::Clear;  // device found but cannot be read / sends nothing
    // macOS: "permissions=ok" or "permissions=missing:accessibility,input_monitoring" (mac_paths.hpp).
    bool permissions_known = false;
    bool missing_accessibility = false, missing_input_monitoring = false;
    bool has_updated = false;
    int64_t updated = 0;  // unix seconds
};

// "device2", "mode3", ...: stores the value of a lane key (n >= 2); false if k is not one.
inline bool laneKeyValue(const std::string &k, const std::string &v, DaemonStatus *d) {
    static const char *const names[] = {"device", "profile", "mode", "toggle", "enabled"};
    for (const char *name : names) {
        const size_t n = std::char_traits<char>::length(name);
        if (k.compare(0, n, name) != 0 || k.size() == n || k.size() > n + 1) continue;
        const int lane = k[n] - '0';  // lanes 2..8 have one digit
        if (lane < 2 || lane > 8) continue;
        if (d->more.size() < static_cast<size_t>(lane - 1)) d->more.resize(static_cast<size_t>(lane - 1));
        DaemonStatus::Lane &l = d->more[static_cast<size_t>(lane - 2)];
        if (!std::strcmp(name, "device")) l.device = v;
        else if (!std::strcmp(name, "profile")) l.profile = v;
        else if (!std::strcmp(name, "mode")) l.mode = v;
        else if (!std::strcmp(name, "toggle")) l.toggle = v;
        else l.enabled = v != "0";
        return true;
    }
    return false;
}

// Parses the file content; unknown keys and lines without '=' are ignored, the last value of a
// key wins. Never fails (missing keys keep their defaults, has_updated tells about "updated").
inline DaemonStatus parseStatus(const std::string &text) {
    DaemonStatus d;
    size_t pos = 0;
    while (pos < text.size()) {
        const size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "profile") d.profile = v;
        else if (k == "device") d.device = v;
        else if (k == "app") d.app = v;
        else if (k == "mode") d.mode = v;
        else if (k == "input") d.input = v;
        else if (k == "toggle") d.toggle = v;
        else if (k == "enabled") d.enabled = v != "0";
        else if (k == "paused") d.paused = v == "1";
        else if (k == "devices_seen") d.devices_seen = int(std::strtol(v.c_str(), nullptr, 10));
        else if (k == "lanes") d.lanes = std::max(1, std::min(int(std::strtol(v.c_str(), nullptr, 10)), 8));
        else if (k == "active_device") d.active_device = v;
        else if (laneKeyValue(k, v, &d)) {}
        else if (k == "input_issue") d.input_issue = inputIssueFromKey(v);
        else if (k == "permissions") {
            d.permissions_known = true;
            d.missing_accessibility = v.find("accessibility") != std::string::npos;
            d.missing_input_monitoring = v.find("input_monitoring") != std::string::npos;
        }
        else if (k == "updated") {
            d.updated = std::strtoll(v.c_str(), nullptr, 10);
            d.has_updated = true;
        }
    }
    return d;
}

class StatusModel {
public:
    enum class State { NoStatus, Stale, Running, Paused, Disabled };
    // Age (seconds) beyond which the daemon counts as not running; the same tolerance applies to
    // timestamps in the future (clock jumps).
    static constexpr int64_t kMaxAgeSeconds = 5;

    // fileReadable false: the file is missing or was refused (owner/permissions) -> NoStatus.
    void update(const std::string &content, bool fileReadable, int64_t nowSeconds) {
        status_ = DaemonStatus();
        if (!fileReadable) {
            state_ = State::NoStatus;
            return;
        }
        status_ = parseStatus(content);
        const int64_t age = nowSeconds - status_.updated;
        if (!status_.has_updated || age > kMaxAgeSeconds || age < -kMaxAgeSeconds) state_ = State::Stale;
        else if (status_.paused) state_ = State::Paused;
        else if (!status_.enabled) state_ = State::Disabled;
        else state_ = State::Running;
    }

    State state() const { return state_; }
    // The daemon delivers current data (Running, Paused or Disabled).
    bool daemonRunning() const {
        return state_ == State::Running || state_ == State::Paused || state_ == State::Disabled;
    }
    const DaemonStatus &status() const { return status_; }
    // Mode id reported by the daemon ("" if none, e.g. an older daemon).
    const std::string &modeId() const { return status_.mode; }

    // modeLabel: display name of the mode (the view/TrayMenuModel looks it up); default = the id.
    std::string mainLine(const StringProvider &s, const std::string &modeLabel = std::string()) const {
        if (!daemonRunning()) return s.tr("Daemon not running / no status data");
        std::string t = format(s.tr("Profile: %1"), {clean(status_.profile.empty() ? "global" : status_.profile, 80)});
        if (status_.paused) t += s.tr(" (paused)");
        if (!status_.enabled) t += s.tr(" (disabled)");
        const std::string &mode = modeLabel.empty() ? status_.mode : modeLabel;
        if (!mode.empty()) t += "  |  " + format(s.tr("Joystick mode: %1"), {clean(mode, 40)});
        // "nav" is a whole word of the space separated list (not a part of a mode id)
        if ((" " + status_.toggle + " ").find(" nav ") != std::string::npos) t += "  |  " + s.tr("Navigation on");
        return t;
    }
    std::string infoLine(const StringProvider &s) const {
        if (!daemonRunning()) return "";
        std::string t;
        if (!status_.app.empty()) t = format(s.tr("Program: %1"), {clean(status_.app, 60)});
        if (status_.lanes > 1 && !status_.more.empty()) {
            const std::string dl = devicesLine(s);
            if (!dl.empty()) t += std::string(t.empty() ? "" : "  |  ") + dl;
        } else if (!status_.device.empty()) {
            t += std::string(t.empty() ? "" : "  |  ") + format(s.tr("Device: %1"), {clean(status_.device, 60)});
        }
        const std::string in = inputLine(s);
        if (!in.empty()) t += std::string(t.empty() ? "" : "  |  ") + in;
        return t;
    }
    // Several devices: "Devices: A (Joystick mode: normal), B (Joystick mode: timeline)"; empty with one device.
    std::string devicesLine(const StringProvider &s) const {
        if (!daemonRunning() || status_.lanes < 2 || status_.more.empty()) return "";
        auto one = [&](const std::string &dev, const std::string &mode) {
            std::string t = clean(dev.empty() ? "?" : dev, 60);
            if (!mode.empty()) t += " (" + format(s.tr("Joystick mode: %1"), {clean(mode, 40)}) + ")";
            return t;
        };
        std::string list = one(status_.device, status_.mode);
        for (const DaemonStatus::Lane &l : status_.more) list += ", " + one(l.device, l.mode);
        return format(s.tr("Devices: %1"), {list});
    }
    // "Input: 3Dconnexion driver" / "Input: direct (HID)" (Windows and macOS); empty when the daemon reports none.
    std::string inputLine(const StringProvider &s) const {
        if (!daemonRunning()) return "";
        if (status_.input == "driver") return format(s.tr("Input: %1"), {s.tr("3Dconnexion driver")});
        if (status_.input == "hid") return format(s.tr("Input: %1"), {s.tr("direct (HID)")});
        return "";
    }
    std::string warningLine(const StringProvider &s) const {
        if (!daemonRunning()) return "";
        // A device that cannot be read matters more than the hint about several devices.
        // The literals must equal inputIssueTextBlocked()/inputIssueTextSilent() (tests/test_input_diag.cpp):
        // gettext extracts only string literals.
        if (status_.input_issue == InputIssue::Blocked)
            return s.tr("SpaceMouse found but cannot be read: another program, possibly the 3Dconnexion driver, holds it "
                        "exclusively. See docs/troubleshooting.md, section \"The official 3Dconnexion driver and dofscroll\"");
        if (status_.input_issue == InputIssue::Silent)
            return s.tr("No input from the SpaceMouse yet. Move the cap. If nothing arrives, another program, possibly the "
                        "3Dconnexion driver, may be holding the device. See docs/troubleshooting.md, section \"The official "
                        "3Dconnexion driver and dofscroll\"");
        if (status_.devices_seen < 2 || status_.lanes > 1) return "";  // one lane per device: nothing hidden
        return format(s.tr("%1 SpaceMice connected, only '%2' is reported"),
                      {std::to_string(status_.devices_seen), clean(status_.device, 60)});
    }
    // Tooltip/title text, e.g. "dofscroll - Profile: global".
    std::string title(const StringProvider &s) const { return "dofscroll - " + mainLine(s); }

private:
    static std::string clean(const std::string &in, size_t maxLen) { return sanitizeForLog(in, maxLen); }
    DaemonStatus status_;
    State state_ = State::NoStatus;
};

}  // namespace spn::ui
