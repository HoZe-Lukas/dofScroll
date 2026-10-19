// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// PLATZHALTER - wird durch die echte Implementierung ersetzt (Schnittstelle: modes.hpp).
#include "modes.hpp"

namespace spn {

const ModeDef *ModeSet::find(const std::string &id) const {
    for (const ModeDef &m : modes)
        if (m.id == id) return &m;
    return nullptr;
}
bool validModeId(const std::string &id) { return !id.empty() && id.size() <= 32 && id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos; }
bool parseChannelAction(const std::string &, ChannelAction *, std::string *) { return false; }
bool parseButtonAction(const std::string &, ButtonActionType *, Key *, std::string *) { return false; }
std::string modesDir() { return ""; }
ModeSet loadModes(const std::string &, std::vector<std::string> *) {
    ModeSet s;
    ModeDef n;
    n.id = "normal";
    n.label = "Normal";
    n.builtin = true;
    s.modes.push_back(n);
    return s;
}
std::string modesStamp(const std::string &) { return ""; }

}  // namespace spn
