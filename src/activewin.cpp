// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Detection of the active program (WM_CLASS) for [app:*] profiles.
// See activewin.hpp for the contract.
#include "activewin.hpp"
#include "platform/running_apps_util.hpp"
#include "xerrors.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace spn {

namespace {

using Clock = std::chrono::steady_clock;

// The error handler and thread_local flag g_xerror come from xerrors.hpp (installed
// once, idempotently; BadWindow for just-closed windows is normal).
constexpr size_t kMaxChildren = 32;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// Absolute path of an executable from the PATH entries; relative entries (empty,
// ".", "bin") are ignored so that no directory relative to the working directory
// is executed. Empty if not found.
std::string findInPath(const char *prog) {
    const char *path = std::getenv("PATH");
    if (!path) return std::string();
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty() || dir[0] != '/') continue;
        std::string full = dir + "/" + prog;
        if (access(full.c_str(), X_OK) == 0) return full;
    }
    return std::string();
}

// Starts prog (absolute path) without a shell, with fixed arguments, and reads the
// first output line with a time limit. On timeout the child is killed with SIGKILL
// and reaped. Empty string on any error.
std::string runCapture(const std::string &prog, const char *a1, const char *a2, int timeoutMs) {
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0) return std::string();
    pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        return std::string();
    }
    if (pid == 0) {
        // Child: stdout to the pipe, stdin/stderr to /dev/null, all other fds closed.
        int dn = open("/dev/null", O_RDWR);
        if (dn >= 0) {
            dup2(dn, 0);
            dup2(dn, 2);
        }
        dup2(fds[1], 1);
        for (int fd = 3; fd < 1024; ++fd) close(fd);
        execl(prog.c_str(), prog.c_str(), a1, a2, (char *)nullptr);
        _exit(127);
    }
    close(fds[1]);

    std::string out;
    auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    bool timedOut = false;
    while (out.size() < 256 && out.find('\n') == std::string::npos) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (left.count() <= 0) {
            timedOut = true;
            break;
        }
        pollfd pfd{fds[0], POLLIN, 0};
        int pr = poll(&pfd, 1, (int)left.count());
        if (pr < 0) {
            if (errno == EINTR) continue;
            timedOut = true;
            break;
        }
        if (pr == 0) {
            timedOut = true;
            break;
        }
        char buf[128];
        ssize_t r = read(fds[0], buf, sizeof buf);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;  // EOF
        out.append(buf, (size_t)r);
    }
    close(fds[0]);

    // Reap the child: on timeout (or truncated output) kill it hard.
    bool finished = false;
    int status = 0;
    if (!timedOut) {
        // After EOF the child ends almost immediately; wait briefly, bounded.
        for (int i = 0; i < 20 && !finished; ++i) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) finished = true;
            else if (w < 0 && errno != EINTR) { finished = true; status = -1; }
            else usleep(5 * 1000);
        }
    }
    if (!finished) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        // Line already read, child just not finished yet: the output is usable.
        return timedOut ? std::string() : out;
    }
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return std::string();
    return out;
}

}  // namespace

struct ActiveWindow::Impl {
    Display *dpy = nullptr;
    Window root = 0;
    Atom netActive = None;
    bool useKdotool = false;
    std::string kdotoolPath;  // absolute path, resolved in init()

    // Background thread: queries cyclically, poll() only reads the cache.
    std::thread worker;
    std::mutex mu;
    std::condition_variable cv;
    bool stop = false;
    std::string cached;

    std::string query() { return useKdotool ? queryKdotool() : queryX(); }

    void startWorker() {
        cached = query();  // initial fill so that the first poll() already returns something
        const auto interval = std::chrono::milliseconds(useKdotool ? 250 : 100);
        worker = std::thread([this, interval] {
            std::unique_lock<std::mutex> lk(mu);
            while (!stop) {
                lk.unlock();
                std::string r = query();
                lk.lock();
                cached = std::move(r);
                cv.wait_for(lk, interval, [this] { return stop; });
            }
        });
    }

    // --- X11 ---------------------------------------------------------------

    // WM_CLASS of a window as "instance class" (lowercase); empty if not present.
    std::string classOf(Window w) {
        XClassHint ch{nullptr, nullptr};
        g_xerror = 0;
        Status ok = XGetClassHint(dpy, w, &ch);
        XSync(dpy, False);
        std::string r;
        if (ok && !g_xerror) r = joinWmClass(ch.res_name, ch.res_class);
        if (ch.res_name) XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
        return g_xerror ? std::string() : r;
    }

    Window activeWindow() {
        Atom type = None;
        int fmt = 0;
        unsigned long n = 0, after = 0;
        unsigned char *data = nullptr;
        g_xerror = 0;
        int rc = XGetWindowProperty(dpy, root, netActive, 0, 1, False, XA_WINDOW,
                                    &type, &fmt, &n, &after, &data);
        XSync(dpy, False);
        Window w = 0;
        if (rc == Success && !g_xerror && data && type == XA_WINDOW && fmt == 32 && n >= 1)
            w = *(Window *)data;
        if (data) XFree(data);
        return g_xerror ? 0 : w;
    }

    std::string queryX() {
        Window w = activeWindow();
        if (w == 0 || w == root) return std::string();

        std::string r = classOf(w);
        if (!r.empty()) return r;

        // Fallback 1: child windows (e.g. reparenting WMs / toolkit subwindows)
        {
            Window rt = 0, par = 0, *kids = nullptr;
            unsigned int nk = 0;
            g_xerror = 0;
            Status ok = XQueryTree(dpy, w, &rt, &par, &kids, &nk);
            XSync(dpy, False);
            std::vector<Window> children;
            if (ok && !g_xerror && kids)
                children.assign(kids, kids + std::min<size_t>(nk, kMaxChildren));
            if (kids) XFree(kids);
            for (Window c : children) {
                r = classOf(c);
                if (!r.empty()) return r;
            }
            // Fallback 2: parent windows (up to the root)
            Window cur = (ok && !g_xerror) ? par : 0;
            for (int depth = 0; cur != 0 && cur != root && depth < 8; ++depth) {
                r = classOf(cur);
                if (!r.empty()) return r;
                Window rt2 = 0, par2 = 0, *k2 = nullptr;
                unsigned int n2 = 0;
                g_xerror = 0;
                Status ok2 = XQueryTree(dpy, cur, &rt2, &par2, &k2, &n2);
                XSync(dpy, False);
                if (k2) XFree(k2);
                if (!ok2 || g_xerror) break;
                cur = par2;
            }
        }
        return std::string();
    }

    // --- KDE/Wayland -------------------------------------------------------

    std::string queryKdotool() {
        // Fixed absolute path, fixed arguments, no shell; 200 ms time limit so that a
        // hanging kdotool (DBus timeouts) does not block the tick thread.
        if (kdotoolPath.empty()) return std::string();
        std::string line = runCapture(kdotoolPath, "getactivewindow", "getwindowclassname", 200);
        // cut to the first line, trim whitespace
        size_t nl = line.find_first_of("\r\n");
        if (nl != std::string::npos) line.erase(nl);
        size_t b = line.find_first_not_of(" \t");
        size_t e = line.find_last_not_of(" \t");
        if (b == std::string::npos) return std::string();
        return lower(line.substr(b, e - b + 1));
    }
};

ActiveWindow::ActiveWindow() : impl_(new Impl) {}

ActiveWindow::~ActiveWindow() {
    {
        std::lock_guard<std::mutex> lk(impl_->mu);
        impl_->stop = true;
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->dpy) XCloseDisplay(impl_->dpy);
    delete impl_;
}

bool ActiveWindow::init() {
    const char *st = std::getenv("XDG_SESSION_TYPE");
    bool wayland = st && std::strcmp(st, "wayland") == 0;
    std::string kpath = findInPath("kdotool");
    bool kdotool = !kpath.empty();
    impl_->kdotoolPath = kpath;

    // Pure Wayland with kdotool: its query is more reliable than XWayland.
    if (wayland && kdotool) {
        impl_->useKdotool = true;
        impl_->startWorker();
        return true;
    }

    Display *d = XOpenDisplay(nullptr);
    if (d) {
        installXErrorHandlers();
        impl_->dpy = d;
        impl_->root = DefaultRootWindow(d);
        impl_->netActive = XInternAtom(d, "_NET_ACTIVE_WINDOW", False);
        impl_->startWorker();
        return true;
    }
    if (kdotool) {
        impl_->useKdotool = true;
        impl_->startWorker();
        return true;
    }
    return false;
}

std::string ActiveWindow::poll() {
    Impl &m = *impl_;
    if (!m.dpy && !m.useKdotool) return std::string();
    std::lock_guard<std::mutex> lk(m.mu);
    return m.cached;
}

}  // namespace spn
