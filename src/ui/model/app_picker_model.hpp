// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Toolkit-free logic of the "Add application" / "Add device" pickers: filtering, sorting,
// "already has a profile" flags, validity check and the capture-next-focus state machine.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "config.hpp"
#include "platform/platform.hpp"
#include "platform/running_apps.hpp"
#include "platform/running_apps_util.hpp"
#include "profile_match.hpp"

namespace spn {

// Case-insensitive substring test; an empty needle matches everything.
inline bool containsNoCase(const std::string &hay, const std::string &needle) {
    if (needle.empty()) return true;
    return lowerAscii(hay).find(lowerAscii(needle)) != std::string::npos;
}

// Valid [app:*]/[device:*] pattern (same rule as ProfileSet::normalizePattern). *normalized gets
// the trimmed pattern; on failure *err (if non-null) gets a short reason.
inline bool validateProfilePattern(const std::string &typed, std::string *normalized,
                                   std::string *err) {
    std::string n;
    if (ProfileSet::normalizePattern(typed, &n)) {
        if (normalized) *normalized = n;
        return true;
    }
    if (err) {
        std::string t = typed;
        const bool blank = t.find_first_not_of(" \t\r\n") == std::string::npos;
        *err = blank ? "The match string is empty"
                     : "The match string must not contain ']' or line breaks";
    }
    return false;
}

// Exact (trimmed, case-insensitive) equality with one of the configured patterns of that kind.
inline bool hasProfileFor(const std::vector<std::string> &configured, const std::string &s) {
    std::string n;
    if (!ProfileSet::normalizePattern(s, &n)) return false;
    const std::string l = lowerAscii(n);
    for (const std::string &c : configured)
        if (lowerAscii(c) == l) return true;
    return false;
}

struct AppRow {
    RunningApp app;
    bool hasProfile = false;  // shown flagged ("already has a profile"), not hidden
};

class AppPickerModel {
public:
    void setApps(std::vector<RunningApp> apps) { apps_ = std::move(apps); }
    // Patterns of the existing [app:*] sections.
    void setConfigured(std::vector<std::string> patterns) { configured_ = std::move(patterns); }

    // Filter on name, class (matchString) and title; stable sort by name (case-insensitive,
    // ties keep the input order).
    std::vector<AppRow> rows(const std::string &filter) const {
        std::vector<AppRow> out;
        for (const RunningApp &a : apps_) {
            if (!(containsNoCase(a.displayName, filter) || containsNoCase(a.matchString, filter) ||
                  containsNoCase(a.windowTitle, filter)))
                continue;
            AppRow r;
            r.app = a;
            r.hasProfile = hasProfileFor(configured_, a.matchString);
            out.push_back(std::move(r));
        }
        std::stable_sort(out.begin(), out.end(), [](const AppRow &x, const AppRow &y) {
            return lowerAscii(sortName(x.app)) < lowerAscii(sortName(y.app));
        });
        return out;
    }

    bool alreadyConfigured(const std::string &matchString) const {
        return hasProfileFor(configured_, matchString);
    }

private:
    static std::string sortName(const RunningApp &a) {
        return a.displayName.empty() ? a.matchString : a.displayName;
    }
    std::vector<RunningApp> apps_;
    std::vector<std::string> configured_;
};

// "Capture the next focused program": deterministic, driven by the caller's clock.
// Usage: start(now, activeStringAtStart); then tick(activeString, now) periodically
// (e.g. every 100 ms) until it returns true or state() == TimedOut.
class CaptureNextFocus {
public:
    enum class State { Idle, Waiting, Captured, TimedOut };

    explicit CaptureNextFocus(int64_t timeoutMs = 8000) : timeoutMs_(timeoutMs) {}

    // baseline: what the active-window provider reports right now (usually the UI itself or the
    // program the user came from); an unchanged value never counts as captured.
    void start(int64_t nowMs, const std::string &baseline = std::string()) {
        state_ = State::Waiting;
        startMs_ = nowMs;
        baseline_ = baseline;
        result_.clear();
    }
    void cancel() { state_ = State::Idle; }

    // Returns true exactly once, when a string is captured. Empty strings and the dofscroll UI's
    // own window are ignored; the first different non-empty string wins.
    bool tick(const std::string &current, int64_t nowMs) {
        if (state_ != State::Waiting) return false;
        if (nowMs - startMs_ >= timeoutMs_) {
            state_ = State::TimedOut;
            return false;
        }
        if (current.empty() || isOwnAppString(current) || current == baseline_) return false;
        result_ = current;
        state_ = State::Captured;
        return true;
    }

    State state() const { return state_; }
    const std::string &result() const { return result_; }
    int64_t remainingMs(int64_t nowMs) const {
        if (state_ != State::Waiting) return 0;
        return std::max<int64_t>(0, timeoutMs_ - (nowMs - startMs_));
    }

private:
    int64_t timeoutMs_;
    State state_ = State::Idle;
    int64_t startMs_ = 0;
    std::string baseline_;
    std::string result_;
};

struct DeviceRow {
    DeviceInfo dev;
    std::string idString;     // "vvvv:pppp" lowercase hex, empty if vendor/product unknown
    bool hasProfile = false;  // [device:*] pattern equals the device name
};

class DevicePickerModel {
public:
    void setDevices(std::vector<DeviceInfo> devs) { devs_ = std::move(devs); }
    void setConfigured(std::vector<std::string> patterns) { configured_ = std::move(patterns); }

    static std::string vendorProduct(const DeviceInfo &d) {
        if (d.vendor == 0 && d.product == 0) return std::string();
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04x:%04x", d.vendor & 0xffffu, d.product & 0xffffu);
        return buf;
    }

    // Filter on name and vendor:product; stable sort by name.
    std::vector<DeviceRow> rows(const std::string &filter) const {
        std::vector<DeviceRow> out;
        for (const DeviceInfo &d : devs_) {
            DeviceRow r;
            r.dev = d;
            r.idString = vendorProduct(d);
            if (!(containsNoCase(d.name, filter) || containsNoCase(r.idString, filter))) continue;
            r.hasProfile = hasProfileFor(configured_, d.name);
            out.push_back(std::move(r));
        }
        std::stable_sort(out.begin(), out.end(), [](const DeviceRow &x, const DeviceRow &y) {
            return lowerAscii(x.dev.name) < lowerAscii(y.dev.name);
        });
        return out;
    }

private:
    std::vector<DeviceInfo> devs_;
    std::vector<std::string> configured_;
};

}  // namespace spn
