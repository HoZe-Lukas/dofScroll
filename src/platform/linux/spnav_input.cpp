// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// InputSource backed by spacenavd via libspnav. spacenavd merges all attached SpaceMice into
// one stream, so there is a single DeviceInfo with id 0 and the events carry device 0.
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <spnav.h>

#include "../platform.hpp"

namespace spn {

namespace {

class SpnavInputSource : public InputSource {
public:
    ~SpnavInputSource() override { closeImpl(); }

    const char *name() const override { return "spacenavd"; }

    bool open(std::string *err) override {
        if (open_) close();
        if (spnav_open() == -1) {
            if (err) *err = "cannot connect to spacenavd";
            return false;
        }
        open_ = true;
        return true;
    }

    WaitResult wait(InputEvent *out, int timeoutMs) override {
        if (!open_) return WaitResult::Closed;
        // Events already readable come first (also covers a zero timeout).
        if (poll1(out)) return WaitResult::Event;
        const int fd = spnav_fd();
        if (fd < 0) return WaitResult::Closed;
        struct pollfd p;
        p.fd = fd;
        p.events = POLLIN;
        p.revents = 0;
        int r = ::poll(&p, 1, timeoutMs);
        if (r < 0) return errno == EINTR ? WaitResult::Timeout : WaitResult::Closed;
        if (r == 0) return WaitResult::Timeout;
        if (poll1(out)) return WaitResult::Event;
        // Readable (or hung up) but no event: either a non-event protocol message or EOF.
        char c;
        ssize_t n = ::recv(fd, &c, 1, MSG_PEEK | MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
            return WaitResult::Closed;
        return WaitResult::Timeout;
    }

    std::vector<DeviceInfo> devices() const override {
        std::vector<DeviceInfo> v;
        if (!open_) return v;
        char nm[256] = "";
        if (spnav_dev_name(nm, sizeof(nm)) < 0) nm[0] = '\0';
        nm[sizeof(nm) - 1] = '\0';
        DeviceInfo d;
        d.id = 0;  // merged stream
        d.name = nm;
        d.reportedButtons = spnav_dev_buttons();
        d.axes = spnav_dev_axes();
        v.push_back(d);
        return v;
    }

    void close() override { closeImpl(); }

private:
    bool open_ = false;

    // Non-virtual so that the destructor does not make a virtual call.
    void closeImpl() {
        if (!open_) return;
        spnav_close();
        open_ = false;
    }

    // Reads one pending libspnav event and converts it. Unknown event types are consumed
    // and reported as "no event".
    static bool poll1(InputEvent *out) {
        spnav_event sev;
        if (spnav_poll_event(&sev) == 0) return false;
        InputEvent e;
        e.device = 0;
        if (sev.type == SPNAV_EVENT_MOTION) {
            e.type = InputEventType::Motion;
            e.axes = {sev.motion.x, sev.motion.y, sev.motion.z,
                      sev.motion.rx, sev.motion.ry, sev.motion.rz};
        } else if (sev.type == SPNAV_EVENT_BUTTON) {
            e.type = InputEventType::Button;
            e.button = sev.button.bnum;  // 0-based, same numbering as ButtonBinding::button
            e.down = sev.button.press != 0;
        } else if (sev.type == SPNAV_EVENT_DEV &&
                   (sev.dev.op == SPNAV_DEV_RM || sev.dev.op == SPNAV_DEV_ADD)) {
            e.type = InputEventType::DevicesChanged;
        } else {
            return false;
        }
        *out = e;
        return true;
    }
};

}  // namespace

std::unique_ptr<InputSource> makeInputSource(const std::string &backend, std::string *log) {
    if (backend != "auto" && backend != "spacenavd") {
        if (log) *log = "unknown input backend '" + backend + "'";
        return nullptr;
    }
    if (log) *log = "input via spacenavd";
    return std::make_unique<SpnavInputSource>();
}

}  // namespace spn
