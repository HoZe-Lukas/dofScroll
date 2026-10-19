// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/profile_match.hpp (assert-based, own main).
#include <cassert>
#include <cstdio>
#include "profile_match.hpp"

using namespace spn;

static void testMatching() {
    assert(patternMatches("compact", "SpaceMouse Compact"));   // case-insensitive
    assert(patternMatches("COMPACT", "spacemouse compact"));
    assert(patternMatches("Space", "3Dconnexion SpaceMouse Pro"));  // substring
    assert(patternMatches("firefox", "firefox"));              // exact
    assert(!patternMatches("pro", "SpaceMouse Compact"));      // no match
    assert(!patternMatches("longer pattern", "short"));
}

static void testEmpty() {
    assert(!patternMatches("", "anything"));
    assert(!patternMatches("x", ""));
    assert(!patternMatches("", ""));
}

int main() {
    testMatching();
    testEmpty();
    std::puts("test_profile_match: all tests passed");
    return 0;
}
