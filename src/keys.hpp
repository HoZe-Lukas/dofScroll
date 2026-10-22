// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Named keys for output and mode files (modes.hpp). Single source of truth: identifier (enum)
// and name in config/mode files. The mapping to X11 keysyms or Linux KEY_* codes lives in each
// backend (output_x11.cpp, output_uinput.cpp) and MUST cover every entry (static_assert on
// kKeyCount).
// The order of the first five entries is fixed (existing code indexes with it).
#pragma once
#include <cstddef>
#include <string>

namespace spn {

#define SPN_KEY_LIST(X)                                                                        \
    X(Ctrl, "ctrl") X(Up, "up") X(Down, "down") X(Left, "left") X(Right, "right")              \
    X(Shift, "shift") X(Alt, "alt") X(Super, "super") X(Space, "space") X(Enter, "enter")      \
    X(Esc, "esc") X(Tab, "tab") X(Backspace, "backspace") X(Delete, "delete")                  \
    X(Insert, "insert") X(Home, "home") X(End, "end") X(PageUp, "pageup")                      \
    X(PageDown, "pagedown") X(Minus, "minus") X(Plus, "plus")                                  \
    X(KpPlus, "kp_plus") X(KpMinus, "kp_minus")                                                \
    X(F1, "f1") X(F2, "f2") X(F3, "f3") X(F4, "f4") X(F5, "f5") X(F6, "f6") X(F7, "f7")        \
    X(F8, "f8") X(F9, "f9") X(F10, "f10") X(F11, "f11") X(F12, "f12")                          \
    X(F13, "f13") X(F14, "f14") X(F15, "f15") X(F16, "f16") X(F17, "f17") X(F18, "f18")        \
    X(F19, "f19") X(F20, "f20") X(F21, "f21") X(F22, "f22") X(F23, "f23") X(F24, "f24")        \
    X(A, "a") X(B, "b") X(C, "c") X(D, "d") X(E, "e") X(F, "f") X(G, "g") X(H, "h") X(I, "i")  \
    X(J, "j") X(K, "k") X(L, "l") X(M, "m") X(N, "n") X(O, "o") X(P, "p") X(Q, "q") X(R, "r")  \
    X(S, "s") X(T, "t") X(U, "u") X(V, "v") X(W, "w") X(X, "x") X(Y, "y") X(Z, "z")            \
    X(N0, "0") X(N1, "1") X(N2, "2") X(N3, "3") X(N4, "4") X(N5, "5") X(N6, "6")              \
    X(N7, "7") X(N8, "8") X(N9, "9") \
    X(Kp0, "kp_0") X(Kp1, "kp_1") X(Kp2, "kp_2") X(Kp3, "kp_3") X(Kp4, "kp_4") X(Kp5, "kp_5") \
    X(Kp6, "kp_6") X(Kp7, "kp_7") X(Kp8, "kp_8") X(Kp9, "kp_9") X(KpEnter, "kp_enter") X(KpMultiply, "kp_multiply") \
    X(KpDivide, "kp_divide") X(KpDecimal, "kp_decimal") X(Comma, "comma") X(Period, "period") X(Slash, "slash") X(Backslash, "backslash") \
    X(Semicolon, "semicolon") X(Apostrophe, "apostrophe") X(BracketLeft, "bracket_left") X(BracketRight, "bracket_right") X(Grave, "grave") X(Equal, "equal") \
    X(CapsLock, "caps_lock") X(Print, "print") X(ScrollLock, "scroll_lock") X(Pause, "pause") X(Menu, "menu") X(NumLock, "num_lock") \
    X(PlayPause, "play_pause") X(MediaNext, "media_next") X(MediaPrev, "media_prev") X(MediaStop, "media_stop") X(VolumeUp, "volume_up") X(VolumeDown, "volume_down") \
    X(VolumeMute, "volume_mute")

enum class Key {
#define X(id, name) id,
    SPN_KEY_LIST(X)
#undef X
    Count_  // only for kKeyCount
};
constexpr size_t kKeyCount = static_cast<size_t>(Key::Count_);

inline const char *keyName(Key k) {
    static const char *const names[] = {
#define X(id, name) name,
        SPN_KEY_LIST(X)
#undef X
    };
    return names[static_cast<size_t>(k)];
}

// Name (lower case, trimmed) -> Key. false if unknown.
inline bool parseKeyName(const std::string &name, Key *out) {
    for (size_t i = 0; i < kKeyCount; ++i) {
        if (name == keyName(static_cast<Key>(i))) {
            *out = static_cast<Key>(i);
            return true;
        }
    }
    return false;
}

}  // namespace spn
