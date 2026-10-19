// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Bookkeeping of the keys this process has pressed through SendInput (pure logic, no Windows
// header, unit-tested on every OS: tests/test_win_pressed.cpp). A key-up is only ever sent for a
// key recorded here, and with exactly the virtual-key code and extended flag used at key-down, so
// a keyboard layout change in between cannot leave a key stuck, and modifiers that the user holds
// physically are never released on the user's behalf.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../keys.hpp"

namespace spn {
namespace winkeys {

// Keys whose bare key-up has a side effect (Alt: menu bar activation, Win: Start menu).
inline bool isMenuTriggerVk(unsigned vk) {
    return vk == 0x12 || vk == 0xA4 || vk == 0xA5 ||  // VK_MENU, VK_LMENU, VK_RMENU
           vk == 0x5B || vk == 0x5C;                  // VK_LWIN, VK_RWIN
}

struct KeyRelease {
    uint16_t vk = 0;
    bool extended = false;
};

class PressedKeys {
public:
    // Records that slot `slot` is pressed with (vk, extended). `needsShift`: the key needs Shift
    // at the same time; `shiftPhysical`: the user holds Shift anyway. Returns true if the caller
    // has to press Shift first (it is then tracked as pressed by us). Idempotent per slot.
    bool press(size_t slot, uint16_t vk, bool extended, bool needsShift, bool shiftSlotHeld, bool shiftPhysical) {
        if (slot >= kSlots || slot_[slot].down) return false;
        slot_[slot] = {true, vk, extended, false};
        if (isShiftSlot_(slot)) shiftByUs_ = false;  // ownership moves to the Shift slot itself
        if (needsShift && !shiftSlotHeld && !shiftByUs_ && !shiftPhysical) {
            shiftByUs_ = true;
            slot_[slot].usedShift = true;
            return true;
        }
        slot_[slot].usedShift = needsShift;
        return false;
    }

    // Releases `slot`. Returns false if it was not pressed by us (nothing must be sent). Otherwise
    // `*key` is the key-up to send, `*releaseShift` whether our synthetic Shift goes up after it.
    bool release(size_t slot, bool shiftSlotHeld, KeyRelease *key, bool *releaseShift) {
        if (slot >= kSlots || !slot_[slot].down) return false;
        const Slot s = slot_[slot];
        slot_[slot] = Slot();
        if (key) *key = {s.vk, s.extended};
        bool rs = false;
        if (s.usedShift && shiftByUs_ && !shiftSlotHeld && !anyUsingShift_()) {
            shiftByUs_ = false;
            rs = true;
        }
        if (releaseShift) *releaseShift = rs;
        return true;
    }

    bool isDown(size_t slot) const { return slot < kSlots && slot_[slot].down; }
    bool shiftByUs() const { return shiftByUs_; }
    // The recorded key of a pressed slot (false if not pressed).
    bool recorded(size_t slot, KeyRelease *key) const {
        if (slot >= kSlots || !slot_[slot].down) return false;
        if (key) *key = {slot_[slot].vk, slot_[slot].extended};
        return true;
    }
    void setShiftSlot(size_t slot) { shiftSlot_ = slot; }

    // Everything this process holds, as key-ups: the tracked keys with their recorded codes, then
    // the synthetic Shift (as `shift`) if it is still ours. Clears the state.
    std::vector<KeyRelease> releaseAll(KeyRelease shift) {
        std::vector<KeyRelease> out;
        for (size_t i = 0; i < kSlots; ++i) {
            if (!slot_[i].down) continue;
            out.push_back({slot_[i].vk, slot_[i].extended});
            slot_[i] = Slot();
        }
        if (shiftByUs_) {
            shiftByUs_ = false;
            out.push_back(shift);
        }
        return out;
    }

private:
    static constexpr size_t kSlots = kKeyCount;
    struct Slot {
        bool down = false;
        uint16_t vk = 0;
        bool extended = false;
        bool usedShift = false;
    };
    bool isShiftSlot_(size_t slot) const { return slot == shiftSlot_; }
    bool anyUsingShift_() const {
        for (size_t i = 0; i < kSlots; ++i)
            if (slot_[i].down && slot_[i].usedShift) return true;
        return false;
    }
    Slot slot_[kSlots];
    size_t shiftSlot_ = static_cast<size_t>(-1);
    bool shiftByUs_ = false;
};

}  // namespace winkeys
}  // namespace spn
