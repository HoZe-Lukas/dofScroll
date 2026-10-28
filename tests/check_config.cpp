// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Small CLI: validates a configuration file with loadProfiles(), a configuration directory
// (dofscroll.conf, devices/, apps/) with loadProfileTree(), or a directory of mode files with loadModes().
// Usage: check_config FILE | check_config DIR | check_config --modes DIR
//   exit 0 = ok, 1 = warning/error, 2 = usage.
#include <iostream>
#include <string>
#include <vector>
#include "config.hpp"
#include "log_sanitize.hpp"
#include "modes.hpp"
#include "platform/fs.hpp"
#include "profile_files.hpp"

int main(int argc, char **argv) {
    if (argc == 3 && std::string(argv[1]) == "--modes") {
        // Every warning of the loader (unknown key, bad action, bad value of a per-part key, ...) fails.
        std::vector<std::string> warnings;
        const spn::ModeSet set = spn::loadModes(argv[2], &warnings);
        for (const auto &w : warnings) std::cerr << "WARNING: " << spn::sanitizeForLog(w, 512) << "\n";
        if (!warnings.empty()) return 1;
        std::cout << "OK: " << argv[2] << " (" << set.modes.size() << " modes)\n";
        return 0;
    }
    if (argc != 2) {
        std::cerr << "Usage: check_config FILE | check_config DIR | check_config --modes DIR\n";
        return 2;
    }
    const std::string path = argv[1];
    spn::ProfileSet ps;
    std::vector<std::string> warnings;
    spn::fs::FileInfo fi;
    if (spn::fs::statPath(path, &fi) && fi.isDir) {
        // Every warning and every defective file fails, like for a single file.
        std::vector<spn::FileDiag> diag;
        if (!spn::loadProfileTree(path, &ps, &diag)) {
            for (const auto &d : diag)
                for (const auto &w : d.warnings) std::cerr << "ERROR: " << spn::sanitizeForLog(w, 512) << "\n";
            return 1;
        }
        bool bad = false;
        for (const auto &d : diag) {
            for (const auto &w : d.warnings) std::cerr << "WARNING: " << spn::sanitizeForLog(w, 512) << "\n";
            if (d.fatal || !d.warnings.empty()) bad = true;
        }
        if (bad) return 1;
        std::cout << "OK: " << path << " (" << ps.sections.size() << " sections)\n";
        return 0;
    }
    if (!spn::loadProfiles(path, &ps, &warnings)) {
        std::cerr << "ERROR: '" << path << "' could not be loaded\n";
        return 1;
    }
    for (const auto &w : warnings) std::cerr << "WARNING: " << spn::sanitizeForLog(w, 512) << "\n";
    if (!warnings.empty()) return 1;
    std::cout << "OK: " << path << " (" << ps.sections.size() << " sections)\n";
    return 0;
}
