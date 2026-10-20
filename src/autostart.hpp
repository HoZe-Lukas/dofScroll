// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Parsing of `systemctl --user is-enabled` output for the "Start at login" option of dofscroll-ui:
// pure logic, no GUI and no process spawning, so it compiles and is unit-tested on every OS
// (tests/test_autostart.cpp). The platform-specific autostart (units on Linux, Run key on
// Windows, LaunchAgent on macOS) lives in src/platform/autostart_os.hpp.
#pragma once

#include <string>
#include <vector>

namespace spn {
namespace autostart {

// State of a single unit as reported by `systemctl is-enabled`.
enum class Unit {
    Enabled,         // "enabled"
    EnabledRuntime,  // "enabled-runtime" (only until reboot, symlink in /run)
    Disabled,        // "disabled", "linked", "linked-runtime"
    Static,          // "static", "indirect", "alias", "generated", "transient": nothing to toggle
    Masked,          // "masked", "masked-runtime"
    NotFound,        // "not-found" or no such unit file
    Error            // anything else ("bad", empty, unknown text)
};

inline Unit parseUnitLine(const std::string &raw) {
    std::string s;
    for (char c : raw)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') s += c;
    if (s == "enabled") return Unit::Enabled;
    if (s == "enabled-runtime") return Unit::EnabledRuntime;
    if (s == "disabled" || s == "linked" || s == "linked-runtime") return Unit::Disabled;
    if (s == "static" || s == "indirect" || s == "alias" || s == "generated" || s == "transient")
        return Unit::Static;
    if (s == "masked" || s == "masked-runtime") return Unit::Masked;
    if (s == "not-found") return Unit::NotFound;
    return Unit::Error;
}

// Parses the stdout of `systemctl --user is-enabled <units...>` (one line per unit, in
// order). If a unit does not exist, systemctl prints only an error on stderr and no line
// for it; then the line count differs from the unit count: with "No such file" in stderr
// the whole answer is NotFound, otherwise Error (the lines cannot be assigned reliably).
inline std::vector<Unit> parseIsEnabled(const std::string &out, const std::string &err,
                                        size_t unit_count) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < out.size()) {
        size_t nl = out.find('\n', pos);
        std::string l = out.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? out.size() : nl + 1;
        if (l.find_first_not_of(" \t\r") != std::string::npos) lines.push_back(l);
    }
    if (lines.size() != unit_count) {
        const bool missing = err.find("No such file") != std::string::npos ||
                             err.find("not-found") != std::string::npos;
        return std::vector<Unit>(unit_count == 0 ? 1 : unit_count,
                                 missing ? Unit::NotFound : Unit::Error);
    }
    std::vector<Unit> r;
    for (const auto &l : lines) r.push_back(parseUnitLine(l));
    return r;
}

}  // namespace autostart
}  // namespace spn
