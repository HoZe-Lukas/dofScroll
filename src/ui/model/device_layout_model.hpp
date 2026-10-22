// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Where the numbered buttons are drawn around the schematic cap in the visual joystick mode editor (issue
// #143, toolkit-free). One schematic cap is drawn for every model; only the button positions differ:
//   two buttons (Compact, Wireless, SpaceNavigator)  one on each side of the cap
//   Pro, Pro Wireless (15)                           two columns on each side
//   Enterprise (31)                                  a keypad under the cap
//   any other model                                  a plain numbered grid (like the cells of the Axes page)
// The positions are schematic: the numbers are right, the places are an arrangement, not a copy of the
// device (no vendor artwork). The number of buttons is factoryButtonCount(), never the reported count alone.
//
// Slot coordinates are in a unit box, x to the right and y down, the cap in the middle at (0.5, 0.45) with
// room for the keypad below; the view scales them to its size and keeps the 24 px minimum target by spacing.
//
//   const DeviceLayout l = deviceLayout(deviceName, reportedButtons);
//   for (const ButtonSlot &s : l.slots) place(s.number, s.x, s.y);
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "device_db.hpp"

namespace spn::ui {

struct ButtonSlot {
    int number = 0;  // 1-based
    double x = 0, y = 0;
};

enum class LayoutKind { None, TwoSides, Pro, Keypad, Grid };

struct DeviceLayout {
    LayoutKind kind = LayoutKind::None;
    int count = 0;
    std::vector<ButtonSlot> slots;  // number order
    int columns = 0;                // Grid and Keypad
    bool knownModel = false;
};

// True when the model is in the table of factoryButtonCount().
inline bool knownDeviceModel(const std::string &deviceName) {
    constexpr int probe = 9999;
    return factoryButtonCount(deviceName, probe) != probe;
}

inline DeviceLayout deviceLayout(const std::string &deviceName, int reportedButtons) {
    DeviceLayout l;
    l.knownModel = knownDeviceModel(deviceName);
    l.count = std::max(0, std::min(32, factoryButtonCount(deviceName, reportedButtons)));
    if (l.count == 0) return l;
    auto grid = [&](double x0, double y0, double x1, double y1, int cols) {
        const int rows = (l.count + cols - 1) / cols;
        for (int i = 0; i < l.count; ++i) {
            const int c = i % cols, r = i / cols;
            ButtonSlot s;
            s.number = i + 1;
            s.x = cols == 1 ? (x0 + x1) / 2 : x0 + (x1 - x0) * c / (cols - 1);
            s.y = rows == 1 ? (y0 + y1) / 2 : y0 + (y1 - y0) * r / (rows - 1);
            l.slots.push_back(s);
        }
    };
    if (!l.knownModel) {
        l.kind = LayoutKind::Grid;
        l.columns = l.count <= 4 ? l.count : int(std::ceil(std::sqrt(double(l.count))));
        grid(0.1, 0.1, 0.9, 0.9, l.columns);
        return l;
    }
    if (l.count <= 2) {
        l.kind = LayoutKind::TwoSides;
        for (int i = 0; i < l.count; ++i) l.slots.push_back({i + 1, l.count == 1 ? 0.5 : (i == 0 ? 0.08 : 0.92), 0.45});
        return l;
    }
    if (l.count <= 16) {
        l.kind = LayoutKind::Pro;
        // the first half on the left, the rest on the right; two columns of buttons on each side
        const int left = (l.count + 1) / 2, right = l.count - left;
        auto side = [&](int first, int n, double xa, double xb) {
            const int rows = (n + 1) / 2;
            for (int i = 0; i < n; ++i) {
                const int c = i % 2, r = i / 2;
                ButtonSlot s;
                s.number = first + i;
                s.x = c == 0 ? xa : xb;
                s.y = rows == 1 ? 0.45 : 0.1 + 0.7 * r / (rows - 1);
                l.slots.push_back(s);
            }
        };
        side(1, left, 0.05, 0.17);
        side(left + 1, right, 0.83, 0.95);
        return l;
    }
    l.kind = LayoutKind::Keypad;
    l.columns = 8;
    grid(0.1, 0.78, 0.9, 0.98, l.columns);
    return l;
}

}  // namespace spn::ui
