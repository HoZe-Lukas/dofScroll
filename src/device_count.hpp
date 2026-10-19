// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Zaehlt angesteckte 3Dconnexion-Geraete (spacenavd meldet bei mehreren nur eines).
#pragma once
#include <string>

namespace spn {

// Rein parsend (unit-getestet): zaehlt Eintraege "N: Name=\"...\"" im Format von
// /proc/bus/input/devices, deren Name "3Dconnexion" enthaelt (Gross-/Kleinschreibung
// egal). Zusatz-Eingabegeraete desselben Herstellers (Tastatur-/Consumer-/System-
// Control-Knoten) zaehlen nicht mit.
int countSpaceMiceInText(const std::string &devicesText);

// Liest /proc/bus/input/devices (in der Sandbox der Unit lesbar, kein /sys noetig,
// begrenzt auf 256 KiB) und zaehlt. Rueckgabe -1, wenn die Datei nicht lesbar ist.
int countSpaceMice();

}  // namespace spn
