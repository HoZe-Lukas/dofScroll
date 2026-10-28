// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Live feed transport for Linux and macOS: Unix domain socket "live.sock" in the runtime
// directory (0600, in a private owner-verified directory, peer uid checked on accept).
// The socket plumbing is shared with the macOS reload channel (platform/unix_socket.cpp).
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>

#include "live_feed.hpp"
#include "platform/mac/mac_paths.hpp"  // socketPathFor/socketDir: pure path logic, portable
#include "platform/unix_socket.hpp"

namespace spn {

namespace {

ReloadSocket livePath(const std::string &runtimeDir) {
    return socketPathFor("live.sock", runtimeDir, static_cast<unsigned long>(::geteuid()), "/tmp");
}

class PosixConn : public FeedConn {
public:
    explicit PosixConn(int fd) : fd_(fd) {}
    ~PosixConn() override { ::close(fd_); }
    long read(uint8_t *buf, size_t n) override {
        for (;;) {
            const ssize_t r = ::recv(fd_, buf, n, 0);
            if (r > 0) return static_cast<long>(r);
            if (r == 0) return -1;  // orderly shutdown
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
    }
    long write(const uint8_t *buf, size_t n) override {
        for (;;) {
            const ssize_t w = ::send(fd_, buf, n, usock::sendFlags());
            if (w >= 0) return static_cast<long>(w);
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
            return -1;
        }
    }

private:
    int fd_;
};

class PosixListener : public FeedListener {
public:
    explicit PosixListener(std::string runtimeDir) : dir_(std::move(runtimeDir)) {}
    ~PosixListener() override {
        if (fd_ >= 0) {
            ::close(fd_);
            ::unlink(path_.c_str());
        }
    }
    bool open(std::string *err) override {
        std::string dummy;
        if (!err) err = &dummy;
        const ReloadSocket loc = livePath(dir_);
        if (loc.path.empty()) {
            *err = "no usable path for the live feed socket";
            return false;
        }
        if (!usock::prepareDir(socketDir(loc.path), loc.fallback, err)) return false;
        fd_ = usock::listenAt(loc.path, 8, err);
        if (fd_ < 0) return false;
        path_ = loc.path;
        return true;
    }
    std::unique_ptr<FeedConn> accept() override {
        if (fd_ < 0) return nullptr;
        for (;;) {
            const int c = usock::acceptCloexec(fd_);
            if (c < 0) {
                return nullptr;  // EAGAIN or a real error: nothing to do here
            }
            uid_t peer = 0;
            if (!usock::peerUid(c, &peer) || peer != ::geteuid() || !usock::setNonBlocking(c)) {
                ::close(c);  // defense in depth: the socket is 0600 in a 0700 directory anyway
                continue;
            }
#ifdef SO_NOSIGPIPE
            const int one = 1;
            ::setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            return std::make_unique<PosixConn>(c);
        }
    }

private:
    std::string dir_, path_;
    int fd_ = -1;
};

}  // namespace

std::unique_ptr<FeedListener> makeFeedListener(const std::string &runtimeDir) {
    return std::make_unique<PosixListener>(runtimeDir);
}

std::unique_ptr<FeedConn> connectFeed(const std::string &runtimeDir, std::string *err) {
    std::string dummy;
    if (!err) err = &dummy;
    const ReloadSocket loc = livePath(runtimeDir);
    if (loc.path.empty()) {
        *err = "no usable path for the live feed socket";
        return nullptr;
    }
    // The directory must be ours before we connect (a planted socket elsewhere is not trusted).
    if (!usock::dirTrusted(socketDir(loc.path), err)) return nullptr;
    const int fd = usock::connectTo(loc.path, true, err);
    if (fd < 0) return nullptr;
    uid_t peer = 0;
    if (!usock::peerUid(fd, &peer) || peer != ::geteuid()) {  // the server must be us as well
        ::close(fd);
        *err = loc.path + ": served by another user - refusing";
        return nullptr;
    }
    return std::make_unique<PosixConn>(fd);
}

}  // namespace spn
