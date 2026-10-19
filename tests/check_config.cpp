// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Kleines CLI: validiert eine Konfigurationsdatei mit loadProfiles().
// Aufruf: check_config DATEI   -> Exit 0 = ok, 1 = Warnung/Fehler, 2 = Aufruf.
#include <iostream>
#include <string>
#include <vector>
#include "config.hpp"

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Aufruf: check_config DATEI\n";
        return 2;
    }
    const std::string path = argv[1];
    spn::ProfileSet ps;
    std::vector<std::string> warnings;
    if (!spn::loadProfiles(path, &ps, &warnings)) {
        std::cerr << "FEHLER: '" << path << "' konnte nicht geladen werden\n";
        return 1;
    }
    for (const auto &w : warnings) std::cerr << "WARNUNG: " << w << "\n";
    if (!warnings.empty()) return 1;
    std::cout << "OK: " << path << " (" << ps.sections.size() << " Sektionen)\n";
    return 0;
}
