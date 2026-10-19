// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// UTF-8 helpers shared by the whole program (header-only). All text is UTF-8 (docs/encoding.md).
// Nothing here depends on the C locale: <cctype> functions on UTF-8 bytes are wrong in a
// single-byte locale (tolower(0xC3) changes a lead byte), so use these ASCII-only helpers.
#pragma once
#include <clocale>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace spn {
namespace utf8 {

constexpr size_t npos = static_cast<size_t>(-1);

// Length in bytes of the valid UTF-8 character starting at s[i] (no overlongs, surrogates or
// values above U+10FFFF), or 0 if the bytes there are not valid. Control characters are valid.
inline size_t charLen(const std::string &s, size_t i) {
    const size_t n = s.size();
    if (i >= n) return 0;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) return 1;
    if (c < 0xc2 || c > 0xf4) return 0;
    const size_t need = c < 0xe0 ? 2 : (c < 0xf0 ? 3 : 4);
    if (i + need > n) return 0;
    for (size_t k = 1; k < need; ++k)
        if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) return 0;
    const unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
    if (c == 0xe0 && c1 < 0xa0) return 0;   // overlong
    if (c == 0xed && c1 >= 0xa0) return 0;  // surrogate
    if (c == 0xf0 && c1 < 0x90) return 0;   // overlong
    if (c == 0xf4 && c1 >= 0x90) return 0;  // above U+10FFFF
    return need;
}

// Offset of the first invalid byte, or npos if the whole string is valid UTF-8.
inline size_t firstInvalid(const std::string &s) {
    size_t i = 0;
    while (i < s.size()) {
        const size_t l = charLen(s, i);
        if (l == 0) return i;
        i += l;
    }
    return npos;
}

inline bool isValid(const std::string &s) { return firstInvalid(s) == npos; }

// At most maxBytes bytes, cut on a character boundary (an invalid byte counts as one character).
inline std::string truncate(const std::string &s, size_t maxBytes) {
    if (s.size() <= maxBytes) return s;
    size_t i = 0;
    while (i < s.size()) {
        size_t l = charLen(s, i);
        if (l == 0) l = 1;
        if (i + l > maxBytes) break;
        i += l;
    }
    return s.substr(0, i);
}

// Display text: valid UTF-8 is kept; C0 controls, DEL and C1 controls are dropped; every
// invalid byte becomes U+FFFD. The result is cut to at most maxBytes on a character boundary.
inline std::string sanitizeDisplay(const std::string &s, size_t maxBytes) {
    std::string out;
    size_t i = 0;
    while (i < s.size()) {
        const size_t l = charLen(s, i);
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::string piece;
        if (l == 0) {
            piece = "\xEF\xBF\xBD";
            i += 1;
        } else {
            const bool c1 = l == 2 && c == 0xc2 && static_cast<unsigned char>(s[i + 1]) < 0xa0;
            if (!(l == 1 && (c < 0x20 || c == 0x7f)) && !c1) piece.assign(s, i, l);
            i += l;
        }
        if (out.size() + piece.size() > maxBytes) break;
        out += piece;
    }
    return out;
}

// ---- ASCII-only helpers (locale independent, bytes >= 0x80 are never touched) ----------------

inline char lowerChar(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }
inline char upperChar(char c) { return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c; }

inline std::string lowerAscii(std::string s) {
    for (char &c : s) c = lowerChar(c);
    return s;
}
inline std::string upperAscii(std::string s) {
    for (char &c : s) c = upperChar(c);
    return s;
}

// Space, \t \n \v \f \r only.
inline bool isSpace(char c) { return c == ' ' || (c >= '\t' && c <= '\r'); }

inline std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && isSpace(s[a])) a++;
    while (b > a && isSpace(s[b - 1])) b--;
    return s.substr(a, b - a);
}

}  // namespace utf8

// ---- numbers in the C format ("0.5") whatever LC_NUMERIC is -------------------------------------
// GTK programs call setlocale(LC_ALL, ""): in a locale with a decimal comma plain strtod/snprintf
// would read "0.5" as 0 and write "0,5" into the configuration file.

// strtod for the C format; a decimal comma (or whatever the locale uses) is never accepted.
inline double strtodC(const char *s, char **end) {
    const char *dp = std::localeconv()->decimal_point;
    if (!dp || (dp[0] == '.' && dp[1] == 0)) return std::strtod(s, end);
    const size_t dpl = std::strlen(dp);
    std::string t;
    size_t dotAt = std::string::npos;
    for (size_t i = 0; s[i]; ++i) {
        if (std::strncmp(s + i, dp, dpl) == 0) {  // locale point in the input: stops the parse
            t += 'x';
            i += dpl - 1;
        } else if (s[i] == '.' && dotAt == std::string::npos) {
            dotAt = t.size();
            t += dp;
        } else {
            t += s[i];
        }
    }
    char *e = nullptr;
    const double v = std::strtod(t.c_str(), &e);
    size_t used = static_cast<size_t>(e - t.c_str());
    if (dotAt != std::string::npos && used > dotAt) used -= (dpl - 1);
    if (end) *end = const_cast<char *>(s) + used;
    return v;
}

// snprintf("%.*g") with a '.' as decimal point.
inline std::string formatG(int prec, double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*g", prec, v);
    std::string r = buf;
    const char *dp = std::localeconv()->decimal_point;
    if (dp && !(dp[0] == '.' && dp[1] == 0)) {
        const size_t p = r.find(dp);
        if (p != std::string::npos) r.replace(p, std::strlen(dp), ".");
    }
    return r;
}
}  // namespace spn
