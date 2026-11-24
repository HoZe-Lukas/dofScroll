// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// X11 output via the XTest extension.
#include "key_tables.hpp"
#include "output.hpp"
#include "xerrors.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>

#include <poll.h>

#include <chrono>
#include <set>
#include <string>

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
            if (win_) XDestroyWindow(dpy_, win_);
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
        loadKeycodes();
        // Clipboard (paste: step): an unmapped 1x1 window owns the selection; atoms are looked up once.
        win_ = XCreateSimpleWindow(dpy_, DefaultRootWindow(dpy_), 0, 0, 1, 1, 0, 0, 0);
        aClipboard_ = XInternAtom(dpy_, "CLIPBOARD", False);
        aUtf8_ = XInternAtom(dpy_, "UTF8_STRING", False);
        aTargets_ = XInternAtom(dpy_, "TARGETS", False);
        aText_ = XInternAtom(dpy_, "TEXT", False);
        aProp_ = XInternAtom(dpy_, "DOFSCROLL_CLIPBOARD", False);
        return true;
    }

    // Paste: step. The text goes to the CLIPBOARD selection, which this process owns until restoreClipboard(); requests
    // are answered from flush() (once per tick). What the clipboard held before is read as UTF-8 text (best effort, 100 ms,
    // text only) and offered again by restoreClipboard(); without such text the selection is just released.
    bool setClipboardText(const std::string &text, std::string *why) override {
        if (!dpy_ || !win_) {
            if (why) *why = "x11: no connection for the clipboard";
            return false;
        }
        if (!ownsClipboard_) hadSaved_ = readClipboard(&saved_);
        own_ = text;
        XSetSelectionOwner(dpy_, aClipboard_, win_, CurrentTime);
        XFlush(dpy_);
        if (XGetSelectionOwner(dpy_, aClipboard_) != win_) {
            if (why) *why = "x11: could not become the owner of the clipboard";
            return false;
        }
        ownsClipboard_ = true;
        return true;
    }
    void restoreClipboard() override {
        if (!dpy_ || !ownsClipboard_) return;
        if (hadSaved_) {
            own_ = saved_;  // still the owner, now offering the old text
        } else {
            XSetSelectionOwner(dpy_, aClipboard_, None, CurrentTime);
            own_.clear();
            ownsClipboard_ = false;
        }
        saved_.clear();
        hadSaved_ = false;
        XFlush(dpy_);
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
        if (k == Key::Plus && down) resolvePlus();
        // The release goes to the keycode used at the press: the keymap (and with it code(k)) may
        // have changed while the key was held, and the old keycode must not stay down.
        const size_t idx = static_cast<size_t>(k);
        const unsigned kc = (!down && heldCode_[idx]) ? heldCode_[idx] : code(k);
        if (!kc) return;
        if (down == (keysDown_.count(kc) != 0)) return;  // idempotent
        if (k == Key::Plus && down) pressPlusShift();
        XTestFakeKeyEvent(dpy_, kc, down ? True : False, CurrentTime);
        if (down) keysDown_.insert(kc); else keysDown_.erase(kc);
        heldCode_[idx] = down ? kc : 0;
        if (k == Key::Plus && !down) releasePlusShift();
    }

    void tapKey(Key k) override {
        if (!dpy_) return;
        if (k == Key::Plus) resolvePlus();
        const unsigned kc = code(k);
        if (!kc) return;
        const bool plus = k == Key::Plus;
        // A key already held by a channel is released, tapped and pressed again (like the
        // uinput backend), so keysDown_ stays consistent with the ModeRunner's view.
        const bool wasDown = keysDown_.count(kc) != 0;
        if (plus) pressPlusShift();
        if (wasDown) XTestFakeKeyEvent(dpy_, kc, False, CurrentTime);
        XTestFakeKeyEvent(dpy_, kc, True, CurrentTime);
        XTestFakeKeyEvent(dpy_, kc, False, CurrentTime);
        if (wasDown) XTestFakeKeyEvent(dpy_, kc, True, CurrentTime);
        if (plus) releasePlusShift();
    }

    void mouseButton(int button, bool down) override {
        if (!dpy_ || button < 1 || button > 3) return;
        if (down == (buttonsDown_.count(button) != 0)) return;
        XTestFakeButtonEvent(dpy_, button, down ? True : False, CurrentTime);
        if (down) buttonsDown_.insert(button); else buttonsDown_.erase(button);
    }

    void moveMouse(int dx, int dy) override {
        dx = clampMouseDelta(dx);
        dy = clampMouseDelta(dy);
        if (!dpy_ || (dx == 0 && dy == 0)) return;
        XTestFakeRelativeMotionEvent(dpy_, dx, dy, CurrentTime);
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
        doReleaseAll();  // also releases a Shift pressed for "plus"
    }

    void flush() override {
        if (!dpy_) return;
        XFlush(dpy_);
        followKeymap();
    }

private:
    // Keys are sent as keycodes, looked up from the keysyms. The keyboard layout can change while we
    // run (the desktop applies the user's layout after login, setxkbmap, xmodmap): the server then
    // sends MappingNotify. Xlib keeps its own keymap cache that only XRefreshKeyboardMapping
    // updates, and codes_ was built from it, so both are refreshed here. Called once per tick from
    // flush(); XPending reads what the server sent without blocking. Other events are dropped.
    void followKeymap() {
        bool remapped = false;
        while (XPending(dpy_) > 0) {
            XEvent ev;
            XNextEvent(dpy_, &ev);
            handleEvent(ev, &remapped);
        }
        if (remapped) loadKeycodes();
    }
    void handleEvent(XEvent &ev, bool *remapped) {
        if (ev.type == MappingNotify && ev.xmapping.request != MappingPointer) {
            XRefreshKeyboardMapping(&ev.xmapping);
            *remapped = true;
        } else if (ev.type == SelectionRequest) {
            answerSelectionRequest(ev.xselectionrequest);
        } else if (ev.type == SelectionClear && ev.xselectionclear.selection == aClipboard_) {
            ownsClipboard_ = false;  // another program took the clipboard: nothing to restore any more
            hadSaved_ = false;
            own_.clear();
            saved_.clear();
        }
    }
    // Answers a request for the text we own: TARGETS, UTF8_STRING and TEXT (UTF-8 bytes), STRING for pure ASCII.
    void answerSelectionRequest(const XSelectionRequestEvent &rq) {
        XSelectionEvent r = {};
        r.type = SelectionNotify;
        r.display = rq.display;
        r.requestor = rq.requestor;
        r.selection = rq.selection;
        r.target = rq.target;
        r.time = rq.time;
        r.property = None;
        const Atom prop = rq.property != None ? rq.property : rq.target;
        if (ownsClipboard_ && rq.selection == aClipboard_) {
            bool ascii = true;
            for (unsigned char c : own_)
                if (c >= 0x80) ascii = false;
            if (rq.target == aTargets_) {
                const Atom list[] = {aTargets_, aUtf8_, aText_, XA_STRING};
                XChangeProperty(dpy_, rq.requestor, prop, XA_ATOM, 32, PropModeReplace, reinterpret_cast<const unsigned char *>(list), ascii ? 4 : 3);
                r.property = prop;
            } else if (rq.target == aUtf8_ || rq.target == aText_ || (rq.target == XA_STRING && ascii)) {
                XChangeProperty(dpy_, rq.requestor, prop, rq.target == aText_ ? aUtf8_ : rq.target, 8, PropModeReplace,
                                reinterpret_cast<const unsigned char *>(own_.data()), static_cast<int>(own_.size()));
                r.property = prop;
            }
        }
        XSendEvent(dpy_, rq.requestor, False, NoEventMask, reinterpret_cast<XEvent *>(&r));
        XFlush(dpy_);
    }
    // The text of the clipboard (owned by another program), as UTF-8. false if there is none, it is not text or it does not
    // arrive within 100 ms. Never blocks longer; other events that arrive meanwhile are handled.
    bool readClipboard(std::string *out) {
        if (XGetSelectionOwner(dpy_, aClipboard_) == None) return false;
        XDeleteProperty(dpy_, win_, aProp_);
        XConvertSelection(dpy_, aClipboard_, aUtf8_, aProp_, win_, CurrentTime);
        XFlush(dpy_);
        const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
        bool remapped = false;
        for (;;) {
            while (XPending(dpy_) > 0) {
                XEvent ev;
                XNextEvent(dpy_, &ev);
                if (ev.type == SelectionNotify && ev.xselection.requestor == win_) {
                    if (ev.xselection.property == None) return false;
                    Atom type = None;
                    int fmt = 0;
                    unsigned long n = 0, left = 0;
                    unsigned char *data = nullptr;
                    g_xerror = 0;
                    const int rc = XGetWindowProperty(dpy_, win_, aProp_, 0, 16384, True, AnyPropertyType, &type, &fmt, &n, &left, &data);
                    bool ok = false;
                    if (rc == Success && data && type == aUtf8_ && fmt == 8 && left == 0 && !g_xerror) {
                        out->assign(reinterpret_cast<char *>(data), n);
                        ok = true;
                    }
                    if (data) XFree(data);
                    return ok;
                }
                handleEvent(ev, &remapped);
            }
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count();
            if (left <= 0) return false;
            pollfd pfd = {ConnectionNumber(dpy_), POLLIN, 0};
            poll(&pfd, 1, static_cast<int>(left));
        }
    }
    void loadKeycodes() {
        for (size_t i = 0; i < kKeyCount; ++i)
            codes_[i] = XKeysymToKeycode(dpy_, keyToKeysym(static_cast<Key>(i)));
    }

    void doReleaseAll() {
        if (!dpy_) return;
        for (unsigned kc : keysDown_) XTestFakeKeyEvent(dpy_, kc, False, CurrentTime);
        keysDown_.clear();
        for (unsigned &h : heldCode_) h = 0;
        releasePlusShift();
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
    // Resolves "plus" (keysym XK_plus) against the current keyboard map: keycode and whether
    // Shift is needed (keysym on level 1). Only group 0 is considered, so with another active
    // group the result may differ. Falls back to the keypad plus (KP_Add) if not found.
    void resolvePlus() {
        const size_t idx = static_cast<size_t>(Key::Plus);
        plusShift_ = false;
        const KeyCode kc = XKeysymToKeycode(dpy_, XK_plus);
        if (kc) {
            if (XkbKeycodeToKeysym(dpy_, kc, 0, 0) == XK_plus) {
                codes_[idx] = kc;
                return;
            }
            if (XkbKeycodeToKeysym(dpy_, kc, 0, 1) == XK_plus) {
                codes_[idx] = kc;
                plusShift_ = true;
                return;
            }
        }
        codes_[idx] = XKeysymToKeycode(dpy_, XK_KP_Add);
    }
    // Shift around "plus" is only pressed if the user does not already hold Shift.
    void pressPlusShift() {
        if (!plusShift_ || shiftByUs_) return;
        const unsigned sh = code(Key::Shift);
        if (!sh || keysDown_.count(sh)) return;
        XTestFakeKeyEvent(dpy_, sh, True, CurrentTime);
        shiftByUs_ = true;
    }
    void releasePlusShift() {
        if (!shiftByUs_) return;
        shiftByUs_ = false;
        XTestFakeKeyEvent(dpy_, code(Key::Shift), False, CurrentTime);
    }
    unsigned code(Key k) const { return codes_[static_cast<size_t>(k)]; }

    Display *dpy_ = nullptr;
    Window win_ = 0;                       // owner window of the clipboard
    Atom aClipboard_ = None, aUtf8_ = None, aTargets_ = None, aText_ = None, aProp_ = None;
    bool ownsClipboard_ = false;           // we own the CLIPBOARD selection (a paste: step is running)
    bool hadSaved_ = false;                // saved_ holds the text that was on the clipboard before
    std::string own_, saved_;              // the text we offer, and the one to offer again afterwards
    unsigned codes_[kKeyCount] = {};
    unsigned heldCode_[kKeyCount] = {};  // keycode a Key was pressed with by setKey(), 0 = not held
    bool plusShift_ = false;   // "plus" needs Shift on the current layout
    bool shiftByUs_ = false;   // Shift pressed on behalf of "plus"
    std::set<unsigned> keysDown_;
    std::set<int> buttonsDown_;
};

}  // namespace

std::unique_ptr<Output> makeX11Output() { return std::make_unique<X11Output>(); }

}  // namespace spn
