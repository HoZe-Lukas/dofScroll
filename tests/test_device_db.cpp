// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include <cassert>
#include <cstdio>

#include "device_db.hpp"

using spn::factoryButtonCount;

int main() {
    assert(factoryButtonCount("3Dconnexion SpaceMouse Compact", 7) == 2);
    assert(factoryButtonCount("3Dconnexion SpaceMouse Wireless", 7) == 2);
    assert(factoryButtonCount("3Dconnexion SpaceMouse Pro", 2) == 15);
    assert(factoryButtonCount("3Dconnexion SpaceMouse Pro Wireless (cabled)", 2) == 15);
    assert(factoryButtonCount("3Dconnexion SpaceMouse Enterprise", 2) == 31);
    assert(factoryButtonCount("3Dconnexion SpaceNavigator", 9) == 2);
    assert(factoryButtonCount("3Dconnexion SpaceNavigator for Notebooks", 9) == 2);
    // case-insensitive
    assert(factoryButtonCount("SPACEMOUSE COMPACT", 0) == 2);
    assert(factoryButtonCount("spacemouse pro wireless receiver", 0) == 15);
    // fallback: unknown / empty names keep the reported count
    assert(factoryButtonCount("3Dconnexion SpaceExplorer", 15) == 15);
    assert(factoryButtonCount("Some Other Device", 4) == 4);
    assert(factoryButtonCount("", 6) == 6);
    assert(factoryButtonCount("", -3) == 0);
    std::puts("test_device_db OK");
    return 0;
}
