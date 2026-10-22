// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Single-instance guard of the daemon on Linux and macOS (Windows uses a named mutex, see
// win/win_main.cpp): an exclusive, non-blocking flock(2) on the file "dofscroll.lock" in the
// daemon's private runtime directory. The kernel drops the lock when the process ends, however it
// ends (no stale lock file, no pid file to trust). The directory is opened with O_NOFOLLOW and
// checked on the descriptor (owner = effective user, not group/other-writable), the file is opened
// with openat(O_NOFOLLOW) under its fixed name from runtime_path.hpp, as every runtime file
// (docs/security.md, section 8). POSIX only.
#pragma once
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <string>

#include "runtime_path.hpp"

namespace spn {

// Exit code of a daemon that finds another one running (EX_TEMPFAIL). The systemd unit lists it in
// RestartPreventExitStatus, so the service manager does not start it again in a loop.
constexpr int kExitDaemonAlreadyRunning = 75;

class InstanceLock {
public:
    enum class Result {
        Acquired,        // this process holds the lock until it ends
        AlreadyRunning,  // another process holds it
        Unavailable      // no usable runtime directory or file: the guard cannot work (carry on without it)
    };

    InstanceLock() = default;
    ~InstanceLock() { release(); }
    InstanceLock(const InstanceLock &) = delete;
    InstanceLock &operator=(const InstanceLock &) = delete;

    Result acquire(const std::string &runtimeDir) {
        release();
        if (!runtimeDirLexicallyValid(runtimeDir)) return Result::Unavailable;
        const int dfd = ::open(runtimeDir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (dfd < 0) return Result::Unavailable;
        struct stat ds;
        if (fstat(dfd, &ds) != 0 || !S_ISDIR(ds.st_mode) || ds.st_uid != geteuid() || (ds.st_mode & 022) != 0) {
            ::close(dfd);
            return Result::Unavailable;
        }
        const int fd = ::openat(dfd, runtimeFileName(RuntimeFile::LockFile),
                                O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        ::close(dfd);
        if (fd < 0) return Result::Unavailable;
        struct stat fs;
        if (fstat(fd, &fs) != 0 || !S_ISREG(fs.st_mode) || fs.st_uid != geteuid()) {
            ::close(fd);
            return Result::Unavailable;
        }
        if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
            const bool held = errno == EWOULDBLOCK;
            ::close(fd);
            return held ? Result::AlreadyRunning : Result::Unavailable;
        }
        fd_ = fd;  // O_CLOEXEC: helper programs the daemon runs never inherit the lock
        return Result::Acquired;
    }

    void release() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    int fd_ = -1;
};

}  // namespace spn
