// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Every built-in joystick mode and button mode explains itself in plain words: a description that is long
// enough to say what it does and for which program, and short enough to be shown whole (the interface
// shows at most 256 bytes).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "button_modes.hpp"
#include "modes.hpp"

using namespace spn;

int main() {
    constexpr size_t kMin = 60, kMax = 256;
    int n = 0;
    for (const ModeDef &m : loadModes("", nullptr).modes) {
        if (!m.builtin) continue;
        ++n;
        if (m.description.size() < kMin || m.description.size() > kMax) {
            std::fprintf(stderr, "joystick mode %s: description of %zu bytes (want %zu to %zu)\n", m.id.c_str(), m.description.size(), kMin, kMax);
            return 1;
        }
    }
    for (const ButtonMode &b : loadButtonModes("", nullptr).modes) {
        if (!b.builtin) continue;
        ++n;
        if (b.description.size() < kMin || b.description.size() > kMax) {
            std::fprintf(stderr, "button mode %s: description of %zu bytes (want %zu to %zu)\n", b.id.c_str(), b.description.size(), kMin, kMax);
            return 1;
        }
    }
    assert(n >= 21);  // 16 joystick modes and 5 button modes
    std::puts("built-in descriptions ok");
    return 0;
}
