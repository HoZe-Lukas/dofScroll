// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Key tables: every Key is mapped, no accidental duplicates, names round-trip.
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

#include "key_tables.hpp"

using namespace spn;

int main() {
    std::set<unsigned long> syms;
    std::set<int> codes;
    std::set<std::string> names;
    for (size_t i = 0; i < kKeyCount; ++i) {
        const Key k = static_cast<Key>(i);
        assert(keyToKeysym(k) != 0);
        assert(keyToLinuxCode(k) != 0);
        assert(syms.insert(keyToKeysym(k)).second);
        assert(codes.insert(keyToLinuxCode(k)).second);
        assert(names.insert(keyName(k)).second);
        Key back = Key::Count_;
        assert(parseKeyName(keyName(k), &back) && back == k);
    }
    assert(syms.size() == kKeyCount && codes.size() == kKeyCount);
    Key dummy;
    assert(!parseKeyName("nonexistent", &dummy));
    assert(mouseButtonToLinuxCode(1) == BTN_LEFT);
    assert(mouseButtonToLinuxCode(2) == BTN_MIDDLE);
    assert(mouseButtonToLinuxCode(3) == BTN_RIGHT);
    assert(mouseButtonToLinuxCode(0) == 0 && mouseButtonToLinuxCode(4) == 0);
    std::puts("test_keys ok");
    return 0;
}
