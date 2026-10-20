// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/autostart.hpp (assert-based, own main).
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "autostart.hpp"

using namespace spn::autostart;

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

    using V = std::vector<Unit>;
    // One unit, one line (blank lines are ignored).
    assert(parseIsEnabled("enabled\n", "", 1) == V{Unit::Enabled});
    assert(parseIsEnabled("\n\nenabled\n\n", "", 1) == V{Unit::Enabled});
    // Two units, in order.
    assert((parseIsEnabled("enabled\ndisabled\n", "", 2) == V{Unit::Enabled, Unit::Disabled}));
    // Missing unit file: systemctl prints only stderr.
    assert(parseIsEnabled("", "Failed to get unit file state for x: No such file or directory\n", 1) ==
           V{Unit::NotFound});
    assert((parseIsEnabled("enabled\n", "Failed to get unit file state for x: No such file or directory", 2) ==
            V{Unit::NotFound, Unit::NotFound}));
    // A line count that does not match, without an explanation: Error.
    assert(parseIsEnabled("", "", 1) == V{Unit::Error});
    assert((parseIsEnabled("enabled\n", "", 2) == V{Unit::Error, Unit::Error}));
    assert((parseIsEnabled("enabled\nenabled\nenabled\n", "", 2) == V{Unit::Error, Unit::Error}));

    puts("test_autostart: ok");
    return 0;
}
