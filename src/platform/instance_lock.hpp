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

    // `dir`: the validated runtime directory object, opened once and checked on its descriptor (Linux:
    // spn::RuntimeDir, platform/linux/runtime_files.hpp; macOS: MacRuntimeDir below). It needs ok() and
    // open(RuntimeFile, flags, mode); the path never reaches an open call here.
    template <class Dir>
    Result acquire(const Dir &dir) {
        release();
        if (!dir.ok()) return Result::Unavailable;
        const int fd = dir.open(RuntimeFile::LockFile, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
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

#ifdef __APPLE__
// macOS: the same checks as spn::RuntimeDir (which is a Linux file): O_NOFOLLOW directory, owner and mode
// checked on the descriptor, files only through openat under the fixed names of runtime_path.hpp.
class MacRuntimeDir {
public:
    explicit MacRuntimeDir(const std::string &dir) {
        if (!runtimeDirLexicallyValid(dir)) return;
        const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) return;
        struct stat ds;
        if (fstat(fd, &ds) != 0 || !S_ISDIR(ds.st_mode) || ds.st_uid != geteuid() || (ds.st_mode & 022) != 0) {
            ::close(fd);
            return;
        }
        fd_ = fd;
    }
    ~MacRuntimeDir() {
        if (fd_ >= 0) ::close(fd_);
    }
    MacRuntimeDir(const MacRuntimeDir &) = delete;
    MacRuntimeDir &operator=(const MacRuntimeDir &) = delete;
    bool ok() const { return fd_ >= 0; }
    int open(RuntimeFile f, int flags, mode_t mode) const { return fd_ < 0 ? -1 : ::openat(fd_, runtimeFileName(f), flags, mode); }

private:
    int fd_ = -1;
};
#endif

}  // namespace spn
