// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Linux runtime files in the private XDG_RUNTIME_DIR: suspend heartbeat probe, status file
// writer and the log mirror (see common.hpp for the formats). Used by the Linux daemon main
// and handed to the platform-independent core as callbacks.
#pragma once
#include <cstdint>
#include <memory>
#include <streambuf>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>

#include "../runtime_path.hpp"

namespace spn {

// Is `dir` trustworthy? (owned by the effective user, not group/other-writable) -
// prerequisite for the status and log files. Empty dir: false.
bool runtimeDirTrusted(const std::string &dir);

// The runtime directory opened once (O_DIRECTORY|O_NOFOLLOW) and checked on the descriptor (owner =
// effective user, not group/other-writable). Every file access goes through it with openat/unlinkat/
// renameat/fstatat and one of the three fixed names of RuntimeFile (string literals chosen by the
// enum, never derived from the environment); `tmp` selects "<name>.tmp.<pid>" for atomic replacement.
class RuntimeDir {
public:
    explicit RuntimeDir(const std::string &dir);
    ~RuntimeDir();
    RuntimeDir(const RuntimeDir &) = delete;
    RuntimeDir &operator=(const RuntimeDir &) = delete;
    bool ok() const { return fd_ >= 0; }
    int open(RuntimeFile f, int flags, mode_t mode, bool tmp = false) const;
    bool unlink(RuntimeFile f, bool tmp = false) const;
    bool rename(RuntimeFile from_tmp_of, RuntimeFile to) const;  // "<from>.tmp.<pid>" -> "<to>"
    bool lstat(RuntimeFile f, struct stat *st) const;
private:
    int fd_ = -1;
};

// Is the tray pausing the output right now (fresh heartbeat file in `dir`)?
class SuspendProbe {
public:
    explicit SuspendProbe(const std::string &dir);
    bool operator()() const;
private:
    std::shared_ptr<RuntimeDir> dir_;
};

// Writes the status file atomically (temp file + rename), errors are never fatal.
class StatusWriter {
public:
    explicit StatusWriter(const std::string &dir);
    // body = lines without "updated"; written on change or after one second.
    void update(const std::string &body, int64_t now_ns);
    void remove();
private:
    std::shared_ptr<RuntimeDir> dir_;
    std::string last_body_;
    int64_t next_refresh_ns_ = 0;
    bool warned_ = false, wrote_ = false;
    bool write(const std::string &text);
};

// Stream buffer that mirrors everything written into the rolling log file in `dir`.
// Deliberately never destroyed by the caller (a detached reader thread may still log).
std::streambuf *makeTeeLogBuf(std::streambuf *orig, const std::string &dir, bool debug);

}  // namespace spn
