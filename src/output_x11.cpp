// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// X11-Ausgabe ueber die XTest-Extension.
#include "output.hpp"
#include "xerrors.hpp"

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <set>

namespace spn {
namespace {

// Obergrenze fuer Klicks pro Aufruf (Schutz vor INT_MIN-Negation und Event-Fluten).
constexpr int kMaxSteps = 200;
int clampSteps(int n) { return n > kMaxSteps ? kMaxSteps : (n < -kMaxSteps ? -kMaxSteps : n); }

class X11Output : public Output {
public:
    ~X11Output() override {
        if (dpy_) {
            // Im Destruktor die eigene Implementierung ueber nicht-virtuellen Helfer nutzen
            doReleaseAll();
            XFlush(dpy_);
            XCloseDisplay(dpy_);
        }
    }

    const char *name() const override { return "x11"; }

    bool init(std::string *err) override {
        installXErrorHandlers();  // gemeinsam (xerrors.hpp), idempotent
        dpy_ = XOpenDisplay(nullptr);
        if (!dpy_) {
            if (err) *err = "X11: XOpenDisplay fehlgeschlagen (kein DISPLAY oder X-Server nicht erreichbar)";
            return false;
        }
        int ev = 0, er = 0, maj = 0, min = 0;
        if (!XTestQueryExtension(dpy_, &ev, &er, &maj, &min)) {
            if (err) *err = "X11: XTest-Extension nicht verfuegbar";
            XCloseDisplay(dpy_);
            dpy_ = nullptr;
            return false;
        }
        kCtrl_ = XKeysymToKeycode(dpy_, XK_Control_L);
        kUp_ = XKeysymToKeycode(dpy_, XK_Up);
        kDown_ = XKeysymToKeycode(dpy_, XK_Down);
        kLeft_ = XKeysymToKeycode(dpy_, XK_Left);
        kRight_ = XKeysymToKeycode(dpy_, XK_Right);
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
        if (!dpy_ || button < 1) return;
        if (down == (buttonsDown_.count(button) != 0)) return;
        XTestFakeButtonEvent(dpy_, button, down ? True : False, CurrentTime);
        if (down) buttonsDown_.insert(button); else buttonsDown_.erase(button);
    }

    void releaseAll() override { doReleaseAll(); }

    void forceReleaseAll() override {
        if (!dpy_) return;
        const unsigned codes[] = {kCtrl_, XKeysymToKeycode(dpy_, XK_Control_R), kUp_, kDown_,
                                  kLeft_, kRight_};
        for (unsigned kc : codes)
            if (kc) XTestFakeKeyEvent(dpy_, kc, False, CurrentTime);
        XTestFakeButtonEvent(dpy_, 1, False, CurrentTime);
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
    unsigned code(Key k) const {
        switch (k) {
            case Key::Ctrl: return kCtrl_;
            case Key::Up: return kUp_;
            case Key::Down: return kDown_;
            case Key::Left: return kLeft_;
            case Key::Right: return kRight_;
        }
        return 0;
    }

    Display *dpy_ = nullptr;
    unsigned kCtrl_ = 0, kUp_ = 0, kDown_ = 0, kLeft_ = 0, kRight_ = 0;
    std::set<unsigned> keysDown_;
    std::set<int> buttonsDown_;
};

}  // namespace

std::unique_ptr<Output> makeX11Output() { return std::make_unique<X11Output>(); }

}  // namespace spn
