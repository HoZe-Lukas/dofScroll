// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows event output with SendInput: wheel, horizontal wheel, keys, mouse buttons 1-3. Same
// semantics as the X11/uinput outputs: idempotent setKey, tapKey on a held key releases and
// re-presses it, releaseAll releases exactly what we hold (tracked in win_pressed.hpp).
// Limits (expected Windows behavior, see docs/port-windows.md): input to elevated windows is blocked
// by UIPI unless the daemon is elevated too, and the secure desktop (lock screen, UAC) cannot be
// injected into; SendInput then injects fewer events than requested, which is logged once.
#ifndef _WIN32
#error "win_output.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iostream>
#include <vector>

#include "win_key_tables.hpp"
#include "win_output.hpp"
#include "win_pressed.hpp"

namespace spn {
namespace {

bool g_blanketRelease = false;

class WinOutput : public Output {
public:
    WinOutput() { pressed_.setShiftSlot(idx(Key::Shift)); }
    ~WinOutput() override { doReleaseAll(); }

    const char *name() const override { return "sendinput"; }
    bool init(std::string *) override { return true; }

    void wheelV(int n) override {
        const int32_t d = winkeys::wheelDetentV(n);
        if (d == 0) return;
        std::vector<INPUT> in;
        for (int i = 0; i < winkeys::wheelCount(n); ++i) in.push_back(wheelInput(winkeys::kMouseEventWheel, d));
        send(in);
    }
    void wheelH(int n) override {
        const int32_t d = winkeys::wheelDetentH(n);
        if (d == 0) return;
        std::vector<INPUT> in;
        for (int i = 0; i < winkeys::wheelCount(n); ++i) in.push_back(wheelInput(winkeys::kMouseEventHWheel, d));
        send(in);
    }

    void setKey(Key k, bool down) override {
        const size_t i = idx(k);
        std::vector<INPUT> in;
        if (down) {
            if (pressed_.isDown(i)) return;
            const winkeys::WinKeySpec s = spec(k);  // resolved once, recorded for the key-up
            if (pressed_.press(i, s.vk, s.extended, s.shift, pressed_.isDown(idx(Key::Shift)), shiftPhysical()))
                in.push_back(keyInput(winkeys::keyToWin(Key::Shift), true));
            in.push_back(rawKey(s.vk, s.extended, true));
        } else {
            winkeys::KeyRelease r;
            bool relShift = false;
            if (!pressed_.release(i, pressed_.isDown(idx(Key::Shift)), &r, &relShift)) return;  // not ours
            in.push_back(rawKey(r.vk, r.extended, false));
            if (relShift) in.push_back(keyInput(winkeys::keyToWin(Key::Shift), false));
        }
        send(in);
    }

    void tapKey(Key k) override {
        const size_t i = idx(k);
        std::vector<INPUT> in;
        winkeys::KeyRelease held;
        const bool wasDown = pressed_.recorded(i, &held);
        const winkeys::WinKeySpec s = wasDown ? winkeys::WinKeySpec{held.vk, held.extended, false} : spec(k);
        const bool shiftAdded = !wasDown && s.shift && !pressed_.isDown(idx(Key::Shift)) && !pressed_.shiftByUs() &&
                                !shiftPhysical();
        if (shiftAdded) in.push_back(keyInput(winkeys::keyToWin(Key::Shift), true));
        if (wasDown) in.push_back(keyInput(s, false));
        in.push_back(keyInput(s, true));
        in.push_back(keyInput(s, false));
        if (wasDown) in.push_back(keyInput(s, true));
        if (shiftAdded) in.push_back(keyInput(winkeys::keyToWin(Key::Shift), false));
        send(in);
    }

    void mouseButton(int button, bool down) override {
        const winkeys::MouseButtonFlags f = winkeys::mouseButtonFlags(button);
        if (!f.down) return;
        bool &st = btnHeld_[button - 1];
        if (st == down) return;
        st = down;
        std::vector<INPUT> in;
        in.push_back(mouseInput(down ? f.down : f.up));
        send(in);
    }

    // Relative pointer movement (MOUSEEVENTF_MOVE without ABSOLUTE). Untested on hardware.
    void moveMouse(int dx, int dy) override {
        dx = clampMouseDelta(dx);
        dy = clampMouseDelta(dy);
        if (dx == 0 && dy == 0) return;
        INPUT i = mouseInput(MOUSEEVENTF_MOVE);
        i.mi.dx = dx;
        i.mi.dy = dy;
        std::vector<INPUT> in;
        in.push_back(i);
        send(in);
    }

    void releaseAll() override { doReleaseAll(); }

    // Called at every daemon start and by --release-keys. Normally it only releases what this
    // process holds (nothing at start): a blanket key-up would release modifiers the user holds
    // physically and a bare Alt/Win key-up opens the menu bar or the Start menu. The blanket
    // release (for keys stuck after a crash) is only done for --release-keys (setBlanketRelease).
    void forceReleaseAll() override {
        if (blanket_) {
            std::vector<INPUT> in;
            for (size_t i = 0; i < kKeyCount; ++i) {
                const winkeys::WinKeySpec s = winkeys::keyToWin(static_cast<Key>(i));
                if (!winkeys::isMenuTriggerVk(s.vk)) in.push_back(keyInput(s, false));
            }
            const winkeys::WinKeySpec plus = spec(Key::Plus);
            in.push_back(keyInput(plus, false));  // the layout-resolved "plus"
            for (const winkeys::ExtraRelease &e : winkeys::kExtraReleases)
                if (!winkeys::isMenuTriggerVk(e.vk)) in.push_back(rawKey(e.vk, e.extended, false));
            for (int b = 1; b <= 3; ++b) in.push_back(mouseInput(winkeys::mouseButtonFlags(b).up));
            send(in);
        }
        doReleaseAll();
    }
    void setBlanketRelease(bool on) { blanket_ = on; }

    void flush() override {}

private:
    static size_t idx(Key k) { return static_cast<size_t>(k); }

    // "plus" is layout dependent (a key with Shift on US, a plain key on German): ask the
    // current layout for '+'. Falls back to the keypad plus.
    static winkeys::WinKeySpec spec(Key k) {
        winkeys::WinKeySpec s = winkeys::keyToWin(k);
        if (k == Key::Plus) {
            const SHORT r = VkKeyScanW(L'+');
            if (r == -1) return winkeys::keyToWin(Key::KpPlus);
            const unsigned vk = static_cast<unsigned>(r) & 0xFFu;
            const unsigned st = (static_cast<unsigned>(r) >> 8) & 0xFFu;
            // Only plain or Shift-only combinations are usable (no Ctrl/Alt/AltGr levels).
            if (st & ~1u) return winkeys::keyToWin(Key::KpPlus);
            s.vk = static_cast<uint16_t>(vk);
            s.shift = (st & 1u) != 0;
        }
        return s;
    }

    // The user holds Shift on the keyboard (then it must not be pressed or released for "plus").
    static bool shiftPhysical() { return (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0; }

    static INPUT rawKey(uint16_t vk, bool extended, bool down) {
        INPUT in = {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        in.ki.dwFlags = (extended ? KEYEVENTF_EXTENDEDKEY : 0u) | (down ? 0u : KEYEVENTF_KEYUP);
        return in;
    }
    static INPUT keyInput(const winkeys::WinKeySpec &s, bool down) { return rawKey(s.vk, s.extended, down); }
    static INPUT mouseInput(uint32_t flags) {
        INPUT in = {};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = flags;
        return in;
    }
    static INPUT wheelInput(uint32_t flags, int32_t amount) {
        INPUT in = mouseInput(flags);
        in.mi.mouseData = static_cast<DWORD>(amount);  // negative values are the two's complement
        return in;
    }

    // Releases exactly what this process pressed, with the codes used at key-down.
    void doReleaseAll() {
        std::vector<INPUT> in;
        for (const winkeys::KeyRelease &r : pressed_.releaseAll({winkeys::keyToWin(Key::Shift).vk, false}))
            in.push_back(rawKey(r.vk, r.extended, false));
        for (int b = 0; b < 3; ++b) {
            if (!btnHeld_[b]) continue;
            btnHeld_[b] = false;
            in.push_back(mouseInput(winkeys::mouseButtonFlags(b + 1).up));
        }
        send(in);
    }

    // One SendInput call for all events of an operation (they are injected back to back).
    void send(const std::vector<INPUT> &in) {
        if (in.empty()) return;
        const UINT n = SendInput(static_cast<UINT>(in.size()), const_cast<INPUT *>(in.data()), sizeof(INPUT));
        if (n != in.size() && !warned_) {
            warned_ = true;
            std::cerr << "[dofscroll] WARNING: SendInput injected " << n << " of " << in.size()
                      << " events (error " << GetLastError()
                      << "): input to elevated windows and the secure desktop is blocked unless the daemon runs"
                         " elevated"
                      << std::endl;
        }
    }

    winkeys::PressedKeys pressed_;  // keys this process holds (with the codes used at key-down)
    bool btnHeld_[3] = {};
    bool blanket_ = false;
    bool warned_ = false;
};

}  // namespace

void setWinBlanketRelease(bool on) { g_blanketRelease = on; }

std::unique_ptr<Output> makeWinOutput(std::string *log) {
    if (log) *log = "SendInput";
    auto out = std::make_unique<WinOutput>();
    out->setBlanketRelease(g_blanketRelease);
    return out;
}

}  // namespace spn
