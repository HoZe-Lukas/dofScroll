// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows key tables and SendInput arithmetic (pure; runs on every OS).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <map>
#include <set>
#include <string>

#include "platform/win/win_key_tables.hpp"

using namespace spn;
using namespace spn::winkeys;

int main() {
    // every Key is mapped; names round-trip; VKs are unique except documented ones (none)
    std::set<unsigned> vks;
    for (size_t i = 0; i < kKeyCount; ++i) {
        const Key k = static_cast<Key>(i);
        const WinKeySpec s = keyToWin(k);
        assert(s.vk != 0 && s.vk < 0x100);
        assert(vks.insert(s.vk).second);  // no two keys share a virtual-key code
        Key back;
        assert(parseKeyName(keyName(k), &back) && back == k);
    }
    assert(vks.size() == kKeyCount);

    // spot checks against the documented VK_* values
    assert(keyToWin(Key::Ctrl).vk == 0xA2 && keyToWin(Key::Shift).vk == 0xA0);
    assert(keyToWin(Key::Alt).vk == 0xA4 && keyToWin(Key::Super).vk == 0x5B);
    assert(keyToWin(Key::Enter).vk == 0x0D && keyToWin(Key::Esc).vk == 0x1B);
    assert(keyToWin(Key::A).vk == 'A' && keyToWin(Key::Z).vk == 'Z');
    assert(keyToWin(Key::N0).vk == '0' && keyToWin(Key::N9).vk == '9');
    assert(keyToWin(Key::F1).vk == 0x70 && keyToWin(Key::F12).vk == 0x7B);
    for (int i = 0; i < 12; ++i) {
        const Key k = static_cast<Key>(static_cast<int>(Key::F1) + i);
        assert(keyToWin(k).vk == 0x70 + i);
    }
    assert(keyToWin(Key::Minus).vk == 0xBD && !keyToWin(Key::Minus).shift);
    assert(keyToWin(Key::KpPlus).vk == 0x6B && keyToWin(Key::KpMinus).vk == 0x6D);
    // "plus" is the main-keyboard plus (needs Shift on the default layout), not the keypad one
    assert(keyToWin(Key::Plus).vk == 0xBB && keyToWin(Key::Plus).shift);
    for (size_t i = 0; i < kKeyCount; ++i)
        if (static_cast<Key>(i) != Key::Plus) assert(!keyToWin(static_cast<Key>(i)).shift);

    // extended-key flag exactly for arrows, the nav block and the Win key
    const std::set<Key> ext = {Key::Up,     Key::Down, Key::Left, Key::Right,  Key::Insert,
                               Key::Delete, Key::Home, Key::End,  Key::PageUp, Key::PageDown,
                               Key::Super};
    for (size_t i = 0; i < kKeyCount; ++i) {
        const Key k = static_cast<Key>(i);
        assert(keyToWin(k).extended == (ext.count(k) != 0));
    }

    // right-hand modifiers released by forceReleaseAll are distinct from the table
    for (const ExtraRelease &e : kExtraReleases) assert(vks.count(e.vk) == 0);
    assert(sizeof(kExtraReleases) / sizeof(kExtraReleases[0]) == 4);

    // wheel: Output convention (n > 0 = down) -> Windows (negative = down/toward the user)
    assert(wheelDetentV(1) == -120 && wheelDetentV(-1) == 120 && wheelDetentV(0) == 0);
    assert(wheelDetentV(50) == -120);  // per-detent value; the count is separate
    assert(wheelDetentH(1) == 120 && wheelDetentH(-1) == -120 && wheelDetentH(0) == 0);
    assert(wheelCount(0) == 0 && wheelCount(3) == 3 && wheelCount(-3) == 3);
    assert(wheelTotalV(3) == -360 && wheelTotalV(-2) == 240);
    assert(wheelTotalH(3) == 360 && wheelTotalH(-2) == -240);
    // saturation (also protects INT_MIN)
    assert(wheelCount(1000) == kMaxSteps && wheelCount(-1000) == kMaxSteps);
    assert(wheelCount(INT32_MIN) == kMaxSteps && wheelCount(INT32_MAX) == kMaxSteps);
    assert(wheelTotalV(100000) == -kMaxSteps * kWheelDelta);
    assert(wheelTotalH(-100000) == -kMaxSteps * kWheelDelta);
    assert(kWheelDelta == 120 && kMouseEventWheel == 0x0800 && kMouseEventHWheel == 0x1000);

    // mouse buttons: 1 = left, 2 = middle, 3 = right; anything else invalid
    assert(mouseButtonFlags(1).down == 0x0002 && mouseButtonFlags(1).up == 0x0004);
    assert(mouseButtonFlags(2).down == 0x0020 && mouseButtonFlags(2).up == 0x0040);
    assert(mouseButtonFlags(3).down == 0x0008 && mouseButtonFlags(3).up == 0x0010);
    for (int b : {0, 4, -1, 100}) assert(mouseButtonFlags(b).down == 0 && mouseButtonFlags(b).up == 0);
    std::set<uint32_t> flags;
    for (int b = 1; b <= 3; ++b) {
        assert(flags.insert(mouseButtonFlags(b).down).second && flags.insert(mouseButtonFlags(b).up).second);
    }

    std::puts("test_win_key_tables: ok");
    return 0;
}
