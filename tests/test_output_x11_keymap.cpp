// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The X11 output must follow a keyboard layout change that happens after it was created: the desktop
// applies the user's layout (setxkbmap) a moment after login, often after the daemon started. Keys are
// sent as keycodes, so a table built once at start would press the keys of the OLD layout (on a German
// layout "minus" would type the sharp s, "z" would type "y").
//
// Needs an X server with XTEST and the setxkbmap program: run through xvfb-run (CMake does that when
// it finds it). Without them the test is skipped (exit code 77, CTest SKIP_RETURN_CODE).
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include "output.hpp"

using namespace spn;

namespace {

int skip(const char *why) {
    std::printf("test_output_x11_keymap: skipped (%s)\n", why);
    return 77;
}

// The keysym a KeyPress event produces, looked up in a connection that opened AFTER the layout change
// (so its keymap cache is current): level 1 when Shift is part of the state, else level 0.
KeySym keysymOf(Display *fresh, const XKeyEvent &e) {
    return XkbKeycodeToKeysym(fresh, static_cast<KeyCode>(e.keycode), 0, (e.state & ShiftMask) ? 1 : 0);
}

// Is the key with this keycode logically down right now (XQueryKeymap)? The output runs on its own
// connection, so the server may not have seen its requests yet: wait up to a second for `wanted`.
bool waitKeyState(Display *d, KeyCode kc, bool wanted) {
    for (int i = 0; i < 100; ++i) {
        char keys[32];
        XQueryKeymap(d, keys);
        if (((keys[kc / 8] & (1 << (kc % 8))) != 0) == wanted) return true;
        usleep(10 * 1000);
    }
    return false;
}

struct Pressed {
    std::vector<KeySym> syms;  // every non-modifier KeyPress, in order
};

// Collects the typed keys until `expected` arrived (or a second passed).
Pressed collect(Display *obs, Display *fresh, size_t expected) {
    Pressed p;
    for (int i = 0; i < 100 && p.syms.size() < expected; ++i) {
        XSync(obs, False);
        while (XPending(obs) > 0) {
            XEvent ev;
            XNextEvent(obs, &ev);
            if (ev.type == MappingNotify) {
                XRefreshKeyboardMapping(&ev.xmapping);
                continue;
            }
            if (ev.type != KeyPress) continue;
            const KeySym ks = keysymOf(fresh, ev.xkey);
            if (ks == XK_Shift_L || ks == XK_Shift_R) continue;
            p.syms.push_back(ks);
        }
        if (p.syms.size() < expected) usleep(10 * 1000);
    }
    return p;
}

}  // namespace

int main() {
    Display *obs = XOpenDisplay(nullptr);
    if (!obs) return skip("no X display");
    int ev = 0, er = 0, maj = 0, min = 0;
    if (!XTestQueryExtension(obs, &ev, &er, &maj, &min)) return skip("no XTEST extension");
    if (std::system("setxkbmap us >/dev/null 2>&1") != 0) return skip("setxkbmap not available");

    std::unique_ptr<Output> out = makeX11Output();
    std::string err;
    const bool inited = out->init(&err);  // built on the US layout
    assert(inited);
    (void)inited;

    // The observer: a focused window that receives the key events.
    const Window w = XCreateSimpleWindow(obs, DefaultRootWindow(obs), 0, 0, 10, 10, 0, 0, 0);
    XSelectInput(obs, w, KeyPressMask);
    XMapWindow(obs, w);
    XSetInputFocus(obs, w, RevertToParent, CurrentTime);
    XSync(obs, False);

    // Sanity: on the layout it was built for, the output types what it should.
    {
        Display *fresh = XOpenDisplay(nullptr);
        out->flush();
        out->tapKey(Key::Z);
        out->tapKey(Key::Minus);
        out->flush();
        const Pressed p = collect(obs, fresh, 2);
        assert(p.syms.size() == 2);
        assert(p.syms[0] == XK_z);
        assert(p.syms[1] == XK_minus);
        XCloseDisplay(fresh);
    }

    // A key held while the layout changes must still be released with the keycode it was pressed with.
    const KeyCode zUs = XKeysymToKeycode(obs, XK_z);
    out->setKey(Key::Z, true);
    out->flush();
    assert(waitKeyState(obs, zUs, true));

    // The desktop switches to the German layout after the output exists (y and z swap, minus moves,
    // plus is a plain key).
    if (std::system("setxkbmap de >/dev/null 2>&1") != 0) {
        out->setKey(Key::Z, false);
        return skip("layout de not installed");
    }
    out->flush();  // the output notices the new keymap here
    out->setKey(Key::Z, false);
    out->flush();
    assert(waitKeyState(obs, zUs, false));  // not stuck on the old keycode
    (void)collect(obs, obs, 1);             // drop the press event of the held key
    {
        Display *fresh = XOpenDisplay(nullptr);
        // The daemon calls flush() once per tick: that is where the output notices the new keymap.
        out->flush();
        out->tapKey(Key::Z);
        out->tapKey(Key::Minus);
        out->tapKey(Key::Plus);
        out->flush();
        const Pressed p = collect(obs, fresh, 3);
        std::printf("typed after the switch to de: %zu keys\n", p.syms.size());
        assert(p.syms.size() == 3);
        assert(p.syms[0] == XK_z);      // not "y"
        assert(p.syms[1] == XK_minus);  // not the sharp s
        assert(p.syms[2] == XK_plus);   // not the dead acute accent
        XCloseDisplay(fresh);
    }

    out.reset();
    XCloseDisplay(obs);
    if (std::system("setxkbmap us >/dev/null 2>&1") != 0) std::puts("note: could not restore the us layout");
    std::puts("test_output_x11_keymap: all tests passed");
    return 0;
}
