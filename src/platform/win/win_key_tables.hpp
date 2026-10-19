// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows key tables and event arithmetic for the SendInput output (win_output.cpp). Header-only
// and free of any Windows header, so it is unit-tested on every OS (tests/test_win_key_tables.cpp).
// The numeric values are the documented VK_* / MOUSEEVENTF_* constants; each is named in a
// comment. The switch has no default, so -Wswitch flags a Key that is missing here.
#pragma once

#include <cstdint>

#include "../../keys.hpp"

namespace spn {
namespace winkeys {

// One key as SendInput needs it: virtual-key code, whether the scan code carries the 0xE0 prefix
// (KEYEVENTF_EXTENDEDKEY: arrows, the Insert/Delete/Home/End/Page block, Win keys, right-hand
// Ctrl/Alt), and whether Shift must be held for it on the default (US) layout.
struct WinKeySpec {
    uint16_t vk = 0;  // 0 = unmapped (never the case for a valid Key)
    bool extended = false;
    bool shift = false;
};

inline WinKeySpec keyToWin(Key k) {
    switch (k) {
    case Key::Ctrl: return {0xA2, false, false};       // VK_LCONTROL
    case Key::Up: return {0x26, true, false};          // VK_UP
    case Key::Down: return {0x28, true, false};        // VK_DOWN
    case Key::Left: return {0x25, true, false};        // VK_LEFT
    case Key::Right: return {0x27, true, false};       // VK_RIGHT
    case Key::Shift: return {0xA0, false, false};      // VK_LSHIFT
    case Key::Alt: return {0xA4, false, false};        // VK_LMENU
    case Key::Super: return {0x5B, true, false};       // VK_LWIN
    case Key::Space: return {0x20, false, false};      // VK_SPACE
    case Key::Enter: return {0x0D, false, false};      // VK_RETURN
    case Key::Esc: return {0x1B, false, false};        // VK_ESCAPE
    case Key::Tab: return {0x09, false, false};        // VK_TAB
    case Key::Backspace: return {0x08, false, false};  // VK_BACK
    case Key::Delete: return {0x2E, true, false};      // VK_DELETE
    case Key::Insert: return {0x2D, true, false};      // VK_INSERT
    case Key::Home: return {0x24, true, false};        // VK_HOME
    case Key::End: return {0x23, true, false};         // VK_END
    case Key::PageUp: return {0x21, true, false};      // VK_PRIOR
    case Key::PageDown: return {0x22, true, false};    // VK_NEXT
    case Key::Minus: return {0xBD, false, false};      // VK_OEM_MINUS (main block)
    case Key::Plus: return {0xBB, false, true};        // VK_OEM_PLUS + Shift (US default; the output
                                                       // re-resolves it with VkKeyScanW)
    case Key::KpPlus: return {0x6B, false, false};     // VK_ADD (keypad)
    case Key::KpMinus: return {0x6D, false, false};    // VK_SUBTRACT (keypad)
    case Key::F1: return {0x70, false, false};         // VK_F1
    case Key::F2: return {0x71, false, false};
    case Key::F3: return {0x72, false, false};
    case Key::F4: return {0x73, false, false};
    case Key::F5: return {0x74, false, false};
    case Key::F6: return {0x75, false, false};
    case Key::F7: return {0x76, false, false};
    case Key::F8: return {0x77, false, false};
    case Key::F9: return {0x78, false, false};
    case Key::F10: return {0x79, false, false};
    case Key::F11: return {0x7A, false, false};
    case Key::F12: return {0x7B, false, false};  // VK_F12
    // Letters: VK_A..VK_Z are the ASCII upper-case codes.
    case Key::A: return {0x41, false, false};
    case Key::B: return {0x42, false, false};
    case Key::C: return {0x43, false, false};
    case Key::D: return {0x44, false, false};
    case Key::E: return {0x45, false, false};
    case Key::F: return {0x46, false, false};
    case Key::G: return {0x47, false, false};
    case Key::H: return {0x48, false, false};
    case Key::I: return {0x49, false, false};
    case Key::J: return {0x4A, false, false};
    case Key::K: return {0x4B, false, false};
    case Key::L: return {0x4C, false, false};
    case Key::M: return {0x4D, false, false};
    case Key::N: return {0x4E, false, false};
    case Key::O: return {0x4F, false, false};
    case Key::P: return {0x50, false, false};
    case Key::Q: return {0x51, false, false};
    case Key::R: return {0x52, false, false};
    case Key::S: return {0x53, false, false};
    case Key::T: return {0x54, false, false};
    case Key::U: return {0x55, false, false};
    case Key::V: return {0x56, false, false};
    case Key::W: return {0x57, false, false};
    case Key::X: return {0x58, false, false};
    case Key::Y: return {0x59, false, false};
    case Key::Z: return {0x5A, false, false};
    // Digits of the main block: VK_0..VK_9 are the ASCII codes.
    case Key::N0: return {0x30, false, false};
    case Key::N1: return {0x31, false, false};
    case Key::N2: return {0x32, false, false};
    case Key::N3: return {0x33, false, false};
    case Key::N4: return {0x34, false, false};
    case Key::N5: return {0x35, false, false};
    case Key::N6: return {0x36, false, false};
    case Key::N7: return {0x37, false, false};
    case Key::N8: return {0x38, false, false};
    case Key::N9: return {0x39, false, false};
    case Key::Count_: break;
    }
    return {};
}

// Right-hand modifier variants: not in the Key table, but may be stuck after a crash of an
// earlier process, so forceReleaseAll releases them as well.
struct ExtraRelease {
    uint16_t vk;
    bool extended;
};
inline constexpr ExtraRelease kExtraReleases[] = {
    {0xA3, true},   // VK_RCONTROL
    {0xA1, false},  // VK_RSHIFT (not an extended key)
    {0xA5, true},   // VK_RMENU
    {0x5C, true},   // VK_RWIN
};

// ---- wheel ----------------------------------------------------------------------------------

constexpr int kWheelDelta = 120;  // WHEEL_DELTA: one detent
constexpr int kMaxSteps = 200;    // per call, like the X11 and uinput outputs (flood/overflow guard)

inline int clampSteps(int n) { return n > kMaxSteps ? kMaxSteps : (n < -kMaxSteps ? -kMaxSteps : n); }

// MOUSEINPUT::mouseData for ONE detent of Output::wheelV(n). Output convention: n > 0 = down
// (the content scrolls up). Windows: a positive wheel delta = forward, away from the user = up.
// So the sign flips. Returns 0 for n == 0.
inline int32_t wheelDetentV(int n) { return n > 0 ? -kWheelDelta : (n < 0 ? kWheelDelta : 0); }
// Output::wheelH(n): n > 0 = right; MOUSEEVENTF_HWHEEL: positive = right. Same sign.
inline int32_t wheelDetentH(int n) { return n > 0 ? kWheelDelta : (n < 0 ? -kWheelDelta : 0); }
// Number of detents (events) for a call: |n| saturated to kMaxSteps.
inline int wheelCount(int n) {
    const int c = clampSteps(n);
    return c < 0 ? -c : c;
}
// Total wheel movement of a call in WHEEL_DELTA units (signed, Windows convention).
inline int32_t wheelTotalV(int n) { return -clampSteps(n) * kWheelDelta; }
inline int32_t wheelTotalH(int n) { return clampSteps(n) * kWheelDelta; }

constexpr uint32_t kMouseEventWheel = 0x0800;   // MOUSEEVENTF_WHEEL
constexpr uint32_t kMouseEventHWheel = 0x1000;  // MOUSEEVENTF_HWHEEL

// ---- mouse buttons (Output::mouseButton: 1 = left, 2 = middle, 3 = right) ----------------------

struct MouseButtonFlags {
    uint32_t down = 0;
    uint32_t up = 0;  // both 0 = invalid button
};
inline MouseButtonFlags mouseButtonFlags(int button) {
    switch (button) {
    case 1: return {0x0002, 0x0004};  // MOUSEEVENTF_LEFTDOWN / LEFTUP
    case 2: return {0x0020, 0x0040};  // MOUSEEVENTF_MIDDLEDOWN / MIDDLEUP
    case 3: return {0x0008, 0x0010};  // MOUSEEVENTF_RIGHTDOWN / RIGHTUP
    default: return {};
    }
}

}  // namespace winkeys
}  // namespace spn
