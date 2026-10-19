// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#pragma once

namespace spn {

// Starts a thread that releases all held keys and buttons (macReleaseGuard()) right before the
// system goes to sleep, so nothing stays pressed across a suspend. Compile-only on CI.
void startSleepWatcher();

}  // namespace spn
