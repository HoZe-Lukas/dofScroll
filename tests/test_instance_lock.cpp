// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Single-instance guard of the daemon (src/platform/instance_lock.hpp): a second holder is refused,
// also from another process, and the lock is free again when the holder ends or lets go.
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cassert>
#include <cstdio>
#include <string>

#include "platform/instance_lock.hpp"
#include "platform/linux/runtime_files.hpp"
#include "testutil.hpp"

using namespace spn;
using R = InstanceLock::Result;

int main() {
    const std::string dir = tu::makeTempDir("dofscroll-lock");
    ::chmod(dir.c_str(), 0700);

    {
        InstanceLock a, b;
        assert(a.acquire(RuntimeDir(dir)) == R::Acquired);
        assert(tu::exists(dir + "/dofscroll.lock"));
        struct stat st;
        assert(::stat((dir + "/dofscroll.lock").c_str(), &st) == 0 && (st.st_mode & 077) == 0);
        assert(b.acquire(RuntimeDir(dir)) == R::AlreadyRunning);  // a second holder in the same process
        a.release();
        assert(b.acquire(RuntimeDir(dir)) == R::Acquired);        // free again after the release
    }

    // another process: refused while the parent holds it, accepted once the parent let go; a holder
    // that dies without cleaning up frees the lock (the kernel drops it)
    {
        InstanceLock parent;
        assert(parent.acquire(RuntimeDir(dir)) == R::Acquired);
        const pid_t pid = fork();
        assert(pid >= 0);
        if (pid == 0) {
            InstanceLock child;
            _exit(child.acquire(RuntimeDir(dir)) == R::AlreadyRunning ? 0 : 1);
        }
        int status = 0;
        assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
        parent.release();

        int fds[2];
        assert(::pipe(fds) == 0);
        const pid_t holder = fork();
        assert(holder >= 0);
        if (holder == 0) {
            InstanceLock child;
            const char c = child.acquire(RuntimeDir(dir)) == R::Acquired ? 'y' : 'n';
            (void)!::write(fds[1], &c, 1);
            ::pause();  // killed below, never releases by itself
            _exit(2);
        }
        char c = 0;
        assert(::read(fds[0], &c, 1) == 1 && c == 'y');
        InstanceLock second;
        assert(second.acquire(RuntimeDir(dir)) == R::AlreadyRunning);
        ::kill(holder, SIGKILL);
        assert(waitpid(holder, &status, 0) == holder);
        assert(second.acquire(RuntimeDir(dir)) == R::Acquired);
        ::close(fds[0]);
        ::close(fds[1]);
    }

    // no usable directory: unavailable, never a false "already running"
    {
        InstanceLock l;
        assert(l.acquire(RuntimeDir("")) == R::Unavailable);
        assert(l.acquire(RuntimeDir("relative")) == R::Unavailable);
        assert(l.acquire(RuntimeDir(dir + "/missing")) == R::Unavailable);
        assert(::chmod(dir.c_str(), 0770) == 0);
        assert(l.acquire(RuntimeDir(dir)) == R::Unavailable);  // group-writable directory is not trusted
        ::chmod(dir.c_str(), 0700);
        // a link at the file name is refused (O_NOFOLLOW)
        ::unlink((dir + "/dofscroll.lock").c_str());
        assert(::symlink((dir + "/target").c_str(), (dir + "/dofscroll.lock").c_str()) == 0);
        assert(l.acquire(RuntimeDir(dir)) == R::Unavailable);
        assert(!tu::exists(dir + "/target"));
    }
    std::puts("ok instance lock");
    return 0;
}
