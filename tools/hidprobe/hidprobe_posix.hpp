// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// poll()+read() report stream over a file descriptor (hidraw on Linux; a pipe in tests).
// Never writes to the descriptor.
#pragma once
#include <poll.h>
#include <unistd.h>

#include <cerrno>

#include "hidprobe_backend.hpp"

namespace hidprobe {

class FdReportStream : public ReportStream {
public:
    // Takes ownership of fd.
    explicit FdReportStream(int fd) : fd_(fd) {}
    ~FdReportStream() override {
        if (fd_ >= 0) ::close(fd_);
    }
    FdReportStream(const FdReportStream &) = delete;
    FdReportStream &operator=(const FdReportStream &) = delete;

    ReadResult read(int timeoutMs, std::vector<uint8_t> &out) override {
        struct pollfd p;
        p.fd = fd_;
        p.events = POLLIN;
        p.revents = 0;
        int r = ::poll(&p, 1, timeoutMs);
        if (r < 0) return errno == EINTR ? ReadResult::Timeout : ReadResult::Error;
        if (r == 0) return ReadResult::Timeout;
        if (p.revents & POLLIN) {
            uint8_t buf[4096];
            ssize_t n = ::read(fd_, buf, sizeof buf);
            if (n > 0) {
                out.assign(buf, buf + n);
                return ReadResult::Report;
            }
            if (n == 0) return ReadResult::Closed;
            return (errno == EINTR || errno == EAGAIN) ? ReadResult::Timeout : ReadResult::Error;
        }
        return (p.revents & (POLLHUP | POLLERR | POLLNVAL)) ? ReadResult::Closed
                                                            : ReadResult::Timeout;
    }

private:
    int fd_;
};

// True if a line was waiting on stdin (used by the guided mode for "press Enter"); consumes it.
inline bool stdinLineReady() {
    struct pollfd p;
    p.fd = 0;
    p.events = POLLIN;
    p.revents = 0;
    if (::poll(&p, 1, 0) <= 0 || !(p.revents & POLLIN)) return false;
    char c;
    while (::read(0, &c, 1) == 1 && c != '\n') {
    }
    return true;
}

}  // namespace hidprobe
