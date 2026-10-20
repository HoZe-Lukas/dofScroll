// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// OS-independent part of the driver-interface input backends (macOS: 3DconnexionClient framework,
// Windows: TDxInput COM object). The OS backends load the installed driver's interface, read states
// from it and call into this class; the class turns them into InputEvents with the same meaning and
// scale as the HID path (HidInputCore): axes in the libspnav scale (about +-350 at full deflection),
// buttons as 0-based hardware indices. Everything here is unit-tested on every platform
// (tests/test_driver_input_core.cpp).
//
// The driver interface aggregates all attached devices into one stream, so this source reports at
// most one device (id 1) and cannot tell two SpaceMice apart.
//
// UNIT ASSUMPTIONS (unverified without the real driver; flip the constants below after the hardware
// test, they are the only place):
//   - macOS: the six int16 axis values of the device state are used as they are (factor 1.0). They are
//     assumed to be on the raw 16-bit scale of the device (about +-350 for a SpaceNavigator-class
//     device). No public source gives the scale.
//   - Windows: Sensor.Translation X/Y/Z are doubles in "arbitrary units" (forum remark); factor 1.0 is
//     a placeholder. Sensor.Rotation is an axis/angle pair: the rotation vector is axis * angle, and
//     the unit of Angle is not documented; factor 1.0 is a placeholder too.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "event_queue.hpp"
#include "hid_input_core.hpp"  // kHidAxisLimit
#include "platform.hpp"

namespace spn {

// Multipliers from the driver's values to the libspnav scale (see the assumptions above).
constexpr double kMacDriverAxisScale = 1.0;
constexpr double kWinDriverTranslationScale = 1.0;
constexpr double kWinDriverRotationScale = 1.0;

// Windows: the Keyboard object numbers keys from this value (IsKeyDown(code)); button index = code - base.
// Public sources do not say whether the first key is 0 or 1: unverified.
constexpr long kWinDriverKeyBase = 1;

constexpr const char *kDriverFallbackName = "SpaceMouse (driver)";

// Rotation vector (axis * angle) from the axis/angle pair of the Windows COM interface. A zero or
// non-finite input gives zero. The result is the three rotation axes rx, ry, rz.
inline std::array<double, 3> axisAngleToVector(double x, double y, double z, double angle) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(angle)) return {0, 0, 0};
    return {x * angle, y * angle, z * angle};
}

class DriverInputCore {
public:
    // Appears when the driver reports a device (macOS: added handler, Windows: Connect succeeded).
    // name empty: kDriverFallbackName. Pushes DevicesChanged.
    void deviceAdded(const std::string &name, unsigned vendor, unsigned product, int buttons) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            present_ = true;
            info_.id = 1;
            info_.name = name.empty() ? kDriverFallbackName : name;
            info_.vendor = vendor;
            info_.product = product;
            info_.reportedButtons = buttons > 0 ? std::min(buttons, 32) : 0;
            info_.axes = 6;
        }
        pushChanged();
    }

    // The device went away (or the driver did): neutral state first, then DevicesChanged.
    void deviceRemoved() {
        std::vector<InputEvent> ev;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!present_) return;
            present_ = false;
            neutralLocked(&ev);
        }
        queue_.push(ev);
        pushChanged();
    }

    // Six axis values in the driver's units (tx, ty, tz, rx, ry, rz); `translationScale` and
    // `rotationScale` convert them to the libspnav scale. Ignored while no device is present.
    // Pushes one Motion event when any axis value changed.
    void onAxes(const std::array<double, 6> &v, double translationScale, double rotationScale) {
        InputEvent e;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!present_) return;
            std::array<int, 6> a{};
            for (size_t i = 0; i < 6; ++i) a[i] = clampAxis(v[i] * (i < 3 ? translationScale : rotationScale));
            if (a == axes_) return;
            axes_ = a;
            e.type = InputEventType::Motion;
            e.device = info_.id;
            e.axes = a;
        }
        queue_.push(e);
    }

    // Button bitmask (bit n = hardware index n). One Button event per changed bit, lowest first.
    void onButtons(uint32_t mask) {
        std::vector<InputEvent> ev;
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (!present_) return;
            const uint32_t diff = mask ^ buttons_;
            for (int b = 0; b < 32; ++b) {
                if (!(diff & (1u << b))) continue;
                InputEvent e;
                e.type = InputEventType::Button;
                e.device = info_.id;
                e.button = b;
                e.down = (mask & (1u << b)) != 0;
                ev.push_back(e);
            }
            buttons_ = mask;
        }
        queue_.push(ev);
    }

    // Sleep/wake or driver restart: bring the consumer to the neutral state without removing the device.
    void reset() {
        std::vector<InputEvent> ev;
        {
            std::lock_guard<std::mutex> lk(mu_);
            neutralLocked(&ev);
        }
        queue_.push(ev);
    }

    std::vector<DeviceInfo> devices() const {
        std::lock_guard<std::mutex> lk(mu_);
        return present_ ? std::vector<DeviceInfo>{info_} : std::vector<DeviceInfo>();
    }
    bool present() const {
        std::lock_guard<std::mutex> lk(mu_);
        return present_;
    }

    WaitResult wait(InputEvent *out, int timeoutMs) { return queue_.wait(out, timeoutMs); }
    void open() {
        queue_.reopen();
        std::lock_guard<std::mutex> lk(mu_);
        present_ = false;
        axes_ = {};
        buttons_ = 0;
    }
    void close() { queue_.close(); }

private:
    static int clampAxis(double v) {
        if (!std::isfinite(v)) return 0;
        const double r = std::round(v);
        return static_cast<int>(std::max<double>(-kHidAxisLimit, std::min<double>(kHidAxisLimit, r)));
    }
    void neutralLocked(std::vector<InputEvent> *ev) {
        for (int b = 0; b < 32; ++b) {
            if (!(buttons_ & (1u << b))) continue;
            InputEvent e;
            e.type = InputEventType::Button;
            e.device = info_.id;
            e.button = b;
            e.down = false;
            ev->push_back(e);
        }
        buttons_ = 0;
        if (std::any_of(axes_.begin(), axes_.end(), [](int v) { return v != 0; })) {
            axes_ = {};
            InputEvent e;
            e.type = InputEventType::Motion;
            e.device = info_.id;
            ev->push_back(e);
        }
    }
    void pushChanged() {
        InputEvent e;
        e.type = InputEventType::DevicesChanged;
        queue_.push(e);
    }

    EventQueue queue_;
    mutable std::mutex mu_;
    bool present_ = false;
    DeviceInfo info_;
    std::array<int, 6> axes_{};
    uint32_t buttons_ = 0;
};

}  // namespace spn
