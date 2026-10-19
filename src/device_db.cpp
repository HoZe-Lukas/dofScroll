// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "device_db.hpp"
#include "utf8.hpp"

#include <cctype>

namespace spn {
namespace {

struct Model {
    const char *needle;  // lower-case substring of the reported device name
    int buttons;
};

// More specific names come first: the first match wins.
// Sources: 3Dconnexion product pages (Compact, Wireless, Pro, Pro Wireless, Enterprise)
// and manufacturer product copy as published by distributors (SpaceNavigator).
// SpaceExplorer and SpacePilot Pro are intentionally absent: no count could be verified.
const Model kModels[] = {
    {"spacemouse pro wireless", 15},
    {"spacemouse pro", 15},
    {"spacemouse enterprise", 31},
    {"spacemouse compact", 2},
    {"spacemouse wireless", 2},
    {"spacenavigator", 2},
    {"space navigator", 2},
};

std::string lower(const std::string &s) {
    std::string r = s;
    for (char &c : r) c = static_cast<char>(spn::utf8::lowerChar(c));
    return r;
}

}  // namespace

int factoryButtonCount(const std::string &deviceName, int reported) {
    const std::string n = lower(deviceName);
    for (const Model &m : kModels)
        if (n.find(m.needle) != std::string::npos) return m.buttons;
    return reported < 0 ? 0 : reported;
}

}  // namespace spn
