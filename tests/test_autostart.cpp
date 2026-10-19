// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/autostart.hpp (assert-based, own main).
#include <cassert>
#include <cstdio>
#include <string>
#include "autostart.hpp"

using namespace spn::autostart;

static State st(const std::string &out, const std::string &err = "") {
    return stateFromOutput(out, err);
}

int main() {
    assert(parseUnitLine("enabled\n") == Unit::Enabled);
    assert(parseUnitLine("enabled-runtime") == Unit::EnabledRuntime);
    assert(parseUnitLine("disabled") == Unit::Disabled);
    assert(parseUnitLine("linked") == Unit::Disabled);
    assert(parseUnitLine("static") == Unit::Static);
    assert(parseUnitLine("indirect") == Unit::Static);
    assert(parseUnitLine("masked") == Unit::Masked);
    assert(parseUnitLine("not-found") == Unit::NotFound);
    assert(parseUnitLine("bad") == Unit::Error);
    assert(parseUnitLine("") == Unit::Error);
    assert(parseUnitLine("enabled; rm -rf") == Unit::Error);

    assert(st("enabled\nenabled\n") == State::Enabled);
    assert(st("enabled\nenabled-runtime\n") == State::EnabledRuntime);
    assert(st("disabled\ndisabled\n") == State::Disabled);
    assert(st("enabled\ndisabled\n") == State::Disabled);  // mix: ticking enables both
    assert(st("disabled\nenabled\n") == State::Disabled);
    assert(st("static\nstatic\n") == State::Static);
    assert(st("indirect\nenabled\n") == State::Static);
    assert(st("masked\nenabled\n") == State::Masked);
    assert(st("masked\nstatic\n") == State::Masked);
    assert(st("not-found\nenabled\n") == State::NotFound);
    assert(st("bad\nenabled\n") == State::Error);
    // Missing unit file: systemctl prints only stderr.
    assert(st("", "Failed to get unit file state for dofscroll.service: No such file or directory\n") ==
           State::NotFound);
    assert(st("enabled\n", "Failed to get unit file state for x: No such file or directory") ==
           State::NotFound);
    assert(st("") == State::Error);
    assert(st("enabled\n") == State::Error);  // one line, no explanation
    assert(st("enabled\nenabled\nenabled\n") == State::Error);
    assert(st("\n\nenabled\n\nenabled\n\n") == State::Enabled);  // blank lines ignored
    assert(combine({}) == State::Error);

    assert(ui(State::Enabled).checked && ui(State::Enabled).can_toggle);
    assert(!ui(State::Disabled).checked && ui(State::Disabled).can_toggle);
    assert(ui(State::EnabledRuntime).checked && !ui(State::EnabledRuntime).can_toggle);
    for (State s : {State::Static, State::Masked, State::NotFound, State::Error}) {
        assert(!ui(s).checked && !ui(s).can_toggle);
    }

    using V = std::vector<std::string>;
    assert(queryArgv() == (V{"systemctl", "--user", "is-enabled", "dofscroll.service",
                             "dofscroll-tray.service"}));
    assert(setEnabledArgv(true) == (V{"systemctl", "--user", "enable", "dofscroll.service",
                                      "dofscroll-tray.service"}));
    assert(setEnabledArgv(false) == (V{"systemctl", "--user", "disable", "dofscroll.service",
                                       "dofscroll-tray.service"}));
    for (const auto &a : setEnabledArgv(true)) assert(a != "--now");

    puts("test_autostart: ok");
    return 0;
}
