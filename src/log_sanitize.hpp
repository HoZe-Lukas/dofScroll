// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Bereinigung unvertrauenswuerdiger Strings (Geraetename, WM_CLASS, Profil-
// beschreibung) vor der Ausgabe ins Log: verhindert Log-Injection durch
// Zeilenumbrueche, Escape-Sequenzen und andere Steuerzeichen.
#pragma once
#include <cstddef>
#include <string>

namespace spn {

// Behalten: druckbares ASCII (0x20-0x7e) und gueltiges UTF-8 (ohne Overlongs,
// Surrogate, > U+10FFFF und ohne C1-Steuerzeichen U+0080-U+009F). Alles andere
// (inkl. NUL, ESC, DEL, kaputte Sequenzen) wird bytegenau zu '?'. Laenge wird
// auf maxLen Bytes (an Zeichengrenze) gekuerzt, dann folgt "...".
inline std::string sanitizeForLog(const std::string &in, size_t maxLen = 128) {
    std::string out;
    out.reserve(in.size() < maxLen ? in.size() : maxLen + 3);
    const size_t n = in.size();
    size_t i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)in[i];
        size_t len = 0;  // Bytes dieses Zeichens
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
