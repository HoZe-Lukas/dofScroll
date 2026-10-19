// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The two functions of src/platform/platform.hpp that the UI needs (platformPaths and
// requestReload), for the UI executable. The daemon's own implementations live in
// src/platform/<os>/*_platform.cpp together with the input/output backends, which the UI must not
// link; this file repeats only their small client side and uses the same pure helpers
// (modes.hpp, win_paths.hpp, mac_paths.hpp). Keep it in sync with those files.
#include <QByteArray>
#include <QProcess>
#include <QString>
#include <QtGlobal>
#include <map>
#include <string>

#include "platform/platform.hpp"

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "platform/win/win_paths.hpp"

namespace spn {

PlatformPaths platformPaths() {
    std::map<std::string, std::string> env;
    for (const char *k : {"APPDATA", "LOCALAPPDATA", "USERPROFILE"})
        env[k] = qEnvironmentVariable(k).toStdString();
    const WinPaths wp = windowsPaths(env);
    PlatformPaths p;
    p.configDir = wp.configDir;
    p.runtimeDir = wp.runtimeDir;
    return p;
}

bool requestReload(std::string *err) {
    const std::wstring name = QString::fromUtf8(reloadEventName()).toStdWString();
    HANDLE ev = ::OpenEventW(EVENT_MODIFY_STATE, FALSE, name.c_str());
    if (!ev) {
        if (err) *err = std::string("dofscroll is not running (event ") + reloadEventName() + ")";
        return false;
    }
    const BOOL ok = ::SetEvent(ev);
    ::CloseHandle(ev);
    if (!ok && err) *err = "SetEvent failed";
    return ok != 0;
}

}  // namespace spn

#elif defined(Q_OS_MACOS)
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "platform/mac/mac_paths.hpp"

namespace spn {

namespace {
std::string homeDirectory() {
    const char *home = std::getenv("HOME");
    return home && *home ? home : "";
}
}  // namespace

PlatformPaths platformPaths() {
    const MacPaths m = macPaths(homeDirectory());
    PlatformPaths p;
    p.configDir = m.configDir;
    p.runtimeDir = m.runtimeDir;
    return p;
}

// Client side of the daemon's reload socket: connecting is the request.
bool requestReload(std::string *err) {
    std::string dummy;
    if (!err) err = &dummy;
    const PlatformPaths pp = platformPaths();
    const ReloadSocket loc = reloadSocketPath(pp.runtimeDir, static_cast<unsigned long>(::geteuid()), "/tmp");
    struct sockaddr_un addr;
    std::memset(&addr, 0, sizeof addr);
    if (loc.path.empty() || loc.path.size() >= sizeof addr.sun_path) {
        *err = "no usable path for the reload socket";
        return false;
    }
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, loc.path.c_str(), loc.path.size());
    struct stat st;
    if (::lstat(loc.path.c_str(), &st) != 0) {
        *err = "the dofscroll daemon is not running (no socket " + loc.path + ")";
        return false;
    }
    if (!reloadSocketTrusted(S_ISSOCK(st.st_mode), st.st_uid, ::geteuid())) {
        *err = loc.path + ": not a socket of the current user - refusing to connect";
        return false;
    }
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        *err = std::string("socket: ") + std::strerror(errno);
        return false;
    }
    const bool ok = ::connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof addr) == 0;
    if (!ok) *err = "the dofscroll daemon is not running (cannot connect to " + loc.path + ")";
    else {
        const char byte = 'r';
        (void)::send(fd, &byte, 1, 0);
    }
    ::close(fd);
    return ok;
}

}  // namespace spn

#else  // Linux and other Unix systems with systemd user services
#include "modes.hpp"  // modesDir(): "<config base>/dofscroll/modes"

namespace spn {

PlatformPaths platformPaths() {
    PlatformPaths p;
    const std::string m = modesDir();  // its parent is the config directory (XDG rules in one place)
    const size_t slash = m.rfind('/');
    p.configDir = slash == std::string::npos ? "." : m.substr(0, slash);
    const QByteArray rt = qgetenv("XDG_RUNTIME_DIR");  // only the private per-user directory
    p.runtimeDir = rt.isEmpty() ? std::string() : rt.toStdString();
    return p;
}

// Same as the daemon's platform layer: the user's systemd instance sends SIGHUP to the unit.
bool requestReload(std::string *err) {
    const int rc = QProcess::execute(QStringLiteral("systemctl"),
                                     {QStringLiteral("--user"), QStringLiteral("kill"), QStringLiteral("-s"),
                                      QStringLiteral("HUP"), QStringLiteral("dofscroll.service")});
    if (rc != 0) {
        if (err) *err = "systemctl --user kill -s HUP dofscroll.service failed";
        return false;
    }
    return true;
}

}  // namespace spn
#endif
