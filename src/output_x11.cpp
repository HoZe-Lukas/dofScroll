// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// X11 output via the XTest extension.
#include "key_tables.hpp"
#include "output.hpp"
#include "xerrors.hpp"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <set>

namespace spn {
namespace {

// Upper bound for clicks per call (protects against INT_MIN negation and event floods).
constexpr int kMaxSteps = 200;
int clampSteps(int n) { return n > kMaxSteps ? kMaxSteps : (n < -kMaxSteps ? -kMaxSteps : n); }

class X11Output : public Output {
public:
    ~X11Output() override {
        if (dpy_) {
            // In the destructor use the non-virtual helper (no virtual dispatch)
            doReleaseAll();
            XFlush(dpy_);
            XCloseDisplay(dpy_);
        }
    }

    const char *name() const override { return "x11"; }

    bool init(std::string *err) override {
        installXErrorHandlers();  // shared (xerrors.hpp), idempotent
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_) {
            if (err) *err = "X11: XOpenDisplay failed (no DISPLAY or X server unreachable)";
            return false;
        }
        int ev = 0, er = 0, maj = 0, min = 0;
        if (!XTestQueryExtension(dpy_, &ev, &er, &maj, &min)) {
            if (err) *err = "X11: XTest extension not available";
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
            return false;
        }
        for (size_t i = 0; i < kKeyCount; ++i)
            codes_[i] = XKeysymToKeycode(dpy_, keyToKeysym(static_cast<Key>(i)));
        return true;
    }

    void wheelV(int n) override {
        n = clampSteps(n);
        click(n > 0 ? 5 : 4, n < 0 ? -n : n);
    }
    void wheelH(int n) override {
        n = clampSteps(n);
        click(n > 0 ? 7 : 6, n < 0 ? -n : n);
    }

    void setKey(Key k, bool down) override {
        if (!dpy_) return;
        const unsigned kc = code(k);
        if (!kc) return;
        if (down == (keysDown_.count(kc) != 0)) return;  // idempotent
        XTestFakeKeyEvent(dpy_, kc, down ? True : False, CurrentTime);
        if (down) keysDown_.insert(kc); else keysDown_.erase(kc);
    }

    void tapKey(Key k) override {
        if (!dpy_) return;
        const unsigned kc = code(k);
        if (!kc) return;
        XTestFakeKeyEvent(dpy_, kc, True, CurrentTime);
        XTestFakeKeyEvent(dpy_, kc, False, CurrentTime);
        keysDown_.erase(kc);
    }

    void mouseButton(int button, bool down) override {
        if (!dpy_ || button < 1 || button > 3) return;
        if (down == (buttonsDown_.count(button) != 0)) return;
        XTestFakeButtonEvent(dpy_, button, down ? True : False, CurrentTime);
        if (down) buttonsDown_.insert(button); else buttonsDown_.erase(button);
    }

    void releaseAll() override { doReleaseAll(); }

    void forceReleaseAll() override {
        if (!dpy_) return;
        // Independent of our own state: a crashed earlier process may have left keys down.
        for (unsigned kc : codes_)
            if (kc) XTestFakeKeyEvent(dpy_, kc, False, CurrentTime);
        // Right-hand modifier variants are not in the Key table but may be stuck too.
        const KeySym extra[] = {XK_Control_R, XK_Shift_R, XK_Alt_R, XK_Super_R};
        for (KeySym ks : extra) {
            const unsigned kc = XKeysymToKeycode(dpy_, ks);
            if (kc) XTestFakeKeyEvent(dpy_, kc, False, CurrentTime);
        }
        for (int b = 1; b <= 3; ++b) XTestFakeButtonEvent(dpy_, b, False, CurrentTime);
        doReleaseAll();
    }

    void flush() override { if (dpy_) XFlush(dpy_); }

private:
    void doReleaseAll() {
        if (!dpy_) return;
        for (unsigned kc : keysDown_) XTestFakeKeyEvent(dpy_, kc, False, CurrentTime);
        keysDown_.clear();
        for (int b : buttonsDown_) XTestFakeButtonEvent(dpy_, b, False, CurrentTime);
        buttonsDown_.clear();
    }

    void click(int button, int count) {
        if (!dpy_) return;
        for (int i = 0; i < count; ++i) {
            XTestFakeButtonEvent(dpy_, button, True, CurrentTime);
            XTestFakeButtonEvent(dpy_, button, False, CurrentTime);
        }
    }
    unsigned code(Key k) const { return codes_[static_cast<size_t>(k)]; }

    Display *dpy_ = nullptr;
    unsigned codes_[kKeyCount] = {};
    std::set<unsigned> keysDown_;
    std::set<int> buttonsDown_;
};

}  // namespace

std::unique_ptr<Output> makeX11Output() { return std::make_unique<X11Output>(); }

}  // namespace spn
