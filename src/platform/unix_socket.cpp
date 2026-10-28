// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "unix_socket.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <ctime>

#include "fs.hpp"

namespace spn::usock {

static const int kConnectTimeoutMs = 500;

bool dirTrusted(const std::string &dir, std::string *err) {
    struct stat st;
    if (::lstat(dir.c_str(), &st) != 0) {
        *err = dir + ": " + std::strerror(errno);
        return false;
    }
    if (!S_ISDIR(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & 022) != 0) {
        *err = dir + ": not a directory of the current user or writable by others";
        return false;
    }
    return true;
}

bool prepareDir(const std::string &dir, bool fallback, std::string *err) {
    if (fallback) {
        std::string why;
        if (fs::mkdirPrivate(dir, &why) == fs::MkdirResult::Failed) {
            *err = dir + ": " + why;
            return false;
        }
    } else if (!fs::mkdirsPrivate(dir, err)) {
        return false;
    }
    return dirTrusted(dir, err);
}

bool fillAddr(const std::string &path, struct sockaddr_un *addr) {
    std::memset(addr, 0, sizeof *addr);
    if (path.empty() || path.size() >= sizeof addr->sun_path) return false;
    addr->sun_family = AF_UNIX;
    std::memcpy(addr->sun_path, path.c_str(), path.size() + 1);
#ifdef __APPLE__
    addr->sun_len = static_cast<unsigned char>(SUN_LEN(addr));
#endif
    return true;
}

int newSocket() {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
#ifdef SO_NOSIGPIPE
    const int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

bool setNonBlocking(int fd) {
    const int fl = ::fcntl(fd, F_GETFL, 0);
    return fl >= 0 && ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
}

int sendFlags() {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

// Connects a fresh socket without ever blocking longer than timeoutMs (a listener with a full
// backlog never lets a blocking connect() return). Leaves the socket non-blocking. 0 on success,
// else an errno value (ETIMEDOUT when the time ran out).
static int connectWithTimeout(int fd, const struct sockaddr_un &addr, int timeoutMs) {
    if (!setNonBlocking(fd)) return errno;
    struct timespec t0;
    ::clock_gettime(CLOCK_MONOTONIC, &t0);
    auto elapsedMs = [&t0]() {
        struct timespec t;
        ::clock_gettime(CLOCK_MONOTONIC, &t);
        return static_cast<long>((t.tv_sec - t0.tv_sec) * 1000L + (t.tv_nsec - t0.tv_nsec) / 1000000L);
    };
    for (;;) {
        if (::connect(fd, reinterpret_cast<const struct sockaddr *>(&addr), sizeof addr) == 0) return 0;
        const int e = errno;
        if (e == EINTR) continue;
        if (e == EINPROGRESS) {
            struct pollfd p = {fd, POLLOUT, 0};
            const long left = timeoutMs - elapsedMs();
            const int rc = ::poll(&p, 1, left > 0 ? static_cast<int>(left) : 0);
            if (rc < 0 && errno == EINTR) continue;
            if (rc <= 0) return ETIMEDOUT;
            int soerr = 0;
            socklen_t len = sizeof soerr;
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) != 0) return errno;
            return soerr;
        }
        if (e != EAGAIN) return e;  // Linux: backlog full; retry until the deadline
        if (elapsedMs() >= timeoutMs) return ETIMEDOUT;
        struct timespec nap = {0, 10 * 1000 * 1000};
        ::nanosleep(&nap, nullptr);
    }
}

int acceptCloexec(int listenFd) {
    for (;;) {
#ifdef __linux__
        const int c = ::accept4(listenFd, nullptr, nullptr, SOCK_CLOEXEC);
        if (c < 0 && errno == EINTR) continue;
        return c;
#else
        const int c = ::accept(listenFd, nullptr, nullptr);
        if (c < 0 && errno == EINTR) continue;
        if (c >= 0) ::fcntl(c, F_SETFD, FD_CLOEXEC);
        return c;
#endif
    }
}

int listenAt(const std::string &path, int backlog, std::string *err) {
    struct sockaddr_un addr;
    if (!fillAddr(path, &addr)) {
        *err = path + ": socket path too long";
        return -1;
    }
    struct stat st;
    if (::lstat(path.c_str(), &st) == 0) {
        if (!S_ISSOCK(st.st_mode) || st.st_uid != ::geteuid()) {
            *err = path + ": exists and is not a socket of the current user";
            return -1;
        }
        const int probe = newSocket();
        if (probe >= 0) {
            const int ce = connectWithTimeout(probe, addr, kConnectTimeoutMs);
            // A full backlog (EAGAIN/timeout) means somebody listens; only a refusal means stale.
            const bool alive = ce == 0 || ce == ETIMEDOUT || ce == EAGAIN;
            ::close(probe);
            if (alive) {
                *err = "another dofscroll instance is already listening on " + path;
                return -1;
            }
        }
        ::unlink(path.c_str());
    }
    const int fd = newSocket();
    if (fd < 0) {
        *err = std::string("socket: ") + std::strerror(errno);
        return -1;
    }
    const mode_t oldMask = ::umask(0177);  // the socket file is created 0600
    const int rc = ::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr);
    const int bindErr = errno;
    ::umask(oldMask);
    if (rc != 0) {
        ::close(fd);
        *err = path + ": bind: " + std::strerror(bindErr);
        return -1;
    }
    ::chmod(path.c_str(), 0600);
    if (::listen(fd, backlog) != 0 || !setNonBlocking(fd)) {
        *err = path + ": listen: " + std::strerror(errno);
        ::close(fd);
        ::unlink(path.c_str());
        return -1;
    }
    return fd;
}

int connectTo(const std::string &path, bool nonblocking, std::string *err) {
    struct sockaddr_un addr;
    if (!fillAddr(path, &addr)) {
        *err = path + ": socket path too long";
        return -1;
    }
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) {
        *err = path + ": " + std::strerror(errno);
        return -1;
    }
    if (!S_ISSOCK(st.st_mode) || st.st_uid != ::geteuid()) {
        *err = path + ": not a socket of the current user - refusing to connect";
        return -1;
    }
    const int fd = newSocket();
    if (fd < 0) {
        *err = std::string("socket: ") + std::strerror(errno);
        return -1;
    }
    const int ce = connectWithTimeout(fd, addr, kConnectTimeoutMs);
    if (ce != 0) {
        *err = path + ": " + std::strerror(ce);
        ::close(fd);
        return -1;
    }
    if (!nonblocking) {
        const int fl = ::fcntl(fd, F_GETFL, 0);
        if (fl < 0 || ::fcntl(fd, F_SETFL, fl & ~O_NONBLOCK) != 0) {
            ::close(fd);
            *err = "fcntl failed";
            return -1;
        }
    }
    return fd;
}

bool peerUid(int fd, uid_t *uid) {
#if defined(__linux__)
    struct ucred cr;
    socklen_t len = sizeof cr;
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cr, &len) != 0 || len != sizeof cr) return false;
    *uid = cr.uid;
    return true;
#else
    gid_t gid = 0;
    return ::getpeereid(fd, uid, &gid) == 0;
#endif
}

}  // namespace spn::usock
