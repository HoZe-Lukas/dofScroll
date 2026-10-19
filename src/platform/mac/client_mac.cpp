// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Client side of the macOS platform layer, linked by the daemon AND the UI (no HID/CGEvent, no
// Objective-C): per-user paths and the reload request (connecting to the daemon's socket).
#include <pwd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../platform.hpp"
#include "../unix_socket.hpp"
#include "mac_client.hpp"

namespace spn {

namespace {

std::string homeDirectory() {
    const char *home = std::getenv("HOME");
    if (home && *home) return home;
    struct passwd *pw = ::getpwuid(::getuid());
    if (pw && pw->pw_dir) return pw->pw_dir;
    return std::string();
}

using usock::fillAddr;
using usock::newSocket;

}  // namespace

ReloadSocket macReloadSocketLocation() {
    const PlatformPaths p = platformPaths();
    // $TMPDIR is a long per-user path that would not fit into sun_path: the fallback is the
    // short fixed /tmp, protected by the 0700 owner-checked subdirectory.
    return reloadSocketPath(p.runtimeDir, static_cast<unsigned long>(::geteuid()), "/tmp");
}

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
    const ReloadSocket loc = macReloadSocketLocation();
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
    if (!reloadSocketTrusted(S_ISSOCK(st.st_mode), st.st_uid, ::geteuid())) {
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

}  // namespace spn
