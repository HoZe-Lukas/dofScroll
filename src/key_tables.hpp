// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Key -> X11 keysym / Linux KEY_* code tables. Header-only and free of any X11 or
// uinput calls, so they can be unit-tested without a display (tests/test_keys.cpp).
// The switches list every Key without a default, so -Wswitch flags a missing entry.
// "plus" and "minus" are the main-keyboard keys; "kp_plus" / "kp_minus" are the keypad keys.
// "plus" is the keysym XK_plus: the X11 backend resolves it against the current keyboard map
// (adding Shift when needed). The uinput backend has no layout information and sends
// Shift+KEY_EQUAL (US layout), see keyNeedsShift().
#pragma once
#include <X11/keysym.h>
#include <linux/input-event-codes.h>

#include "keys.hpp"

namespace spn {

// XF86 media keysyms (X11/XF86keysym.h) as numbers, so that no further header is needed.

// X11 keysym of a key (0 = unmapped; never the case for a valid Key).
inline unsigned long keyToKeysym(Key k) {
    switch (k) {
    case Key::Ctrl: return XK_Control_L;
    case Key::Up: return XK_Up;
    case Key::Down: return XK_Down;
    case Key::Left: return XK_Left;
    case Key::Right: return XK_Right;
    case Key::Shift: return XK_Shift_L;
    case Key::Alt: return XK_Alt_L;
    case Key::Super: return XK_Super_L;
    case Key::Space: return XK_space;
    case Key::Enter: return XK_Return;
    case Key::Esc: return XK_Escape;
    case Key::Tab: return XK_Tab;
    case Key::Backspace: return XK_BackSpace;
    case Key::Delete: return XK_Delete;
    case Key::Insert: return XK_Insert;
    case Key::Home: return XK_Home;
    case Key::End: return XK_End;
    case Key::PageUp: return XK_Page_Up;
    case Key::PageDown: return XK_Page_Down;
    case Key::Minus: return XK_minus;
    case Key::Plus: return XK_plus;
    case Key::KpPlus: return XK_KP_Add;
    case Key::KpMinus: return XK_KP_Subtract;
    case Key::F1: return XK_F1;
    case Key::F2: return XK_F2;
    case Key::F3: return XK_F3;
    case Key::F4: return XK_F4;
    case Key::F5: return XK_F5;
    case Key::F6: return XK_F6;
    case Key::F7: return XK_F7;
    case Key::F8: return XK_F8;
    case Key::F9: return XK_F9;
    case Key::F10: return XK_F10;
    case Key::F11: return XK_F11;
    case Key::F12: return XK_F12;
    case Key::F13: return XK_F13;
    case Key::F14: return XK_F14;
    case Key::F15: return XK_F15;
    case Key::F16: return XK_F16;
    case Key::F17: return XK_F17;
    case Key::F18: return XK_F18;
    case Key::F19: return XK_F19;
    case Key::F20: return XK_F20;
    case Key::F21: return XK_F21;
    case Key::F22: return XK_F22;
    case Key::F23: return XK_F23;
    case Key::F24: return XK_F24;
    case Key::A: return XK_a;
    case Key::B: return XK_b;
    case Key::C: return XK_c;
    case Key::D: return XK_d;
    case Key::E: return XK_e;
    case Key::F: return XK_f;
    case Key::G: return XK_g;
    case Key::H: return XK_h;
    case Key::I: return XK_i;
    case Key::J: return XK_j;
    case Key::K: return XK_k;
    case Key::L: return XK_l;
    case Key::M: return XK_m;
    case Key::N: return XK_n;
    case Key::O: return XK_o;
    case Key::P: return XK_p;
    case Key::Q: return XK_q;
    case Key::R: return XK_r;
    case Key::S: return XK_s;
    case Key::T: return XK_t;
    case Key::U: return XK_u;
    case Key::V: return XK_v;
    case Key::W: return XK_w;
    case Key::X: return XK_x;
    case Key::Y: return XK_y;
    case Key::Z: return XK_z;
    case Key::N0: return XK_0;
    case Key::N1: return XK_1;
    case Key::N2: return XK_2;
    case Key::N3: return XK_3;
    case Key::N4: return XK_4;
    case Key::N5: return XK_5;
    case Key::N6: return XK_6;
    case Key::N7: return XK_7;
    case Key::N8: return XK_8;
    case Key::N9: return XK_9;
    case Key::Kp0: return XK_KP_0;
    case Key::Kp1: return XK_KP_1;
    case Key::Kp2: return XK_KP_2;
    case Key::Kp3: return XK_KP_3;
    case Key::Kp4: return XK_KP_4;
    case Key::Kp5: return XK_KP_5;
    case Key::Kp6: return XK_KP_6;
    case Key::Kp7: return XK_KP_7;
    case Key::Kp8: return XK_KP_8;
    case Key::Kp9: return XK_KP_9;
    case Key::KpEnter: return XK_KP_Enter;
    case Key::KpMultiply: return XK_KP_Multiply;
    case Key::KpDivide: return XK_KP_Divide;
    case Key::KpDecimal: return XK_KP_Decimal;
    case Key::Comma: return XK_comma;
    case Key::Period: return XK_period;
    case Key::Slash: return XK_slash;
    case Key::Backslash: return XK_backslash;
    case Key::Semicolon: return XK_semicolon;
    case Key::Apostrophe: return XK_apostrophe;
    case Key::BracketLeft: return XK_bracketleft;
    case Key::BracketRight: return XK_bracketright;
    case Key::Grave: return XK_grave;
    case Key::Equal: return XK_equal;
    case Key::CapsLock: return XK_Caps_Lock;
    case Key::Print: return XK_Print;
    case Key::ScrollLock: return XK_Scroll_Lock;
    case Key::Pause: return XK_Pause;
    case Key::Menu: return XK_Menu;
    case Key::NumLock: return XK_Num_Lock;
    case Key::PlayPause: return 0x1008FF14UL;
    case Key::MediaNext: return 0x1008FF17UL;
    case Key::MediaPrev: return 0x1008FF16UL;
    case Key::MediaStop: return 0x1008FF15UL;
    case Key::VolumeUp: return 0x1008FF13UL;
    case Key::VolumeDown: return 0x1008FF11UL;
    case Key::VolumeMute: return 0x1008FF12UL;
    case Key::Count_: break;
    }
    return 0UL;
}

// Linux input event code of a key (0 = KEY_RESERVED = unmapped).
inline int keyToLinuxCode(Key k) {
    switch (k) {
    case Key::Ctrl: return KEY_LEFTCTRL;
    case Key::Up: return KEY_UP;
    case Key::Down: return KEY_DOWN;
    case Key::Left: return KEY_LEFT;
    case Key::Right: return KEY_RIGHT;
    case Key::Shift: return KEY_LEFTSHIFT;
    case Key::Alt: return KEY_LEFTALT;
    case Key::Super: return KEY_LEFTMETA;
    case Key::Space: return KEY_SPACE;
    case Key::Enter: return KEY_ENTER;
    case Key::Esc: return KEY_ESC;
    case Key::Tab: return KEY_TAB;
    case Key::Backspace: return KEY_BACKSPACE;
    case Key::Delete: return KEY_DELETE;
    case Key::Insert: return KEY_INSERT;
    case Key::Home: return KEY_HOME;
    case Key::End: return KEY_END;
    case Key::PageUp: return KEY_PAGEUP;
    case Key::PageDown: return KEY_PAGEDOWN;
    case Key::Minus: return KEY_MINUS;
    case Key::Plus: return KEY_EQUAL;  // with Shift, see keyNeedsShift()
    case Key::KpPlus: return KEY_KPPLUS;
    case Key::KpMinus: return KEY_KPMINUS;
    case Key::F1: return KEY_F1;
    case Key::F2: return KEY_F2;
    case Key::F3: return KEY_F3;
    case Key::F4: return KEY_F4;
    case Key::F5: return KEY_F5;
    case Key::F6: return KEY_F6;
    case Key::F7: return KEY_F7;
    case Key::F8: return KEY_F8;
    case Key::F9: return KEY_F9;
    case Key::F10: return KEY_F10;
    case Key::F11: return KEY_F11;
    case Key::F12: return KEY_F12;
    case Key::F13: return KEY_F13;
    case Key::F14: return KEY_F14;
    case Key::F15: return KEY_F15;
    case Key::F16: return KEY_F16;
    case Key::F17: return KEY_F17;
    case Key::F18: return KEY_F18;
    case Key::F19: return KEY_F19;
    case Key::F20: return KEY_F20;
    case Key::F21: return KEY_F21;
    case Key::F22: return KEY_F22;
    case Key::F23: return KEY_F23;
    case Key::F24: return KEY_F24;
    case Key::A: return KEY_A;
    case Key::B: return KEY_B;
    case Key::C: return KEY_C;
    case Key::D: return KEY_D;
    case Key::E: return KEY_E;
    case Key::F: return KEY_F;
    case Key::G: return KEY_G;
    case Key::H: return KEY_H;
    case Key::I: return KEY_I;
    case Key::J: return KEY_J;
    case Key::K: return KEY_K;
    case Key::L: return KEY_L;
    case Key::M: return KEY_M;
    case Key::N: return KEY_N;
    case Key::O: return KEY_O;
    case Key::P: return KEY_P;
    case Key::Q: return KEY_Q;
    case Key::R: return KEY_R;
    case Key::S: return KEY_S;
    case Key::T: return KEY_T;
    case Key::U: return KEY_U;
    case Key::V: return KEY_V;
    case Key::W: return KEY_W;
    case Key::X: return KEY_X;
    case Key::Y: return KEY_Y;
    case Key::Z: return KEY_Z;
    case Key::N0: return KEY_0;
    case Key::N1: return KEY_1;
    case Key::N2: return KEY_2;
    case Key::N3: return KEY_3;
    case Key::N4: return KEY_4;
    case Key::N5: return KEY_5;
    case Key::N6: return KEY_6;
    case Key::N7: return KEY_7;
    case Key::N8: return KEY_8;
    case Key::N9: return KEY_9;
    case Key::Kp0: return KEY_KP0;
    case Key::Kp1: return KEY_KP1;
    case Key::Kp2: return KEY_KP2;
    case Key::Kp3: return KEY_KP3;
    case Key::Kp4: return KEY_KP4;
    case Key::Kp5: return KEY_KP5;
    case Key::Kp6: return KEY_KP6;
    case Key::Kp7: return KEY_KP7;
    case Key::Kp8: return KEY_KP8;
    case Key::Kp9: return KEY_KP9;
    case Key::KpEnter: return KEY_KPENTER;
    case Key::KpMultiply: return KEY_KPASTERISK;
    case Key::KpDivide: return KEY_KPSLASH;
    case Key::KpDecimal: return KEY_KPDOT;
    case Key::Comma: return KEY_COMMA;
    case Key::Period: return KEY_DOT;
    case Key::Slash: return KEY_SLASH;
    case Key::Backslash: return KEY_BACKSLASH;
    case Key::Semicolon: return KEY_SEMICOLON;
    case Key::Apostrophe: return KEY_APOSTROPHE;
    case Key::BracketLeft: return KEY_LEFTBRACE;
    case Key::BracketRight: return KEY_RIGHTBRACE;
    case Key::Grave: return KEY_GRAVE;
    case Key::Equal: return KEY_EQUAL;
    case Key::CapsLock: return KEY_CAPSLOCK;
    case Key::Print: return KEY_SYSRQ;
    case Key::ScrollLock: return KEY_SCROLLLOCK;
    case Key::Pause: return KEY_PAUSE;
    case Key::Menu: return KEY_COMPOSE;
    case Key::NumLock: return KEY_NUMLOCK;
    case Key::PlayPause: return KEY_PLAYPAUSE;
    case Key::MediaNext: return KEY_NEXTSONG;
    case Key::MediaPrev: return KEY_PREVIOUSSONG;
    case Key::MediaStop: return KEY_STOPCD;
    case Key::VolumeUp: return KEY_VOLUMEUP;
    case Key::VolumeDown: return KEY_VOLUMEDOWN;
    case Key::VolumeMute: return KEY_MUTE;
    case Key::Count_: break;
    }
    return KEY_RESERVED;
}

// True if the Linux code of the key must be sent together with Shift (US layout assumption).
inline bool keyNeedsShift(Key k) { return k == Key::Plus; }

// Linux button code for mouse buttons 1/2/3 (0 for anything else).
inline int mouseButtonToLinuxCode(int button) {
    switch (button) {
    case 1: return BTN_LEFT;
    case 2: return BTN_MIDDLE;
    case 3: return BTN_RIGHT;
    default: return 0;
    }
}

}  // namespace spn
