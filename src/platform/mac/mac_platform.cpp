// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS implementations of the platform seams that are neither input nor output nor Objective-C:
// paths, the reload channel (user-only Unix domain socket) and the factories that pick the
// backends. See docs/port-macos.md and docs/port-architecture.md.
#include <fcntl.h>
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include "../fs.hpp"
#include "../platform.hpp"
#include "mac_activewin.hpp"
#include "mac_hid_input.hpp"
#include "mac_output.hpp"
#include "mac_paths.hpp"

namespace spn {

namespace {

std::string homeDirectory() {
    const char *home = std::getenv("HOME");
    if (home && *home) return home;
    struct passwd *pw = ::getpwuid(::getuid());
    if (pw && pw->pw_dir) return pw->pw_dir;
    return std::string();
}

// $TMPDIR on macOS is a long per-user path (/var/folders/...) that would not fit into sun_path,
// so the fallback uses the short fixed /tmp, protected by the 0700 owner-checked subdirectory.
std::string tmpDirectory() { return "/tmp"; }

// The directory must be a real directory (no symlink) of the current user that nobody else can
// write to. Anything else could let another user plant a socket we would then talk to.
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

ReloadSocket socketLocation() {
    const PlatformPaths p = platformPaths();
    return reloadSocketPath(p.runtimeDir, static_cast<unsigned long>(::geteuid()), tmpDirectory());
}

// Creates the socket directory (private) and verifies it.
bool prepareSocketDir(const ReloadSocket &loc, std::string *err) {
    const std::string dir = socketDir(loc.path);
    if (loc.fallback) {
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
    addr->sun_len = static_cast<unsigned char>(SUN_LEN(addr));
    return true;
}

int newSocket() {
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    const int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
    ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}

class SocketReloadListener : public ReloadListener {
public:
    ~SocketReloadListener() override {
        if (fd_ >= 0) {
            ::close(fd_);
            if (!path_.empty()) ::unlink(path_.c_str());
        }
    }

    bool init(std::string *err) override {
        std::string dummy;
        if (!err) err = &dummy;
        const ReloadSocket loc = socketLocation();
        if (loc.path.empty()) {
            *err = "no usable path for the reload socket";
            return false;
        }
        if (!prepareSocketDir(loc, err)) return false;
        struct sockaddr_un addr;
        if (!fillAddr(loc.path, &addr)) {
            *err = loc.path + ": socket path too long";
            return false;
        }

        // A leftover file is removed only if it is our own socket that nobody listens on.
        struct stat st;
        if (::lstat(loc.path.c_str(), &st) == 0) {
            if (!S_ISSOCK(st.st_mode) || st.st_uid != ::geteuid()) {
                *err = loc.path + ": exists and is not a socket of the current user";
                return false;
            }
            const int probe = newSocket();
            if (probe >= 0) {
                const bool alive = ::connect(probe, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) == 0;
                ::close(probe);
                if (alive) {
                    *err = "another dofscroll instance is already listening on " + loc.path;
                    return false;
                }
            }
            ::unlink(loc.path.c_str());
        }

        const int fd = newSocket();
        if (fd < 0) {
            *err = std::string("socket: ") + std::strerror(errno);
            return false;
        }
        const mode_t oldMask = ::umask(0177);  // the socket file is created 0600
        const int rc = ::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr);
        const int bindErr = errno;
        ::umask(oldMask);
        if (rc != 0) {
            ::close(fd);
            *err = loc.path + ": bind: " + std::strerror(bindErr);
            return false;
        }
        ::chmod(loc.path.c_str(), 0600);
        if (::listen(fd, 8) != 0 || ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL, 0) | O_NONBLOCK) != 0) {
            *err = loc.path + ": listen: " + std::strerror(errno);
            ::close(fd);
            ::unlink(loc.path.c_str());
            return false;
        }
        fd_ = fd;
        path_ = loc.path;
        return true;
    }

    // True once per burst of requests. A request is a completed connect(); the byte that the
    // writer sends is not needed. The peer's uid is checked as defense in depth (the socket is
    // 0600 in a 0700 directory, so only the same user can connect anyway).
    bool pollReload() override {
        if (fd_ < 0) return false;
        bool got = false;
        for (;;) {
            const int c = ::accept(fd_, nullptr, nullptr);
            if (c < 0) {
                if (errno == EINTR) continue;
                break;  // EAGAIN: nothing pending (or a real error: nothing to do about it here)
            }
            uid_t euid = 0;
            gid_t egid = 0;
            if (::getpeereid(c, &euid, &egid) == 0 && euid == ::geteuid()) got = true;
            ::close(c);
        }
        return got;
    }

private:
    int fd_ = -1;
    std::string path_;
};

}  // namespace

PlatformPaths platformPaths() {
    const MacPaths m = macPaths(homeDirectory());
    PlatformPaths p;
    p.configDir = m.configDir;
    p.runtimeDir = m.runtimeDir;
    return p;
}

bool requestReload(std::string *err) {
    std::string dummy;
    if (!err) err = &dummy;
    const ReloadSocket loc = socketLocation();
    struct sockaddr_un addr;
    if (!fillAddr(loc.path, &addr)) {
        *err = "no usable path for the reload socket";
        return false;
    }
    struct stat st;
    if (::lstat(loc.path.c_str(), &st) != 0) {
        *err = "the dofscroll daemon is not running (no socket " + loc.path + ")";
        return false;
    }
    if (!S_ISSOCK(st.st_mode) || st.st_uid != ::geteuid()) {
        *err = loc.path + ": not a socket of the current user - refusing to connect";
        return false;
    }
    const int fd = newSocket();
    if (fd < 0) {
        *err = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    if (::connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) != 0) {
        const int e = errno;
        ::close(fd);
        *err = (e == ECONNREFUSED || e == ENOENT)
                   ? "the dofscroll daemon is not running (stale socket " + loc.path + ")"
                   : loc.path + ": connect: " + std::strerror(e);
        return false;
    }
    const char byte = 'r';
    (void)::send(fd, &byte, 1, 0);  // the accepted connection is the request; a failed send is harmless
    ::close(fd);
    return true;
}

std::unique_ptr<ReloadListener> makeReloadListener() { return std::make_unique<SocketReloadListener>(); }

std::unique_ptr<ActiveWindowProvider> makeActiveWindowProvider() { return makeMacActiveWindowProvider(); }

std::unique_ptr<InputSource> makeInputSource(const std::string &backend, std::string *log) {
    std::string dummy;
    if (!log) log = &dummy;
    if (backend != "auto" && backend != "hid" && backend != "iokit") {
        *log = "input backend '" + backend + "' does not exist on macOS (use auto)";
        return nullptr;
    }
    const char *env = std::getenv("DOFSCROLL_DEVICES");
    std::string exeDir;
    const std::string exe = fs::executablePath();
    if (!exe.empty()) exeDir = exe.substr(0, exe.rfind('/') == std::string::npos ? 0 : exe.rfind('/'));
    const std::string dir = pickDescriptorDir(descriptorDirCandidates(env ? env : "", exeDir), [](const std::string &d) {
        fs::FileInfo i;
        return fs::statPath(d, &i) && i.isDir;
    });
    return makeMacHidInput(dir, log);
}

std::unique_ptr<Output> makeOutput(const std::string &backend, std::string *log) {
    std::string dummy;
    if (!log) log = &dummy;
    log->clear();
    std::string note;
    if (backend != "auto" && backend != "cgevent") {
        // The same configuration file is used on every OS: x11/uinput/sendinput are not errors.
        note = " ('" + backend + "' is not available on macOS)";
    }
    std::unique_ptr<Output> o = makeMacOutput(log);
    if (!o) {
        *log = "No output backend usable (cgevent not available)" + note;
        return nullptr;
    }
    std::string err;
    if (!o->init(&err)) {
        *log = "Backend cgevent not usable: " + (err.empty() ? std::string("init failed") : err) + note;
        return nullptr;
    }
    *log = "Backend cgevent selected" + note;
    return o;
}

}  // namespace spn
