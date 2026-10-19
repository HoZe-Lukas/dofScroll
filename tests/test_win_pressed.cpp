// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tracked pressed-key set of the Windows output (pure; runs on every OS).
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "platform/win/win_pressed.hpp"

using namespace spn;
using namespace spn::winkeys;

int main() {
    const size_t kCtrl = static_cast<size_t>(Key::Ctrl), kShift = static_cast<size_t>(Key::Shift);
    const size_t kPlus = static_cast<size_t>(Key::Plus), kUp = static_cast<size_t>(Key::Up);
    KeyRelease r;
    bool rs = false;

    {  // a key we never pressed is never released
        PressedKeys p;
        p.setShiftSlot(kShift);
        assert(!p.release(kCtrl, false, &r, &rs));
        assert(p.releaseAll({0xA0, false}).empty());
    }
    {  // idempotent press, release with the recorded code
        PressedKeys p;
        p.setShiftSlot(kShift);
        assert(!p.press(kUp, 0x26, true, false, false, false));
        assert(!p.press(kUp, 0x99, false, false, false, false));  // second press ignored
        assert(p.isDown(kUp) && p.recorded(kUp, &r) && r.vk == 0x26 && r.extended);
        assert(p.release(kUp, false, &r, &rs) && r.vk == 0x26 && r.extended && !rs);
        assert(!p.isDown(kUp) && !p.release(kUp, false, &r, &rs));
    }
    {  // plus: layout changes between down and up, the up uses the vk/shift state of the down
        PressedKeys p;
        p.setShiftSlot(kShift);
        assert(p.press(kPlus, 0xBB, false, true, false, false));  // US: needs Shift, we press it
        assert(p.shiftByUs());
        // (the layout is now German: the caller would resolve another vk, but must not ask again)
        assert(p.release(kPlus, false, &r, &rs));
        assert(r.vk == 0xBB && rs && !p.shiftByUs());
    }
    {  // the user holds Shift physically: we neither press nor release it
        PressedKeys p;
        p.setShiftSlot(kShift);
        assert(!p.press(kPlus, 0xBB, false, true, false, true));
        assert(!p.shiftByUs());
        assert(p.release(kPlus, false, &r, &rs) && !rs);
    }
    {  // Shift held as a mode key: the plus does not touch it
        PressedKeys p;
        p.setShiftSlot(kShift);
        assert(!p.press(kShift, 0xA0, false, false, false, false));
        assert(!p.press(kPlus, 0xBB, false, true, true, false));
        assert(p.release(kPlus, true, &r, &rs) && !rs);
        assert(p.isDown(kShift));
    }
    {  // releaseAll: tracked keys with recorded codes, our Shift, nothing else; state is cleared
        PressedKeys p;
        p.setShiftSlot(kShift);
        assert(!p.press(kCtrl, 0xA2, false, false, false, false));
        assert(p.press(kPlus, 0xBB, false, true, false, false));
        const std::vector<KeyRelease> all = p.releaseAll({0xA0, false});
        assert(all.size() == 3);
        bool ctrl = false, plus = false, shift = false;
        for (const KeyRelease &k : all) {
            ctrl |= k.vk == 0xA2;
            plus |= k.vk == 0xBB;
            shift |= k.vk == 0xA0;
        }
        assert(ctrl && plus && shift);
        assert(!p.isDown(kCtrl) && !p.shiftByUs() && p.releaseAll({0xA0, false}).empty());
    }
    {  // out-of-range slots are ignored
        PressedKeys p;
        assert(!p.press(kKeyCount, 1, false, false, false, false) && !p.isDown(kKeyCount));
        assert(!p.release(kKeyCount + 5, false, &r, &rs));
    }
    // menu-trigger keys: bare Alt/Win key-ups are never part of a blanket release
    assert(isMenuTriggerVk(0xA4) && isMenuTriggerVk(0xA5) && isMenuTriggerVk(0x5B) && isMenuTriggerVk(0x5C));
    assert(!isMenuTriggerVk(0xA2) && !isMenuTriggerVk(0xA0));

    std::puts("test_win_pressed: ok");
    return 0;
}
