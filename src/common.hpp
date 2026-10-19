// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Gemeinsame Grundtypen fuer Daemon, Tray und Tests.
#pragma once
#include <array>
#include <cstddef>
#include <cstdlib>
#include <string>

namespace spn {

// Die sechs Rohachsen einer SpaceMouse (libspnav-Konvention: x rechts, y oben,
// z zum Nutzer; rx/ry/rz = Drehung um die jeweilige Achse). Mit axdiag an einer
// SpaceMouse Pro gemessen: Schieben vor/zurueck = z, seitlich = x, Heben = y;
// Kippen vor/zurueck = rx, Kippen seitlich = rz, Drehen der Kappe = ry.
enum Axis { AX_X = 0, AX_Y, AX_Z, AX_RX, AX_RY, AX_RZ, AX_COUNT };

using RawAxes = std::array<int, AX_COUNT>;

inline const char *axisName(Axis a) {
    static const char *n[AX_COUNT] = {"x", "y", "z", "rx", "ry", "rz"};
    return n[a];
}

// Heartbeat-Datei: Der Tray beruehrt sie (~alle 150 ms), solange er die
// Bewegungen der Maus selbst auswertet (Reiter "Achsen" im aktiven Fenster,
// Achsen-Assistent). Der Daemon sendet dann weder Scroll, Zoom, Pfeiltasten noch
// Klicks. Liegt im privaten XDG_RUNTIME_DIR; ohne diese Variable gibt es keine
// Unterdrueckung. Sicherheit: der Daemon ignoriert den Mechanismus, wenn
// XDG_RUNTIME_DIR nicht dem effektiven Benutzer gehoert bzw. group/other-
// schreibbar ist, oder wenn die Datei fremd gehoert / Gruppen-/Other-Rechte
// hat (der Tray legt sie mit 0600 an). Veraltet die Datei (Tray abgestuerzt), endet sie von selbst.
inline std::string suspendFilePath() {
    const char *d = std::getenv("XDG_RUNTIME_DIR");
    if (!d || !*d) return "";
    return std::string(d) + "/dofscroll-suspend";
}
constexpr long SUSPEND_MAX_AGE_MS = 600;

// Laufzeitdateien im privaten XDG_RUNTIME_DIR (leer, wenn die Variable fehlt). Der Daemon
// schreibt sie, der Tray liest sie. Beide Seiten pruefen wie bei der Suspend-Datei Besitzer
// und Rechte (0600, regulaere Datei, dem effektiven Benutzer gehoerig), O_NOFOLLOW.
//   dofscroll-status  Zeilen "key=value" (UTF-8, ohne Steuerzeichen, atomar ersetzt):
//                          profile=<match_desc>  device=<Name>  app=<WM_CLASS>
//                          enabled=0|1  paused=0|1  devices_seen=<Anzahl USB-3Dconnexion-Geraete>
//                          updated=<unix-Sekunden>
//   dofscroll.log     Kopie der Daemon-Logzeilen (stderr), rollend: hoechstens
//                          kLogMaxLines Zeilen (im --debug-Modus kLogMaxLinesDebug), danach
//                          bleiben nur die neuesten kLogKeepLines; zusaetzlich Obergrenze kLogMaxBytes.
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
