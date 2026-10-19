// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit-Tests fuer src/log_sanitize.hpp (assert-basiert, eigenes main).
#include <cassert>
#include <cstdio>
#include <string>
#include "log_sanitize.hpp"

using namespace spn;

int main() {
    assert(sanitizeForLog("SpaceMouse Pro") == "SpaceMouse Pro");
    assert(sanitizeForLog("a\nb\rc") == "a?b?c");
    assert(sanitizeForLog("x\x1b[31my") == "x?[31my");
    assert(sanitizeForLog("a\x7f" "b") == "a?b");
    assert(sanitizeForLog(std::string("a\0b", 3)) == "a?b");
    assert(sanitizeForLog("K\xc3\xa4se \xc3\x9c") == "K\xc3\xa4se \xc3\x9c");  // Umlaute
    assert(sanitizeForLog("a\xc2\x9b" "b") == "a??b");   // C1 (CSI)
    assert(sanitizeForLog("a\xff" "b") == "a?b");        // ungueltig
    assert(sanitizeForLog("a\xc3") == "a?");             // abgeschnitten
    assert(sanitizeForLog("\xc0\x80") == "??");          // Overlong
    assert(sanitizeForLog("\xed\xa0\x80") == "???");     // Surrogat
    assert(sanitizeForLog("") == "");
    std::string lng(1000, 'a');
    std::string r = sanitizeForLog(lng);
    assert(r.size() == 128 + 3 && r.substr(128) == "...");
    assert(sanitizeForLog(lng, 10) == std::string(10, 'a') + "...");
    // Kuerzung nie mitten im UTF-8-Zeichen
    std::string u = sanitizeForLog("aaa\xc3\xa4\xc3\xa4", 4);
    assert(u == "aaa...");
    assert(sanitizeForLog("abc", 3) == "abc");
    std::puts("test_log_sanitize: alle Tests bestanden");
    return 0;
}
