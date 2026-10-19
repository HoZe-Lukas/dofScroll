// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Sanitizing of untrusted strings (device name, WM_CLASS, profile description)
// before they are written to the log: prevents log injection via line breaks,
// escape sequences and other control characters.
#pragma once
#include <cstddef>
#include <string>

namespace spn {

// Kept: printable ASCII (0x20-0x7e) and valid UTF-8 (no overlongs, surrogates,
// > U+10FFFF and no C1 control characters U+0080-U+009F). Everything else
// (incl. NUL, ESC, DEL, broken sequences) becomes '?' byte by byte. The length is
// truncated to maxLen bytes (at a character boundary), followed by "...".
inline std::string sanitizeForLog(const std::string &in, size_t maxLen = 128) {
    std::string out;
    out.reserve(in.size() < maxLen ? in.size() : maxLen + 3);
    const size_t n = in.size();
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)in[i];
        size_t len = 0;  // bytes of this character
        if (c >= 0x20 && c <= 0x7e) {
            len = 1;
        } else if (c >= 0xc2 && c <= 0xf4) {
            size_t need = c < 0xe0 ? 2 : (c < 0xf0 ? 3 : 4);
            if (i + need <= n) {
                bool ok = true;
                for (size_t k = 1; k < need; ++k)
                    if (((unsigned char)in[i + k] & 0xc0) != 0x80) ok = false;
                if (ok) {
                    unsigned char c1 = (unsigned char)in[i + 1];
                    if (c == 0xc2 && c1 < 0xa0) ok = false;          // C1
                    else if (c == 0xe0 && c1 < 0xa0) ok = false;     // Overlong
                    else if (c == 0xed && c1 >= 0xa0) ok = false;    // Surrogate
                    else if (c == 0xf0 && c1 < 0x90) ok = false;     // Overlong
                    else if (c == 0xf4 && c1 >= 0x90) ok = false;    // > U+10FFFF
                }
                if (ok) len = need;
            }
        }
        if (out.size() + (len ? len : 1) > maxLen) {
            out += "...";
            return out;
        }
        if (len) out.append(in, i, len);
        else out += '?';
        i += len ? len : 1;
    }
    return out;
}

}  // namespace spn
