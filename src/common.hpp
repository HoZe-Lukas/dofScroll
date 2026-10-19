// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Shared basic types for daemon, tray and tests.
#pragma once
#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace spn {

// The six raw axes of a SpaceMouse (libspnav convention: x right, y up, z towards the
// user; rx/ry/rz = rotation about the respective axis). Measured with axdiag on a
// SpaceMouse Pro: push forward/back = z, sideways = x, lift = y; tilt forward/back = rx,
// tilt sideways = rz, twisting the cap = ry.
enum Axis { AX_X = 0, AX_Y, AX_Z, AX_RX, AX_RY, AX_RZ, AX_COUNT };

using RawAxes = std::array<int, AX_COUNT>;

inline const char *axisName(Axis a) {
    static const char *n[AX_COUNT] = {"x", "y", "z", "rx", "ry", "rz"};
    return n[a];
}

// Heartbeat file: the tray touches it (~every 150 ms) while it evaluates the mouse
// motion itself (tab "Axes" in the active window, axis wizard). The daemon then sends
// neither scroll, zoom, arrow keys nor clicks. Lives in the private XDG_RUNTIME_DIR;
// without this variable there is no suppression. Security: the daemon ignores the
// mechanism if XDG_RUNTIME_DIR does not belong to the effective user or is
// group/other-writable, or if the file belongs to someone else / has group/other
// permissions (the tray creates it with 0600). If the file goes stale (tray crashed),
// it ends by itself.
inline std::string suspendFilePath() {
    const char *d = std::getenv("XDG_RUNTIME_DIR");
    if (!d || !*d) return "";
    return std::string(d) + "/dofscroll-suspend";
}
constexpr long SUSPEND_MAX_AGE_MS = 600;

// Runtime files in the private XDG_RUNTIME_DIR (empty if the variable is missing). The
// daemon writes them, the tray reads them. As with the suspend file, both sides check
// owner and permissions (0600, regular file, owned by the effective user), O_NOFOLLOW.
//   dofscroll-status  lines "key=value" (UTF-8, no control characters, replaced atomically):
//                          profile=<match_desc>  device=<name>  app=<WM_CLASS>
//                          mode=<id of the active mode, e.g. normal, off, timeline>
//                          enabled=0|1  paused=0|1  devices_seen=<number of USB 3Dconnexion devices>
//                          updated=<unix seconds>
//   dofscroll.log     copy of the daemon log lines (stderr), rolling: at most
//                          kLogMaxLines lines (kLogMaxLinesDebug in --debug mode), after which
//                          only the newest kLogKeepLines remain; additionally capped at kLogMaxBytes.
inline std::string runtimeFilePath(const char *name) {
    const char *d = std::getenv("XDG_RUNTIME_DIR");
    if (!d || !*d) return "";
    return std::string(d) + "/" + name;
}
inline std::string statusFilePath() { return runtimeFilePath("dofscroll-status"); }
inline std::string logFilePath() { return runtimeFilePath("dofscroll.log"); }
constexpr size_t kLogMaxBytes = 256 * 1024;
constexpr size_t kLogMaxLines = 300, kLogKeepLines = 200;
constexpr size_t kLogMaxLinesDebug = 2000, kLogKeepLinesDebug = 1500;
constexpr size_t kStatusMaxBytes = 4096;

}  // namespace spn
