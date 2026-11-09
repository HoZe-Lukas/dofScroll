// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Driver restart on macOS (platform/driver_restart_os.hpp): the processes of this user (libproc), SIGTERM,
// `open` for an app bundle. NOT run on a Mac: the process name (3DconnexionHelper) comes from the vendor's
// support pages, and whether launchd starts the helper again by itself after it ended is not known; the
// shared code starts it only when nothing did (docs/windows-mac.md).
#ifndef __APPLE__
#error "driver_restart_mac.cpp is built on macOS only"
#endif
#include <libproc.h>
#include <signal.h>
#include <spawn.h>
#include <sys/proc_info.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

#include "../driver_restart_os.hpp"

extern char **environ;

namespace spn::ui {

namespace {

class MacOps : public DriverRestartOps {
public:
    bool list(std::vector<DriverProcess> *out, std::string *err) override {
        out->clear();
        const int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
        if (bytes <= 0) {
            *err = "the process list cannot be read";
            return false;
        }
        std::vector<pid_t> pids(static_cast<size_t>(bytes) / sizeof(pid_t) + 64);
        const int got = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
        if (got <= 0) {
            *err = "the process list cannot be read";
            return false;
        }
        const size_t n = static_cast<size_t>(got) / sizeof(pid_t);
        for (size_t i = 0; i < n && i < pids.size(); ++i) {
            const pid_t pid = pids[i];
            if (pid <= 0) continue;
            struct proc_bsdinfo info;
            if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != static_cast<int>(sizeof(info))) continue;
            if (info.pbi_uid != getuid()) continue;  // only this user's processes
            char path[PROC_PIDPATHINFO_MAXSIZE];
            const int len = proc_pidpath(pid, path, sizeof(path));
            if (len > 0) out->push_back({static_cast<int>(pid), std::string(path, static_cast<size_t>(len))});
        }
        return true;
    }
    void askToClose(int) override {}  // the driver's daemon has no window to close
    bool end(int pid, std::string *err) override {
        if (::kill(static_cast<pid_t>(pid), SIGTERM) != 0) {
            if (errno == ESRCH) return true;  // gone by itself
            *err = errno == EPERM ? "not allowed" : "error " + std::to_string(errno);
            return false;
        }
        for (int i = 0; i < 30; ++i) {  // up to 3 seconds
            if (::kill(static_cast<pid_t>(pid), 0) != 0 && errno == ESRCH) return true;
            ::usleep(100 * 1000);
        }
        *err = "did not end in time";
        return false;
    }
    bool start(const std::string &exe, const std::vector<std::string> &args, std::string *err) override {
        // An app bundle is started the way Finder does it (`open`); a plain program is started directly.
        std::vector<std::string> argv;
        const size_t app = exe.find(".app/");
        if (app != std::string::npos) {
            argv = {"/usr/bin/open", "-g", exe.substr(0, app + 4)};
        } else {
            argv.push_back(exe);
            for (const std::string &a : args) argv.push_back(a);
        }
        std::vector<char *> cargv;
        for (std::string &s : argv) cargv.push_back(&s[0]);
        cargv.push_back(nullptr);
        pid_t child = 0;
        const int rc = ::posix_spawn(&child, cargv[0], nullptr, nullptr, cargv.data(), environ);
        if (rc != 0) {
            *err = "error " + std::to_string(rc);
            return false;
        }
        // `open` returns at once; reap it so that no zombie is left. A direct program keeps running: do not wait for it.
        if (app != std::string::npos) {
            int status = 0;
            ::waitpid(child, &status, 0);
        }
        return true;
    }
    void wait(int ms) override { ::usleep(static_cast<useconds_t>(ms) * 1000); }
};

}  // namespace

std::unique_ptr<DriverRestartOps> makeDriverRestartOps() { return std::unique_ptr<DriverRestartOps>(new MacOps); }

DriverRestartRules osDriverRestartRules() {
    const char *home = std::getenv("HOME");
    return driverRestartRules(DriverOs::MacOs, "", "", home ? home : "");
}

}  // namespace spn::ui
