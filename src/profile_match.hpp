// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Matching logic for [device:*]/[app:*] profiles (substring, case-insensitive).
// Header-only so that the daemon and unit tests use the same function.
#pragma once
#include <cctype>
#include <string>

namespace spn {

inline std::string lowerAscii(std::string s) {
    for (char &c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// Same matching logic as ProfileSet::resolve. An empty pattern or empty
// text never matches.
inline bool patternMatches(const std::string &pattern, const std::string &text) {
    if (pattern.empty() || text.empty()) return false;
    return lowerAscii(text).find(lowerAscii(pattern)) != std::string::npos;
}

}  // namespace spn
