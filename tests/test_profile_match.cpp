// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit-Tests fuer src/profile_match.hpp (assert-basiert, eigenes main).
#include <cassert>
#include <cstdio>
#include "profile_match.hpp"

using namespace spn;

static void testMatching() {
    assert(patternMatches("compact", "SpaceMouse Compact"));   // Gross/Klein egal
    assert(patternMatches("COMPACT", "spacemouse compact"));
    assert(patternMatches("Space", "3Dconnexion SpaceMouse Pro"));  // Teilstring
    assert(patternMatches("firefox", "firefox"));              // exakt
    assert(!patternMatches("pro", "SpaceMouse Compact"));      // kein Treffer
    assert(!patternMatches("longer pattern", "short"));
}

static void testEmpty() {
    assert(!patternMatches("", "irgendwas"));
    assert(!patternMatches("x", ""));
    assert(!patternMatches("", ""));
}

int main() {
    testMatching();
    testEmpty();
    std::puts("test_profile_match: alle Tests bestanden");
    return 0;
}
