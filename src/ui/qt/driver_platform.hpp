// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// DOFSCROLL_DRIVER_UI is defined where dofscroll reads the SpaceMouse through the driver's interface or
// directly (Windows, macOS): the "Input path" control and the 3Dconnexion note of the About box exist
// there only. On Linux spacenavd is the driver and INPUT is ignored. Building with
// -DDOFSCROLL_TEST_DRIVER_UI shows the Windows/macOS variant on Linux so that the self-test can drive it.
#pragma once
#if defined(_WIN32) || defined(__APPLE__) || defined(DOFSCROLL_TEST_DRIVER_UI)
#define DOFSCROLL_DRIVER_UI 1
#endif
