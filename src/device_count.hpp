// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Counts connected 3Dconnexion devices (spacenavd reports only one when several are present).
#pragma once
#include <string>

namespace spn {

// Pure parsing (unit-tested): counts entries "N: Name=\"...\"" in the format of
// /proc/bus/input/devices whose name contains "3Dconnexion" (case-insensitive).
// Additional input devices of the same vendor (keyboard/consumer/system-control
// nodes) are not counted.
int countSpaceMiceInText(const std::string &devicesText);

// Reads /proc/bus/input/devices (readable in the unit's sandbox, no /sys needed,
// limited to 256 KiB) and counts. Returns -1 if the file is not readable.
int countSpaceMice();

}  // namespace spn
