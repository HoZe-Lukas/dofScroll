// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Key -> macOS virtual key code, modifier flags, scroll and mouse-button constants for the CGEvent
// output (mac_output.cpp). Pure header: no Apple headers, so it is unit-tested on every OS
// (tests/test_mac_key_tables.cpp); mac_output.cpp static_asserts that the numeric constants below
// equal the Apple definitions.
//
// The key codes are the ANSI (US layout) positions from Carbon HIToolbox/Events.h (kVK_ANSI_*,
// kVK_Return, ...). macOS key codes identify physical key positions, not characters, so letters and
// digits follow the US layout positions on other layouts as well (same as the uinput backend on
// Linux). The switch has no default, so -Wswitch flags a missing entry.
#pragma once

#include <cstdint>

#include "../../keys.hpp"

namespace spn {
namespace mac {

// Upper bound for steps per call (protects against INT_MIN negation and event floods), as in the
// X11 and uinput outputs.
constexpr int kMaxSteps = 200;
inline int clampSteps(int n) { return n > kMaxSteps ? kMaxSteps : (n < -kMaxSteps ? -kMaxSteps : n); }

// kVK_* code of a key (0xFFFF = not available on macOS: F21 to F24, which Apple has no key codes for; the output skips them).
inline uint16_t keyToKeycode(Key k) {
    switch (k) {
    case Key::Ctrl: return 0x3B;       // kVK_Control
    case Key::Up: return 0x7E;         // kVK_UpArrow
    case Key::Down: return 0x7D;       // kVK_DownArrow
    case Key::Left: return 0x7B;       // kVK_LeftArrow
    case Key::Right: return 0x7C;      // kVK_RightArrow
    case Key::Shift: return 0x38;      // kVK_Shift
    case Key::Alt: return 0x3A;        // kVK_Option
    case Key::Super: return 0x37;      // kVK_Command
    case Key::Space: return 0x31;      // kVK_Space
    case Key::Enter: return 0x24;      // kVK_Return (the keypad Enter is kVK_ANSI_KeypadEnter 0x4C)
    case Key::Esc: return 0x35;        // kVK_Escape
    case Key::Tab: return 0x30;        // kVK_Tab
    case Key::Backspace: return 0x33;  // kVK_Delete (the backspace key)
    case Key::Delete: return 0x75;     // kVK_ForwardDelete
    case Key::Insert: return 0x72;     // kVK_Help (Apple keyboards have no Insert key; Help sits there)
    case Key::Home: return 0x73;       // kVK_Home
    case Key::End: return 0x77;        // kVK_End
    case Key::PageUp: return 0x74;     // kVK_PageUp
    case Key::PageDown: return 0x79;   // kVK_PageDown
    case Key::Minus: return 0x1B;      // kVK_ANSI_Minus
    case Key::Plus: return 0x18;       // kVK_ANSI_Equal, sent with Shift, see keyNeedsShift()
    case Key::KpPlus: return 0x45;     // kVK_ANSI_KeypadPlus
    case Key::KpMinus: return 0x4E;    // kVK_ANSI_KeypadMinus
    case Key::F1: return 0x7A;         // kVK_F1
    case Key::F2: return 0x78;         // kVK_F2
    case Key::F3: return 0x63;         // kVK_F3
    case Key::F4: return 0x76;         // kVK_F4
    case Key::F5: return 0x60;         // kVK_F5
    case Key::F6: return 0x61;         // kVK_F6
    case Key::F7: return 0x62;         // kVK_F7
    case Key::F8: return 0x64;         // kVK_F8
    case Key::F9: return 0x65;         // kVK_F9
    case Key::F10: return 0x6D;        // kVK_F10
    case Key::F11: return 0x67;        // kVK_F11
    case Key::F12: return 0x6F;        // kVK_F12
    case Key::F13: return 0x69;         // kVK_F13
    case Key::F14: return 0x6B;         // kVK_F14
    case Key::F15: return 0x71;         // kVK_F15
    case Key::F16: return 0x6A;         // kVK_F16
    case Key::F17: return 0x40;         // kVK_F17
    case Key::F18: return 0x4F;         // kVK_F18
    case Key::F19: return 0x50;         // kVK_F19
    case Key::F20: return 0x5A;         // kVK_F20
    // macOS has no virtual key codes above F20: these four are not sent there (0xFFFF, see below)
    case Key::F21: return 0xFFFF;
    case Key::F22: return 0xFFFF;
    case Key::F23: return 0xFFFF;
    case Key::F24: return 0xFFFF;
    case Key::A: return 0x00;          // kVK_ANSI_A
    case Key::B: return 0x0B;          // kVK_ANSI_B
    case Key::C: return 0x08;          // kVK_ANSI_C
    case Key::D: return 0x02;          // kVK_ANSI_D
    case Key::E: return 0x0E;          // kVK_ANSI_E
    case Key::F: return 0x03;          // kVK_ANSI_F
    case Key::G: return 0x05;          // kVK_ANSI_G
    case Key::H: return 0x04;          // kVK_ANSI_H
    case Key::I: return 0x22;          // kVK_ANSI_I
    case Key::J: return 0x26;          // kVK_ANSI_J
    case Key::K: return 0x28;          // kVK_ANSI_K
    case Key::L: return 0x25;          // kVK_ANSI_L
    case Key::M: return 0x2E;          // kVK_ANSI_M
    case Key::N: return 0x2D;          // kVK_ANSI_N
    case Key::O: return 0x1F;          // kVK_ANSI_O
    case Key::P: return 0x23;          // kVK_ANSI_P
    case Key::Q: return 0x0C;          // kVK_ANSI_Q
    case Key::R: return 0x0F;          // kVK_ANSI_R
    case Key::S: return 0x01;          // kVK_ANSI_S
    case Key::T: return 0x11;          // kVK_ANSI_T
    case Key::U: return 0x20;          // kVK_ANSI_U
    case Key::V: return 0x09;          // kVK_ANSI_V
    case Key::W: return 0x0D;          // kVK_ANSI_W
    case Key::X: return 0x07;          // kVK_ANSI_X
    case Key::Y: return 0x10;          // kVK_ANSI_Y
    case Key::Z: return 0x06;          // kVK_ANSI_Z
    case Key::N0: return 0x1D;         // kVK_ANSI_0
    case Key::N1: return 0x12;         // kVK_ANSI_1
    case Key::N2: return 0x13;         // kVK_ANSI_2
    case Key::N3: return 0x14;         // kVK_ANSI_3
    case Key::N4: return 0x15;         // kVK_ANSI_4
    case Key::N5: return 0x17;         // kVK_ANSI_5
    case Key::N6: return 0x16;         // kVK_ANSI_6
    case Key::N7: return 0x1A;         // kVK_ANSI_7
    case Key::N8: return 0x1C;         // kVK_ANSI_8
    case Key::N9: return 0x19;         // kVK_ANSI_9
    case Key::Kp0: return 0x52;
    case Key::Kp1: return 0x53;
    case Key::Kp2: return 0x54;
    case Key::Kp3: return 0x55;
    case Key::Kp4: return 0x56;
    case Key::Kp5: return 0x57;
    case Key::Kp6: return 0x58;
    case Key::Kp7: return 0x59;
    case Key::Kp8: return 0x5B;
    case Key::Kp9: return 0x5C;
    case Key::KpEnter: return 0x4C;
    case Key::KpMultiply: return 0x43;
    case Key::KpDivide: return 0x4B;
    case Key::KpDecimal: return 0x41;
    case Key::Comma: return 0x2B;
    case Key::Period: return 0x2F;
    case Key::Slash: return 0x2C;
    case Key::Backslash: return 0x2A;
    case Key::Semicolon: return 0x29;
    case Key::Apostrophe: return 0x27;
    case Key::BracketLeft: return 0x21;
    case Key::BracketRight: return 0x1E;
    case Key::Grave: return 0x32;
    case Key::Equal: return 0x18;
    case Key::CapsLock: return 0x39;
    case Key::Print: return 0xFFFF;  // no key code on macOS
    case Key::ScrollLock: return 0xFFFF;  // no key code on macOS
    case Key::Pause: return 0xFFFF;  // no key code on macOS
    case Key::Menu: return 0x6E;
    case Key::NumLock: return 0xFFFF;  // no key code on macOS
    case Key::PlayPause: return 0xFFFF;  // no key code on macOS
    case Key::MediaNext: return 0xFFFF;  // no key code on macOS
    case Key::MediaPrev: return 0xFFFF;  // no key code on macOS
    case Key::MediaStop: return 0xFFFF;  // no key code on macOS
    case Key::VolumeUp: return 0xFFFF;  // no key code on macOS
    case Key::VolumeDown: return 0xFFFF;  // no key code on macOS
    case Key::VolumeMute: return 0xFFFF;  // no key code on macOS
    case Key::Count_: break;
    }
    return 0xFFFF;
}

// Right-hand modifier key codes: not in the Key table, but cleared by forceReleaseAll() in case an
// earlier process left them stuck.
constexpr uint16_t kExtraModifierKeycodes[4] = {
    0x3C,  // kVK_RightShift
    0x3E,  // kVK_RightControl
    0x3D,  // kVK_RightOption
    0x36,  // kVK_RightCommand
};

// True if the key must be sent with Shift (US layout assumption, like the uinput backend).
inline bool keyNeedsShift(Key k) { return k == Key::Plus; }

// CGEventFlags bits (kCGEventFlagMask*). Synthetic events do not inherit the physical modifier
// state, so every posted event carries the flags of the modifier keys we hold.
constexpr uint64_t kFlagShift = 0x00020000;    // kCGEventFlagMaskShift
constexpr uint64_t kFlagControl = 0x00040000;  // kCGEventFlagMaskControl
constexpr uint64_t kFlagAlt = 0x00080000;      // kCGEventFlagMaskAlternate (Option)
constexpr uint64_t kFlagCommand = 0x00100000;  // kCGEventFlagMaskCommand

// Flag bit of a modifier key (0 for every other key).
inline uint64_t keyModifierFlag(Key k) {
    switch (k) {
    case Key::Ctrl: return kFlagControl;
    case Key::Shift: return kFlagShift;
    case Key::Alt: return kFlagAlt;
    case Key::Super: return kFlagCommand;
    default: return 0;
    }
}

// ---- scroll ------------------------------------------------------------------------------------
// Output::wheelV: positive = down (content moves up). CGEventCreateScrollWheelEvent wheel1:
// positive = scroll up (like the physical wheel rolled away from the user), so the sign flips.
// Output::wheelH: positive = right. Assumption (UNVERIFIED on hardware): wheel2 follows the
// NSEvent deltaX convention, where positive means the content moves right, i.e. the user scrolls
// left, so the sign flips here as well. If scrolling sideways turns out reversed on a Mac, change
// only this function (and the test).
// kCGScrollEventUnitLine is used: one detent step = one line.
inline int32_t scrollWheelValue(int steps) { return -static_cast<int32_t>(clampSteps(steps)); }

// ---- mouse buttons -----------------------------------------------------------------------------
// Output buttons: 1 = left, 2 = middle, 3 = right.
constexpr int kCGMouseButtonLeft = 0;    // kCGMouseButtonLeft
constexpr int kCGMouseButtonRight = 1;   // kCGMouseButtonRight
constexpr int kCGMouseButtonCenter = 2;  // kCGMouseButtonCenter
// CGEventType values
constexpr int kEvLeftMouseDown = 1;    // kCGEventLeftMouseDown
constexpr int kEvLeftMouseUp = 2;      // kCGEventLeftMouseUp
constexpr int kEvRightMouseDown = 3;   // kCGEventRightMouseDown
constexpr int kEvRightMouseUp = 4;     // kCGEventRightMouseUp
constexpr int kEvOtherMouseDown = 25;  // kCGEventOtherMouseDown
constexpr int kEvOtherMouseUp = 26;    // kCGEventOtherMouseUp

struct MouseButtonSpec {
    int cgButton;  // CGMouseButton
    int downType;  // CGEventType
    int upType;
};

// false for anything but 1..3.
inline bool mouseButtonSpec(int button, MouseButtonSpec *out) {
    switch (button) {
    case 1: *out = {kCGMouseButtonLeft, kEvLeftMouseDown, kEvLeftMouseUp}; return true;
    case 2: *out = {kCGMouseButtonCenter, kEvOtherMouseDown, kEvOtherMouseUp}; return true;
    case 3: *out = {kCGMouseButtonRight, kEvRightMouseDown, kEvRightMouseUp}; return true;
    default: return false;
    }
}

}  // namespace mac
}  // namespace spn
