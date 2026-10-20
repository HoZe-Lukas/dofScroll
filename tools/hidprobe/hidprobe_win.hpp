// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows counterpart of hidprobe_posix.hpp: the "press Enter" check of the guided mode.
#pragma once
#include <conio.h>

namespace hidprobe {

// True if a key was pressed in the console (any key counts as Enter); consumes the pending keys.
// Without a console (redirected input) it is always false.
inline bool stdinLineReady() {
    if (!_kbhit()) return false;
    while (_kbhit()) (void)_getch();
    return true;
}

}  // namespace hidprobe
