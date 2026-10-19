// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Trefferlogik fuer [device:*]/[app:*]-Profile (Teilstring, Gross/Klein egal).
// Header-only, damit Daemon und Unit-Tests dieselbe Funktion nutzen.
#pragma once
#include <cctype>
#include <string>

namespace spn {

inline std::string lowerAscii(std::string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Gleiche Trefferlogik wie ProfileSet::resolve. Leeres Muster oder leerer
// Text treffen nie.
inline bool patternMatches(const std::string &pattern, const std::string &text) {
    if (pattern.empty() || text.empty()) return false;
    return lowerAscii(text).find(lowerAscii(pattern)) != std::string::npos;
}

}  // namespace spn
