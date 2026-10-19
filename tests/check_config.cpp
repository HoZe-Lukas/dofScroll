// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Small CLI: validates a configuration file with loadProfiles().
// Usage: check_config FILE   -> exit 0 = ok, 1 = warning/error, 2 = usage.
#include <iostream>
#include <string>
#include <vector>
#include "config.hpp"

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "Usage: check_config FILE\n";
        return 2;
    }
    const std::string path = argv[1];
    spn::ProfileSet ps;
    std::vector<std::string> warnings;
    if (!spn::loadProfiles(path, &ps, &warnings)) {
        std::cerr << "ERROR: '" << path << "' could not be loaded\n";
        return 1;
    }
    for (const auto &w : warnings) std::cerr << "WARNING: " << w << "\n";
    if (!warnings.empty()) return 1;
    std::cout << "OK: " << path << " (" << ps.sections.size() << " sections)\n";
    return 0;
}
