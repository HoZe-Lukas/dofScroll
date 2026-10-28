// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// uinput backend: creates a virtual input device (Wayland-capable, no X11 needed).
// Limitation: uinput knows nothing about the keyboard layout. The key "plus" is sent as
// Shift+KEY_EQUAL, which yields "+" on a US layout only; other layouts may produce another
// character. Use "kp_plus" for a layout independent plus.
#include "key_tables.hpp"
#include "output.hpp"

#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <vector>

namespace spn {

namespace {

int keyCode(Key k) { return keyToLinuxCode(k); }

int keyIndex(Key k) { return static_cast<int>(k); }

// Upper bound for wheel steps per call (protects against overflow/floods from extreme config values).
constexpr int kMaxSteps = 200;
// Upper bound for unsent events in the buffer (backlog on EAGAIN).
constexpr size_t kMaxBuffered = 4096;

int clampSteps(int n) { return n > kMaxSteps ? kMaxSteps : (n < -kMaxSteps ? -kMaxSteps : n); }

class UinputOutput : public Output {
public:
    ~UinputOutput() override {
        if (fd_ >= 0) {
            // In the destructor use the non-virtual helpers (no virtual dispatch)
            doReleaseAll();
            doFlush();
            ioctl(fd_, UI_DEV_DESTROY);
            close(fd_);
        }
    }

    const char *name() const override { return "uinput"; }

    bool init(std::string *err) override {
        fd_ = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd_ < 0) {
            fd_ = open("/dev/input/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        }
        if (fd_ < 0) {
            int e = errno;
            setErr(err, std::string("cannot open /dev/uinput: ") +
                            std::strerror(e) +
                            (e == ENOENT
                                 ? " (load the 'uinput' kernel module: modprobe uinput)"
                                 : " (missing permissions: install the udev rule packaging/"
                                   "70-dofscroll-uinput.rules and log in "
                                   "again (uaccess), see "
                                   "packaging/README-uinput.md)"));
            return false;
        }

        bool ok = true;
        auto bit = [&](unsigned long req, int v) {
            if (ioctl(fd_, req, v) < 0) ok = false;
        };
        bit(UI_SET_EVBIT, EV_KEY);
        bit(UI_SET_EVBIT, EV_REL);
        bit(UI_SET_EVBIT, EV_SYN);
        for (int b = 1; b <= 3; ++b) bit(UI_SET_KEYBIT, mouseButtonToLinuxCode(b));
        for (size_t i = 0; i < kKeyCount; ++i) bit(UI_SET_KEYBIT, keyCode(static_cast<Key>(i)));
        bit(UI_SET_RELBIT, REL_WHEEL);
        bit(UI_SET_RELBIT, REL_HWHEEL);
        // REL_X/REL_Y: pointer movement (moveMouse) and lets compositors recognise a mouse.
        bit(UI_SET_RELBIT, REL_X);
        bit(UI_SET_RELBIT, REL_Y);
#if defined(REL_WHEEL_HI_RES) && defined(REL_HWHEEL_HI_RES)
        // Hi-res is optional: fails on older kernels, which is not an error.
        if (ioctl(fd_, UI_SET_RELBIT, REL_WHEEL_HI_RES) == 0 &&
            ioctl(fd_, UI_SET_RELBIT, REL_HWHEEL_HI_RES) == 0) {
            hiRes_ = true;
        }
#endif
        if (!ok) {
            setErr(err, std::string("could not set uinput capabilities: ") +
                            std::strerror(errno));
            return fail();
        }

        uinput_setup us;
        std::memset(&us, 0, sizeof(us));
        us.id.bustype = BUS_VIRTUAL;
        us.id.vendor = 0x1209;
        us.id.product = 0x5350;
        us.id.version = 1;
        std::strncpy(us.name, "dofscroll virtual input", UINPUT_MAX_NAME_SIZE - 1);
        if (ioctl(fd_, UI_DEV_SETUP, &us) < 0 || ioctl(fd_, UI_DEV_CREATE) < 0) {
            setErr(err, std::string("could not create uinput device: ") +
                            std::strerror(errno));
            return fail();
        }
        created_ = true;
        // Compositors/libinput need a moment to register the new device.
        usleep(300 * 1000);
        return true;
    }

    void wheelV(int n) override {
        n = clampSteps(n);
        if (n == 0) return;
        // Convention: n > 0 = down = REL_WHEEL negative.
        wheel(REL_WHEEL,
#ifdef REL_WHEEL_HI_RES
              REL_WHEEL_HI_RES,
#else
              -1,
#endif
              -n);
    }

    void wheelH(int n) override {
        n = clampSteps(n);
        if (n == 0) return;
        // n > 0 = right = REL_HWHEEL positive.
        wheel(REL_HWHEEL,
#ifdef REL_HWHEEL_HI_RES
              REL_HWHEEL_HI_RES,
#else
              -1,
#endif
              n);
    }

    void setKey(Key k, bool down) override {
        bool &st = held_[keyIndex(k)];
        if (st == down) return;
        st = down;
        const bool sh = keyNeedsShift(k);
        const int shc = keyCode(Key::Shift);
        if (down && sh && !held_[keyIndex(Key::Shift)] && !shiftByUs_) {
            shiftByUs_ = true;
            push(EV_KEY, shc, 1);
        }
        push(EV_KEY, keyCode(k), down ? 1 : 0);
        if (!down && sh && shiftByUs_) {
            shiftByUs_ = false;
            push(EV_KEY, shc, 0);
        }
    }

    void tapKey(Key k) override {
        const bool shiftAdded = keyNeedsShift(k) && !held_[keyIndex(Key::Shift)] && !shiftByUs_;
        if (shiftAdded) {
            push(EV_KEY, keyCode(Key::Shift), 1);
            tapBody(k);
            push(EV_KEY, keyCode(Key::Shift), 0);
        } else {
            tapBody(k);
        }
    }

    void tapBody(Key k) {
        bool wasDown = held_[keyIndex(k)];
        if (wasDown) push(EV_KEY, keyCode(k), 0);
        push(EV_KEY, keyCode(k), 1);
        push(EV_SYN, SYN_REPORT, 0);
        push(EV_KEY, keyCode(k), 0);
        if (wasDown) {
            push(EV_SYN, SYN_REPORT, 0);
            push(EV_KEY, keyCode(k), 1);
        }
    }

    void mouseButton(int button, bool down) override {
        const int code = mouseButtonToLinuxCode(button);
        if (!code) return;
        bool &st = btnHeld_[button - 1];
        if (st == down) return;
        st = down;
        push(EV_KEY, code, down ? 1 : 0);
    }

    void moveMouse(int dx, int dy) override {
        dx = clampMouseDelta(dx);
        dy = clampMouseDelta(dy);
        if (dx != 0) push(EV_REL, REL_X, dx);
        if (dy != 0) push(EV_REL, REL_Y, dy);
    }

    void releaseAll() override { doReleaseAll(); }

    void forceReleaseAll() override {
        // Independent of our own state: a crashed earlier process may have left keys down.
        // Key-up events for keys that are not down are harmless.
        for (size_t i = 0; i < kKeyCount; ++i) push(EV_KEY, keyCode(static_cast<Key>(i)), 0);
        for (int b = 1; b <= 3; ++b) push(EV_KEY, mouseButtonToLinuxCode(b), 0);
        doReleaseAll();
    }

    void flush() override { doFlush(); }

private:
    void doReleaseAll() {
        if (shiftByUs_) {
            shiftByUs_ = false;
            push(EV_KEY, keyCode(Key::Shift), 0);
        }
        for (size_t i = 0; i < kKeyCount; ++i) {
            if (held_[i]) {
                held_[i] = false;
                push(EV_KEY, keyCode(static_cast<Key>(i)), 0);
            }
        }
        for (int b = 0; b < 3; ++b) {
            if (btnHeld_[b]) {
                btnHeld_[b] = false;
                push(EV_KEY, mouseButtonToLinuxCode(b + 1), 0);
            }
        }
    }

    // Writes buffered events. Anything unsent (EAGAIN etc.) stays in the buffer and is
    // retried on the next flush, so no key/button-up is lost.
    void doFlush() {
        if (fd_ < 0 || buf_.empty()) return;
        if (buf_.back().type != EV_SYN) push(EV_SYN, SYN_REPORT, 0);
        size_t sent = 0;  // number of fully written events
        while (sent < buf_.size()) {
            const char *p = reinterpret_cast<const char *>(buf_.data() + sent);
            size_t left = (buf_.size() - sent) * sizeof(input_event);
            ssize_t w = write(fd_, p, left);
            if (w < 0) {
                if (errno == EINTR) continue;
                break;  // EAGAIN etc.: do not block, the next flush retries
            }
            // uinput writes whole input_event structures
            sent += static_cast<size_t>(w) / sizeof(input_event);
            if (static_cast<size_t>(w) % sizeof(input_event) != 0) break;
        }
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(sent));
        trimBuffer();
    }

    // Bounds the backlog: only the oldest wheel events are dropped, never key/button
    // events (otherwise e.g. Ctrl would stay stuck) and never SYN.
    void trimBuffer() {
        if (buf_.size() <= kMaxBuffered) return;
        size_t excess = buf_.size() - kMaxBuffered;
        std::vector<input_event> keep;
        keep.reserve(buf_.size());
        for (const input_event &ev : buf_) {
            bool wheelEv = ev.type == EV_REL;
            if (wheelEv && excess > 0) {
                --excess;
                continue;
            }
            keep.push_back(ev);
        }
        buf_.swap(keep);
    }

private:
    static void setErr(std::string *err, const std::string &m) {
        if (err) *err = m;
    }

    bool fail() {
        if (fd_ >= 0) close(fd_);
        fd_ = -1;
        return false;
    }

    void push(int type, int code, int value) {
        input_event ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.type = static_cast<__u16>(type);
        ev.code = static_cast<__u16>(code);
        ev.value = value;
        buf_.push_back(ev);
    }

    // Per step one normal wheel event and (if possible) 120 hi-res units.
    void wheel(int code, int hiCode, int n) {
        push(EV_REL, code, n);
        if (hiRes_ && hiCode >= 0) push(EV_REL, hiCode, n * 120);
    }

    int fd_ = -1;
    bool created_ = false;
    bool hiRes_ = false;
    bool held_[kKeyCount] = {};
    bool shiftByUs_ = false;  // Shift pressed on behalf of "plus"
    bool btnHeld_[3] = {};
    std::vector<input_event> buf_;
};

}  // namespace

std::unique_ptr<Output> makeUinputOutput() { return std::make_unique<UinputOutput>(); }

}  // namespace spn
