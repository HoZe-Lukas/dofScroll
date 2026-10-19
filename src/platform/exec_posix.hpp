// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Runs a program from an argument vector (fork + execvp, never a shell) and captures stdout and
// stderr. POSIX only (Linux and macOS autostart). Output is capped; a hung child is killed after
// the timeout.
#pragma once

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace spn {

struct ExecResult {
    bool started = false;  // fork/exec worked (false: *error is set)
    int status = -1;       // exit status, -1 if killed or not started
    std::string out, err, error;
};

inline ExecResult execCapture(const std::vector<std::string> &argv, int timeoutMs = 15000) {
    ExecResult r;
    if (argv.empty()) { r.error = "empty command"; return r; }
    int po[2], pe[2], px[2];
    if (pipe(po) != 0) { r.error = "pipe failed"; return r; }
    if (pipe(pe) != 0) { close(po[0]); close(po[1]); r.error = "pipe failed"; return r; }
    if (pipe(px) != 0) { for (int fd : {po[0], po[1], pe[0], pe[1]}) close(fd); r.error = "pipe failed"; return r; }
    fcntl(px[1], F_SETFD, FD_CLOEXEC);  // closes on a successful exec: EOF means "exec worked"
    std::vector<char *> a;
    for (const auto &s : argv) a.push_back(const_cast<char *>(s.c_str()));
    a.push_back(nullptr);
    const pid_t pid = fork();
    if (pid < 0) {
        for (int fd : {po[0], po[1], pe[0], pe[1], px[0], px[1]}) close(fd);
        r.error = "fork failed";
        return r;
    }
    if (pid == 0) {
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, 0); close(devnull); }
        dup2(po[1], 1);
        dup2(pe[1], 2);
        for (int fd : {po[0], po[1], pe[0], pe[1], px[0]}) close(fd);
        execvp(a[0], a.data());
        const int e = errno;
        if (write(px[1], &e, sizeof e) < 0) {}
        _exit(127);
    }
    close(po[1]); close(pe[1]); close(px[1]);
    int execErrno = 0;
    const bool execFailed = read(px[0], &execErrno, sizeof execErrno) == static_cast<ssize_t>(sizeof execErrno);
    close(px[0]);
    const auto start = std::chrono::steady_clock::now();
    int fds[2] = {po[0], pe[0]};
    bool open_[2] = {true, true};
    constexpr size_t kCap = 64 * 1024;
    bool timedOut = false;
    while (open_[0] || open_[1]) {
        const int left = timeoutMs - static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now() - start).count());
        if (left <= 0) { timedOut = true; break; }
        struct pollfd p[2];
        int n = 0, idx[2];
        for (int i = 0; i < 2; i++)
            if (open_[i]) { p[n].fd = fds[i]; p[n].events = POLLIN; p[n].revents = 0; idx[n++] = i; }
        const int pr = poll(p, n, left);
        if (pr < 0) { if (errno == EINTR) continue; break; }
        for (int k = 0; k < n; k++) {
            if (!(p[k].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            char buf[4096];
            const ssize_t got = read(p[k].fd, buf, sizeof buf);
            if (got <= 0) { open_[idx[k]] = false; continue; }
            std::string &dst = idx[k] == 0 ? r.out : r.err;
            if (dst.size() < kCap) dst.append(buf, static_cast<size_t>(got));
        }
    }
    close(po[0]); close(pe[0]);
    if (timedOut) kill(pid, SIGKILL);
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    if (execFailed) { r.error = std::string(argv[0]) + ": " + std::string(strerror(execErrno)); return r; }
    r.started = true;
    if (timedOut) { r.error = std::string(argv[0]) + ": timed out"; return r; }
    r.status = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    return r;
}

}  // namespace spn
