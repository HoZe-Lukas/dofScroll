// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "signal_flush.hpp"

#include <QSocketNotifier>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <cerrno>
#endif

#ifdef Q_OS_UNIX
namespace {
constexpr int kSignals[] = {SIGTERM, SIGINT, SIGHUP};
int g_pipe[2] = {-1, -1};
struct sigaction g_old[sizeof kSignals / sizeof *kSignals];
bool g_installed = false;

// Async-signal-safe: one byte (the signal number) into the pipe, errno kept.
void handler(int signo) {
    const int saved = errno;
    const unsigned char b = static_cast<unsigned char>(signo);
    ssize_t n;
    do {
        n = ::write(g_pipe[1], &b, 1);
    } while (n < 0 && errno == EINTR);
    errno = saved;
}

bool setCloexecNonblock(int fd) {
    const int fl = ::fcntl(fd, F_GETFL);
    const int fdfl = ::fcntl(fd, F_GETFD);
    return fl >= 0 && fdfl >= 0 && ::fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0 && ::fcntl(fd, F_SETFD, fdfl | FD_CLOEXEC) == 0;
}
}  // namespace
#endif

SignalFlush::SignalFlush(std::function<void(int)> onSignal, QObject *parent) : QObject(parent), onSignal_(std::move(onSignal)) {
#ifdef Q_OS_UNIX
    if (g_installed) return;  // one instance only
    if (::pipe(g_pipe) != 0) return;
    if (!setCloexecNonblock(g_pipe[0]) || !setCloexecNonblock(g_pipe[1])) {
        ::close(g_pipe[0]);
        ::close(g_pipe[1]);
        g_pipe[0] = g_pipe[1] = -1;
        return;
    }
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    for (size_t i = 0; i < sizeof kSignals / sizeof *kSignals; ++i) ::sigaction(kSignals[i], &sa, &g_old[i]);
    g_installed = true;
    notifier_ = new QSocketNotifier(g_pipe[0], QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] { drain(); });
#endif
}

SignalFlush::~SignalFlush() {
#ifdef Q_OS_UNIX
    if (!notifier_) return;
    for (size_t i = 0; i < sizeof kSignals / sizeof *kSignals; ++i) ::sigaction(kSignals[i], &g_old[i], nullptr);
    delete notifier_;
    notifier_ = nullptr;
    ::close(g_pipe[0]);
    ::close(g_pipe[1]);
    g_pipe[0] = g_pipe[1] = -1;
    g_installed = false;
#endif
}

void SignalFlush::drain() {
#ifdef Q_OS_UNIX
    if (!notifier_) return;
    notifier_->setEnabled(false);
    unsigned char buf[16];
    int first = 0;
    ssize_t n;
    while ((n = ::read(g_pipe[0], buf, sizeof buf)) > 0)
        if (!first) first = buf[0];
    notifier_->setEnabled(true);
    if (first && onSignal_) onSignal_(first);  // a burst of signals is one flush and one quit
#endif
}
