// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Start at login" option of the tray: pure logic, no GUI and no process spawning, so it
// compiles and is unit-tested on every OS (tests/test_autostart.cpp).
//
// Contract every platform tray implements (two operations):
//   1. queryState()      -> spn::autostart::State  (is dofscroll started with the session?)
//   2. setEnabled(bool)  -> success or a short error message
// Linux (this file): the user units dofscroll.service and dofscroll-tray.service,
//   queried with `systemctl --user is-enabled`, changed with `enable`/`disable`
//   (never --now: the option only affects the next login).
// Windows: per-user Run key values, see src/platform/autostart_platform.hpp (W18).
// macOS: LaunchAgent plists in ~/Library/LaunchAgents, same place (W18).
// The Windows and macOS trays map their native state onto the same State values and reuse
// ui(): Enabled/Disabled for entry present/absent, NotFound if the program is not installed
// (no executable path to register), Error for failures.
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

// Combined state of all units, what the checkbox is based on.
enum class State { Enabled, EnabledRuntime, Disabled, Static, Masked, NotFound, Error };

inline const std::vector<std::string> &units() {
    static const std::vector<std::string> u = {"dofscroll.service", "dofscroll-tray.service"};
    return u;
}

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

// Combination, in order of precedence: a missing unit, a masked unit, an error, a unit
// that cannot be toggled; otherwise Enabled only if ALL units are enabled (persistently,
// else EnabledRuntime), everything else (including a mix) is Disabled so that ticking the
// box enables both.
inline State combine(const std::vector<Unit> &us) {
    if (us.empty()) return State::Error;
    bool any_missing = false, any_masked = false, any_err = false, any_static = false;
    bool all_enabled = true, any_runtime = false;
    for (Unit u : us) {
        any_missing |= u == Unit::NotFound;
        any_masked |= u == Unit::Masked;
        any_err |= u == Unit::Error;
        any_static |= u == Unit::Static;
        if (u == Unit::EnabledRuntime) any_runtime = true;
        if (u != Unit::Enabled && u != Unit::EnabledRuntime) all_enabled = false;
    }
    if (any_missing) return State::NotFound;
    if (any_masked) return State::Masked;
    if (any_err) return State::Error;
    if (any_static) return State::Static;
    if (all_enabled) return any_runtime ? State::EnabledRuntime : State::Enabled;
    return State::Disabled;
}

inline State stateFromOutput(const std::string &out, const std::string &err) {
    return combine(parseIsEnabled(out, err, units().size()));
}

// What the UI shows. `can_toggle` false = insensitive checkbox (the reason is the State).
struct Ui {
    bool checked;
    bool can_toggle;
};

inline Ui ui(State s) {
    switch (s) {
        case State::Enabled: return {true, true};
        case State::Disabled: return {false, true};
        case State::EnabledRuntime: return {true, false};
        default: return {false, false};  // Static, Masked, NotFound, Error
    }
}

inline std::vector<std::string> queryArgv() {
    std::vector<std::string> a = {"systemctl", "--user", "is-enabled"};
    for (const auto &u : units()) a.push_back(u);
    return a;
}

// Never --now: only the next login is affected, the running services stay as they are.
inline std::vector<std::string> setEnabledArgv(bool enable) {
    std::vector<std::string> a = {"systemctl", "--user", enable ? "enable" : "disable"};
    for (const auto &u : units()) a.push_back(u);
    return a;
}

}  // namespace autostart
}  // namespace spn
