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
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "../fs.hpp"
#include "../platform.hpp"
#include "../unix_socket.hpp"
#include "mac_activewin.hpp"
#include "mac_hid_input.hpp"
#include "mac_output.hpp"
#include "mac_client.hpp"
#include "mac_driver_input.hpp"
#include "mac_paths.hpp"
#include "../input_select.hpp"

namespace spn {

namespace {

// Socket plumbing (trusted directory, stale-socket handling, bind 0600, peer uid) is shared with the
// live feed: src/platform/unix_socket.cpp.
using usock::fillAddr;
using usock::newSocket;

ReloadSocket socketLocation() { return macReloadSocketLocation(); }

bool prepareSocketDir(const ReloadSocket &loc, std::string *err) {
    return usock::prepareDir(socketDir(loc.path), loc.fallback, err);
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
        const int fd = usock::listenAt(loc.path, 8, err);
        if (fd < 0) return false;
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
            uid_t peer = 0;
            if (usock::peerUid(c, &peer) && reloadPeerAllowed(peer, ::geteuid())) got = true;
            ::close(c);
        }
        return got;
    }

private:
    int fd_ = -1;
    std::string path_;
};

}  // namespace

// platformPaths() and requestReload() (the client side) live in client_mac.cpp, which the UI links too.

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
    // Hybrid input (INPUT = auto | driver | hid, applied by the daemon through setInputMode): the
    // 3Dconnexion driver's client interface when the driver is installed, the raw HID path otherwise.
    // Which one comes first for "auto" is the table in input_select.hpp.
    {
        std::string n;
        std::unique_ptr<InputSource> probe = makeMacHidInput(dir, &n);  // only for the log line (descriptions found)
        *log = n;
    }
    SwitchingInput::Parts parts;
    parts.driverInstalled = [] {
        static std::mutex mu;
        static std::string last;
        std::string why;
        const bool ok = macDriverInstalled(&why);
        std::lock_guard<std::mutex> lk(mu);
        if (!ok && why != last) std::cerr << "[dofscroll] 3Dconnexion driver interface not usable: " << why << std::endl;
        last = ok ? std::string() : why;
        return ok;
    };
    parts.makeDriver = [dir] {
        std::string l;
        return makeMacDriverInput(dir, &l);
    };
    parts.makeHid = [dir] {
        std::string l;
        return makeMacHidInput(dir, &l);
    };
    return std::make_unique<SwitchingInput>(std::move(parts), InputOs::Mac);
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
    // keep what makeMacOutput() reported (e.g. the missing Accessibility permission)
    *log = (log->empty() ? std::string() : *log + "; ") + "Backend cgevent selected" + note;
    return o;
}

}  // namespace spn
