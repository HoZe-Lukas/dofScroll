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
    X(A, "a") X(B, "b") X(C, "c") X(D, "d") X(E, "e") X(F, "f") X(G, "g") X(H, "h") X(I, "i")  \
    X(J, "j") X(K, "k") X(L, "l") X(M, "m") X(N, "n") X(O, "o") X(P, "p") X(Q, "q") X(R, "r")  \
    X(S, "s") X(T, "t") X(U, "u") X(V, "v") X(W, "w") X(X, "x") X(Y, "y") X(Z, "z")            \
    X(N0, "0") X(N1, "1") X(N2, "2") X(N3, "3") X(N4, "4") X(N5, "5") X(N6, "6")              \
    X(N7, "7") X(N8, "8") X(N9, "9")

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
