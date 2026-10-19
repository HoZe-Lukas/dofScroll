// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// uinput-Backend: erzeugt ein virtuelles Eingabegeraet (Wayland-tauglich, ohne X11).
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

int keyCode(Key k) {
    switch (k) {
    case Key::Ctrl:  return KEY_LEFTCTRL;
    case Key::Up:    return KEY_UP;
    case Key::Down:  return KEY_DOWN;
    case Key::Left:  return KEY_LEFT;
    case Key::Right: return KEY_RIGHT;
    }
    return KEY_RESERVED;
}

int keyIndex(Key k) { return static_cast<int>(k); }

constexpr int kKeyCount = 5;

// Obergrenze fuer Raster pro Aufruf (Schutz vor Ueberlauf/Fluten durch extreme Config-Werte).
constexpr int kMaxSteps = 200;
// Obergrenze fuer ungesendete Ereignisse im Puffer (Rueckstau bei EAGAIN).
constexpr size_t kMaxBuffered = 4096;

int clampSteps(int n) { return n > kMaxSteps ? kMaxSteps : (n < -kMaxSteps ? -kMaxSteps : n); }

class UinputOutput : public Output {
public:
    ~UinputOutput() override {
        if (fd_ >= 0) {
            // Im Destruktor die eigene Implementierung ueber nicht-virtuelle Helfer nutzen
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
            setErr(err, std::string("/dev/uinput kann nicht geoeffnet werden: ") +
                            std::strerror(e) +
                            (e == ENOENT
                                 ? " (Kernelmodul 'uinput' laden: modprobe uinput)"
                                 : " (Rechte fehlen: udev-Regel packaging/"
                                   "70-dofscroll-uinput.rules installieren und "
                                   "neu anmelden (uaccess), siehe "
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
        bit(UI_SET_KEYBIT, BTN_LEFT);
        bit(UI_SET_KEYBIT, KEY_LEFTCTRL);
        bit(UI_SET_KEYBIT, KEY_UP);
        bit(UI_SET_KEYBIT, KEY_DOWN);
        bit(UI_SET_KEYBIT, KEY_LEFT);
        bit(UI_SET_KEYBIT, KEY_RIGHT);
        bit(UI_SET_RELBIT, REL_WHEEL);
        bit(UI_SET_RELBIT, REL_HWHEEL);
        // REL_X/REL_Y nur, damit Compositoren das Geraet als Maus erkennen.
        bit(UI_SET_RELBIT, REL_X);
        bit(UI_SET_RELBIT, REL_Y);
#if defined(REL_WHEEL_HI_RES) && defined(REL_HWHEEL_HI_RES)
        // Hi-Res optional: scheitert auf aelteren Kerneln, ist dann kein Fehler.
        if (ioctl(fd_, UI_SET_RELBIT, REL_WHEEL_HI_RES) == 0 &&
            ioctl(fd_, UI_SET_RELBIT, REL_HWHEEL_HI_RES) == 0) {
            hiRes_ = true;
        }
#endif
        if (!ok) {
            setErr(err, std::string("uinput-Faehigkeiten konnten nicht gesetzt werden: ") +
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
            setErr(err, std::string("uinput-Geraet konnte nicht angelegt werden: ") +
                            std::strerror(errno));
            return fail();
        }
        created_ = true;
        // Compositoren/libinput brauchen einen Moment, um das neue Geraet zu registrieren.
        usleep(300 * 1000);
        return true;
    }

    void wheelV(int n) override {
        n = clampSteps(n);
        if (n == 0) return;
        // Konvention: n > 0 = runter = REL_WHEEL negativ.
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
        // n > 0 = rechts = REL_HWHEEL positiv.
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
        push(EV_KEY, keyCode(k), down ? 1 : 0);
    }

    void tapKey(Key k) override {
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
        if (button != 1) return;
        if (btnLeft_ == down) return;
        btnLeft_ = down;
        push(EV_KEY, BTN_LEFT, down ? 1 : 0);
    }

    void releaseAll() override { doReleaseAll(); }

    void flush() override { doFlush(); }

private:
    void doReleaseAll() {
        for (int i = 0; i < kKeyCount; ++i) {
            if (held_[i]) {
                held_[i] = false;
                push(EV_KEY, keyCode(static_cast<Key>(i)), 0);
            }
        }
        if (btnLeft_) {
            btnLeft_ = false;
            push(EV_KEY, BTN_LEFT, 0);
        }
    }

    // Schreibt gepufferte Ereignisse. Nicht Gesendetes (EAGAIN o.ae.) bleibt im Puffer und
    // wird beim naechsten Flush erneut versucht, damit kein Key-/Button-Up verloren geht.
    void doFlush() {
        if (fd_ < 0 || buf_.empty()) return;
        if (buf_.back().type != EV_SYN) push(EV_SYN, SYN_REPORT, 0);
        size_t sent = 0;  // Anzahl vollstaendig geschriebener Ereignisse
        while (sent < buf_.size()) {
            const char *p = reinterpret_cast<const char *>(buf_.data() + sent);
            size_t left = (buf_.size() - sent) * sizeof(input_event);
            ssize_t w = write(fd_, p, left);
            if (w < 0) {
                if (errno == EINTR) continue;
                break;  // EAGAIN o.ae.: nicht blockieren, naechster Flush versucht es erneut
            }
            // uinput schreibt ganze input_event-Strukturen
            sent += static_cast<size_t>(w) / sizeof(input_event);
            if (static_cast<size_t>(w) % sizeof(input_event) != 0) break;
        }
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(sent));
        trimBuffer();
    }

    // Begrenzt den Rueckstau: verworfen werden nur aelteste Wheel-Ereignisse, nie
    // Tasten-/Button-Ereignisse (sonst bleibt z.B. Ctrl haengen) und nie SYN.
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

    // Pro Raster ein normales Wheel-Event und (falls moeglich) 120 Hi-Res-Einheiten.
    void wheel(int code, int hiCode, int n) {
        push(EV_REL, code, n);
        if (hiRes_ && hiCode >= 0) push(EV_REL, hiCode, n * 120);
    }

    int fd_ = -1;
    bool created_ = false;
    bool hiRes_ = false;
    bool held_[kKeyCount] = {};
    bool btnLeft_ = false;
    std::vector<input_event> buf_;
};

}  // namespace

std::unique_ptr<Output> makeUinputOutput() { return std::make_unique<UinputOutput>(); }

}  // namespace spn
