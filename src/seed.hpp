// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Erststart-Seeding: Beispiel-Konfiguration (examples/default/dofscroll.conf) und
// Beispiel-Modi (examples/modes/*.conf) in den Konfigurationsordner uebernehmen, wenn dort
// noch keine Config existiert. Wird vom Tray beim Start aufgerufen (der Daemon hat keinen
// Schreibzugriff auf den Konfigurationsordner).
#pragma once
#include <string>

namespace spn {

// true, wenn etwas angelegt wurde (*log = einzeilige Beschreibung, darf nullptr sein).
// Ueberschreibt nie eine vorhandene Datei. Siehe seed.cpp fuer die Suchpfade.
bool seedDefaults(std::string *log);

}  // namespace spn
