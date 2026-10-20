// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Hybrid input of the Windows and macOS daemons: the SpaceMouse is read either through the interface
// of the installed official 3Dconnexion driver ("driver") or directly as raw HID ("hid"). This file is
// free of OS and toolkit code (it runs and is tested on every platform with fake sources):
//   - InputMode / InputPath names and the config value parser,
//   - THE table of per-OS defaults for INPUT = auto (autoOrder), to be flipped after the hardware test,
//   - candidatePaths(): which paths are tried, in which order,
//   - SwitchingInput: an InputSource that owns the two real sources, opens the right one, falls back,
//     retries, and never flaps.
//
// Rules (docs/port-architecture.md, section "Driver interface backend"):
//   INPUT = driver | hid   exactly that path. Nothing else is tried: a user who chose a path wants to
//                          see that it does not work instead of silently getting the other one.
//   INPUT = auto           candidates in the order of autoOrder(); the driver path only when the driver
//                          is installed. The first candidate that opens is used. A path "opens" when its
//                          interface registers or its device manager starts; whether it delivers
//                          reports cannot be known before the user moves the device.
//   Switching while running (auto only; every switch needs a target that has devices NOW):
//     1. the current path has had no device for kNoDeviceMs -> try the other path;
//     2. the current path is the fallback, nothing arrived for kQuietMs and the preferred path was not
//        probed for kReprobeMs -> probe the preferred path (switch only if it has devices).
//   A probe opens the other source while the current one stays open, waits up to probeWindowMs for a
//   device and either switches (closing the old source) or closes the probe. A path that has no device
//   is never switched to, so with nothing plugged in nothing changes. Changing INPUT while running
//   switches at once (Driver/Hid: to that path; Auto: toward the preferred path if it has devices).
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../log_sanitize.hpp"
#include "platform.hpp"

namespace spn {

enum class InputPath { None, Driver, Hid };
enum class InputOs { Windows, Mac, Other };

constexpr InputOs thisInputOs() {
#if defined(_WIN32)
    return InputOs::Windows;
#elif defined(__APPLE__)
    return InputOs::Mac;
#else
    return InputOs::Other;
#endif
}

inline const char *inputPathId(InputPath p) {
    return p == InputPath::Driver ? "driver" : p == InputPath::Hid ? "hid" : "";
}
// The wording shown after "Input: " in the status line, the log and the interface.
inline const char *inputPathText(InputPath p) {
    return p == InputPath::Driver ? "3Dconnexion driver" : p == InputPath::Hid ? "direct (HID)" : "none";
}
inline InputPath inputPathFromId(const std::string &id) {
    return id == "driver" ? InputPath::Driver : id == "hid" ? InputPath::Hid : InputPath::None;
}

inline const char *inputModeName(InputMode m) {
    return m == InputMode::Driver ? "driver" : m == InputMode::Hid ? "hid" : "auto";
}
// "auto", "driver" or "hid" (ASCII, case-insensitive, surrounding blanks ignored); false otherwise.
inline bool parseInputMode(const std::string &text, InputMode *out) {
    size_t a = 0, b = text.size();
    while (a < b && (text[a] == ' ' || text[a] == '\t')) ++a;
    while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) --b;
    std::string v = text.substr(a, b - a);
    for (char &c : v)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    if (v == "auto") *out = InputMode::Auto;
    else if (v == "driver") *out = InputMode::Driver;
    else if (v == "hid") *out = InputMode::Hid;
    else return false;
    return true;
}

// ---- THE table of defaults for INPUT = auto -------------------------------------------------------------
// first = tried first, second = fallback. Both are UNVERIFIED guesses from public reports (see
// docs/port-architecture.md); flip them here after the hardware test, nowhere else.
//   macOS:   driver first. Public reports say the driver's helper holds the HID device, so raw reports
//            do not arrive while it runs.
//   Windows: HID first. Public reports (e.g. Blender reads the device through Raw Input next to a
//            running driver) say both can read; HID needs no driver at all. The driver path is the
//            fallback when HID cannot be opened.
struct AutoOrder {
    InputPath first, second;
};
constexpr AutoOrder autoOrder(InputOs os) {
    return os == InputOs::Windows ? AutoOrder{InputPath::Hid, InputPath::Driver}
                                  : AutoOrder{InputPath::Driver, InputPath::Hid};
}

// The paths to try, in order. Driver without an installed driver: empty (nothing to try).
inline std::vector<InputPath> candidatePaths(InputMode mode, InputOs os, bool driverInstalled) {
    std::vector<InputPath> v;
    if (mode == InputMode::Hid) {
        v.push_back(InputPath::Hid);
    } else if (mode == InputMode::Driver) {
        if (driverInstalled) v.push_back(InputPath::Driver);
    } else {
        const AutoOrder o = autoOrder(os);
        for (InputPath p : {o.first, o.second})
            if (p != InputPath::Driver || driverInstalled) v.push_back(p);
    }
    return v;
}

struct SwitchTiming {
    int64_t noDeviceMs = 20000;     // current path without a device this long -> try the other path
    int64_t quietMs = 10000;        // fallback path without any event this long -> may probe the preferred path
    int64_t reprobeMs = 30000;      // minimum distance between two probes of the same path
    int probeWindowMs = 2500;       // how long a probe waits for the probed path to report a device
};

class SwitchingInput : public InputSource {
public:
    struct Parts {
        std::function<bool()> driverInstalled;                    // cheap probe, may be called often
        std::function<std::unique_ptr<InputSource>()> makeDriver;  // null result: path not available
        std::function<std::unique_ptr<InputSource>()> makeHid;
    };

    SwitchingInput(Parts parts, InputOs os, InputMode mode = InputMode::Auto, const SwitchTiming &timing = SwitchTiming(),
                   std::function<int64_t()> clockNs = nullptr, std::function<void(const std::string &)> log = nullptr)
        : parts_(std::move(parts)), os_(os), timing_(timing), clock_(std::move(clockNs)), log_(std::move(log)) {
        mode_.store(static_cast<int>(mode));
        appliedMode_ = mode;
    }

    const char *name() const override {
        const InputPath p = path();
        return p == InputPath::Driver ? "driver" : p == InputPath::Hid ? "hid" : "input";
    }
    void setInputMode(InputMode m) override { mode_.store(static_cast<int>(m)); }
    std::string pathLabel() const override { return inputPathId(path()); }
    InputPath path() const {
        std::lock_guard<std::mutex> lk(mu_);
        return path_;
    }

    bool open(std::string *err) override {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (cur_) return true;
        }
        const InputMode mode = currentMode();
        appliedMode_ = mode;
        const bool installed = driverInstalled();
        const std::vector<InputPath> order = candidatePaths(mode, os_, installed);
        std::string why;
        if (order.empty()) {
            why = mode == InputMode::Driver ? "INPUT=driver, but the 3Dconnexion driver is not installed" : "no input path";
        }
        for (InputPath p : order) {
            std::string e;
            std::shared_ptr<InputSource> s = openPath(p, &e);
            if (s) {
                adopt(p, s, /*announce=*/true);
                const std::string note = std::string("Input: ") + inputPathText(p) +
                                         (mode == InputMode::Auto ? " (automatic)" : " (INPUT=" + std::string(inputModeName(mode)) + ")");
                logLine(note);
                return true;
            }
            if (!why.empty()) why += "; ";
            why += std::string(inputPathText(p)) + ": " + (e.empty() ? "cannot be opened" : e);
        }
        if (why != lastFailure_) {  // once per distinct failure, not on every retry of the backoff
            lastFailure_ = why;
            logLine("Input not available: " + why);
        }
        if (err) *err = why;
        return false;
    }

    WaitResult wait(InputEvent *out, int timeoutMs) override {
        if (pendingChanged_) {
            pendingChanged_ = false;
            *out = InputEvent();
            out->type = InputEventType::DevicesChanged;
            return WaitResult::Event;
        }
        std::shared_ptr<InputSource> s = current();
        if (!s) return WaitResult::Closed;
        const WaitResult r = s->wait(out, timeoutMs);
        const int64_t now = nowNs();
        if (r == WaitResult::Closed) return WaitResult::Closed;  // source gone: the daemon reopens with backoff
        if (r == WaitResult::Event) {
            lastEventNs_ = now;
            if (out->type == InputEventType::DevicesChanged) noDeviceSince_ = -1;
            return r;
        }
        // Timeout: supervision. Closed here means "reopen with the new configuration".
        return supervise(now) ? WaitResult::Timeout : WaitResult::Closed;
    }

    std::vector<DeviceInfo> devices() const override {
        std::shared_ptr<InputSource> s = current();
        return s ? s->devices() : std::vector<DeviceInfo>();
    }

    void close() override {
        std::shared_ptr<InputSource> s;
        {
            std::lock_guard<std::mutex> lk(mu_);
            s = std::move(cur_);
            cur_.reset();
            path_ = InputPath::None;
        }
        if (s) s->close();
        // A fresh open() starts the evaluation over; the probe timers are kept (no probing storm).
        noDeviceSince_ = -1;
    }

private:
    std::shared_ptr<InputSource> current() const {
        std::lock_guard<std::mutex> lk(mu_);
        return cur_;
    }
    InputMode currentMode() const { return static_cast<InputMode>(mode_.load()); }
    // The probe may touch the registry or the file system: open() asks afresh, supervision at most every kInstalledTtlMs.
    bool driverInstalled(bool fresh = true) {
        const int64_t now = nowNs();
        if (!fresh && installedAt_ != 0 && now - installedAt_ < kInstalledTtlMs * 1000000) return installed_;
        installed_ = parts_.driverInstalled && parts_.driverInstalled();
        installedAt_ = now == 0 ? 1 : now;
        return installed_;
    }
    int64_t nowNs() const {
        if (clock_) return clock_();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    void logLine(const std::string &s) const {
        if (log_) log_(s);
        else std::cerr << "[dofscroll] " << sanitizeForLog(s) << std::endl;
    }

    std::shared_ptr<InputSource> openPath(InputPath p, std::string *err) {
        std::unique_ptr<InputSource> u;
        if (p == InputPath::Driver && parts_.makeDriver) u = parts_.makeDriver();
        else if (p == InputPath::Hid && parts_.makeHid) u = parts_.makeHid();
        if (!u) {
            if (err) *err = "not available on this system";
            return nullptr;
        }
        std::shared_ptr<InputSource> s(std::move(u));
        if (!s->open(err)) return nullptr;
        return s;
    }

    void adopt(InputPath p, const std::shared_ptr<InputSource> &s, bool announce) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            cur_ = s;
            path_ = p;
        }
        const int64_t now = nowNs();
        lastEventNs_ = now;
        noDeviceSince_ = -1;
        lastFailure_.clear();
        if (announce) pendingChanged_ = true;  // the reader republishes the device list of the new source
    }

    // Returns false when the current source must be reopened (configuration change that cannot be
    // applied in place).
    bool supervise(int64_t now) {
        const InputMode mode = currentMode();
        const InputPath cur = path();
        const bool installed = driverInstalled(mode != appliedMode_);
        const std::vector<InputPath> order = candidatePaths(mode, os_, installed);

        if (mode != appliedMode_) {  // INPUT changed while running
            appliedMode_ = mode;
            if (mode != InputMode::Auto) {
                if (!order.empty() && order[0] == cur) return true;
                logLine(std::string("INPUT=") + inputModeName(mode) + ": switching the input path");
                return false;  // close, then open() applies the mode with the usual backoff
            }
            if (!order.empty() && order[0] != cur) tryProbe(order[0], now, /*force=*/true);
            return true;
        }
        if (mode != InputMode::Auto || order.size() < 2) return true;

        std::shared_ptr<InputSource> s = current();
        if (!s) return false;
        const bool hasDevice = !s->devices().empty();
        if (hasDevice) noDeviceSince_ = -1;
        else if (noDeviceSince_ < 0) noDeviceSince_ = now;

        const InputPath other = cur == order[0] ? order[1] : order[0];
        if (!hasDevice && noDeviceSince_ >= 0 && now - noDeviceSince_ >= timing_.noDeviceMs * 1000000) {
            tryProbe(other, now, /*force=*/false);
        } else if (cur != order[0] && now - lastEventNs_ >= timing_.quietMs * 1000000) {
            tryProbe(order[0], now, /*force=*/false);
        }
        return true;
    }

    int64_t &lastProbe(InputPath p) { return p == InputPath::Driver ? lastProbeDriver_ : lastProbeHid_; }

    // Opens `target` next to the current source; switches when it reports a device in time.
    void tryProbe(InputPath target, int64_t now, bool force) {
        int64_t &last = lastProbe(target);
        if (!force && last != 0 && now - last < timing_.reprobeMs * 1000000) return;
        last = now;
        std::string e;
        std::shared_ptr<InputSource> s = openPath(target, &e);
        if (!s) return;
        bool has = false;
        const int step = 20;
        for (int waited = 0;; waited += step) {
            if (!s->devices().empty()) {
                has = true;
                break;
            }
            if (waited >= timing_.probeWindowMs) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(step));
        }
        if (!has) {
            s->close();
            return;
        }
        std::shared_ptr<InputSource> old;
        {
            std::lock_guard<std::mutex> lk(mu_);
            old = cur_;
        }
        adopt(target, s, /*announce=*/true);
        if (old) old->close();
        logLine(std::string("Input: ") + inputPathText(target) + " (switched)");
    }

    Parts parts_;
    InputOs os_;
    SwitchTiming timing_;
    std::function<int64_t()> clock_;
    std::function<void(const std::string &)> log_;
    std::atomic<int> mode_{0};
    InputMode appliedMode_ = InputMode::Auto;  // reader thread only (with everything below the mutex)

    mutable std::mutex mu_;
    std::shared_ptr<InputSource> cur_;
    InputPath path_ = InputPath::None;

    bool pendingChanged_ = false;
    int64_t lastEventNs_ = 0, noDeviceSince_ = -1;
    int64_t lastProbeDriver_ = 0, lastProbeHid_ = 0;
    static constexpr int64_t kInstalledTtlMs = 5000;
    bool installed_ = false;
    int64_t installedAt_ = 0;
    std::string lastFailure_;
};

}  // namespace spn
