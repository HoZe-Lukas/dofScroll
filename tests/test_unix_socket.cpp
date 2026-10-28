// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// usock::connectTo never blocks on a full listener backlog; accepted fds are close-on-exec.
#undef NDEBUG
#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/unix_socket.hpp"
#include "testutil.hpp"

using namespace spn;

int main() {
    alarm(60);  // a blocking connect would hang here: fail instead
    const std::string d = tu::makeTempDir("spn_usock");
    std::string err;
    for (int backlog : {1, 4, 8}) {
        const std::string path = d + "/s" + std::to_string(backlog) + ".sock";
        const int lfd = usock::listenAt(path, backlog, &err);
        assert(lfd >= 0);
        std::vector<int> fds;
        bool failed = false;
        for (int i = 0; i < 64 && !failed; i++) {
            const auto t0 = std::chrono::steady_clock::now();
            std::string e;
            const int c = usock::connectTo(path, true, &e);
            const auto ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            assert(ms < 3000);
            if (c < 0) failed = true; else fds.push_back(c);
        }
        assert(failed);  // the listener never accepts: the backlog fills and connectTo reports an error
#ifdef __linux__
        // a second listener on the same path refuses to take over a live (full) socket. Linux only:
        // on macOS/BSD a full accept queue answers ECONNREFUSED, the same as a socket file without a
        // listener, so the probe cannot tell the two apart (documented in docs/security.md).
        assert(usock::listenAt(path, backlog, &err) < 0 && err.find("already listening") != std::string::npos);
#endif
        for (int c : fds) ::close(c);
        ::close(lfd);
    }

    // accepted fds are close-on-exec; a normal connect works, blocking and non-blocking
    const std::string path = d + "/ok.sock";
    const int lfd = usock::listenAt(path, 4, &err);
    assert(lfd >= 0);
    const int c1 = usock::connectTo(path, false, &err);
    assert(c1 >= 0 && !(::fcntl(c1, F_GETFL) & O_NONBLOCK));
    const int c2 = usock::connectTo(path, true, &err);
    assert(c2 >= 0 && (::fcntl(c2, F_GETFL) & O_NONBLOCK));
    // an accepting listener is detected as alive on every OS
    assert(usock::listenAt(path, 4, &err) < 0 && err.find("already listening") != std::string::npos);
    const int a = usock::acceptCloexec(lfd);
    assert(a >= 0 && (::fcntl(a, F_GETFD) & FD_CLOEXEC));
    ::close(a);
    ::close(c1);
    ::close(c2);
    ::close(lfd);
    tu::removeAll(d);
    std::puts("test_unix_socket: ok");
    return 0;
}
