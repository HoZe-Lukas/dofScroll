// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS output tables (src/platform/mac/mac_key_tables.hpp): every Key mapped, no accidental
// duplicates, modifier flags, scroll and button conventions. Pure; runs on every OS.
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <set>

#include "platform/mac/mac_key_tables.hpp"

using namespace spn;
using namespace spn::mac;

int main() {
    std::set<uint16_t> codes;
    for (size_t i = 0; i < kKeyCount; ++i) {
        const Key k = static_cast<Key>(i);
        const uint16_t c = keyToKeycode(k);
        assert(c != 0xFFFF && c < 0x80);  // virtual key codes are below 0x80
        assert(codes.insert(c).second);   // no two keys share a code
    }
    assert(codes.size() == kKeyCount);

    // spot checks against Carbon HIToolbox/Events.h
    assert(keyToKeycode(Key::A) == 0x00 && keyToKeycode(Key::Z) == 0x06 && keyToKeycode(Key::Space) == 0x31);
    assert(keyToKeycode(Key::Enter) == 0x24 && keyToKeycode(Key::Esc) == 0x35 && keyToKeycode(Key::Tab) == 0x30);
    assert(keyToKeycode(Key::Left) == 0x7B && keyToKeycode(Key::Right) == 0x7C);
    assert(keyToKeycode(Key::Down) == 0x7D && keyToKeycode(Key::Up) == 0x7E);
    assert(keyToKeycode(Key::Ctrl) == 0x3B && keyToKeycode(Key::Shift) == 0x38);
    assert(keyToKeycode(Key::Alt) == 0x3A && keyToKeycode(Key::Super) == 0x37);
    assert(keyToKeycode(Key::F1) == 0x7A && keyToKeycode(Key::F12) == 0x6F);
    assert(keyToKeycode(Key::Backspace) == 0x33 && keyToKeycode(Key::Delete) == 0x75);

    // plus / minus: main block vs keypad
    assert(keyToKeycode(Key::Plus) == 0x18 && keyNeedsShift(Key::Plus));
    assert(keyToKeycode(Key::Minus) == 0x1B && !keyNeedsShift(Key::Minus));
    assert(keyToKeycode(Key::KpPlus) == 0x45 && keyToKeycode(Key::KpMinus) == 0x4E);
    assert(!keyNeedsShift(Key::KpPlus) && !keyNeedsShift(Key::KpMinus));
    int shifts = 0;
    for (size_t i = 0; i < kKeyCount; ++i) shifts += keyNeedsShift(static_cast<Key>(i)) ? 1 : 0;
    assert(shifts == 1);

    // the extra modifier codes are distinct from every mapped key
    for (uint16_t c : kExtraModifierKeycodes) assert(codes.count(c) == 0);

    // modifier flags: exactly the four modifier keys, distinct single bits
    assert(keyModifierFlag(Key::Ctrl) == 0x40000 && keyModifierFlag(Key::Shift) == 0x20000);
    assert(keyModifierFlag(Key::Alt) == 0x80000 && keyModifierFlag(Key::Super) == 0x100000);
    int mods = 0;
    for (size_t i = 0; i < kKeyCount; ++i) {
        const uint64_t f = keyModifierFlag(static_cast<Key>(i));
        if (!f) continue;
        ++mods;
        assert((f & (f - 1)) == 0);
    }
    assert(mods == 4);

    // scroll: positive wheelV (down) / wheelH (right) become negative CGEvent values; clamped
    assert(scrollWheelValue(0) == 0);
    assert(scrollWheelValue(3) == -3 && scrollWheelValue(-3) == 3);
    assert(scrollWheelValue(100000) == -kMaxSteps && scrollWheelValue(-100000) == kMaxSteps);
    assert(scrollWheelValue(INT32_MIN) == kMaxSteps && scrollWheelValue(INT32_MAX) == -kMaxSteps);

    // mouse buttons
    MouseButtonSpec s{};
    assert(mouseButtonSpec(1, &s) && s.cgButton == 0 && s.downType == 1 && s.upType == 2);
    assert(mouseButtonSpec(2, &s) && s.cgButton == 2 && s.downType == 25 && s.upType == 26);
    assert(mouseButtonSpec(3, &s) && s.cgButton == 1 && s.downType == 3 && s.upType == 4);
    assert(!mouseButtonSpec(0, &s) && !mouseButtonSpec(4, &s) && !mouseButtonSpec(-1, &s));

    std::puts("test_mac_key_tables: ok");
    return 0;
}
