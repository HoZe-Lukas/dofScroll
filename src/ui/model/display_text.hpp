// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Text from files (profile names, file names, loader messages) as it is shown in the interface: controls and invalid
// bytes are dropped or replaced (utf8::sanitizeDisplay) and the characters that can spoof a name are removed:
// zero-width characters, bidirectional controls (U+202A..U+202E, U+2066..U+2069, U+200E/F) and the byte order mark.
// The data itself (the profile name in the file) is never changed, only what is displayed. No toolkit.
#pragma once
#include <string>

#include "utf8.hpp"

namespace spn::ui {

inline std::string displayText(const std::string &s, size_t maxBytes = 200) {
    const std::string clean = utf8::sanitizeDisplay(s, maxBytes * 2);
    std::string out;
    for (size_t i = 0; i < clean.size(); ++i) {
        const unsigned char a = static_cast<unsigned char>(clean[i]);
        if (a == 0xe2 && i + 2 < clean.size()) {
            const unsigned char b = static_cast<unsigned char>(clean[i + 1]), c = static_cast<unsigned char>(clean[i + 2]);
            const bool zw = b == 0x80 && ((c >= 0x8b && c <= 0x8f) || (c >= 0xaa && c <= 0xae));  // U+200B..200F, U+202A..202E
            const bool iso = b == 0x81 && c >= 0xa6 && c <= 0xa9;                              // U+2066..2069
            if (zw || iso) {
                i += 2;
                continue;
            }
        }
        if (a == 0xef && i + 2 < clean.size() && static_cast<unsigned char>(clean[i + 1]) == 0xbb &&
            static_cast<unsigned char>(clean[i + 2]) == 0xbf) {  // U+FEFF
            i += 2;
            continue;
        }
        out += clean[i];
    }
    return out.size() > maxBytes ? utf8::sanitizeDisplay(out, maxBytes) : out;
}

}  // namespace spn::ui
