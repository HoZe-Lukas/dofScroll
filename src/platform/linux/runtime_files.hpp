// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Linux runtime files in the private XDG_RUNTIME_DIR: suspend heartbeat probe, status file
// writer and the log mirror (see common.hpp for the formats). Used by the Linux daemon main
// and handed to the platform-independent core as callbacks.
#pragma once
#include <cstdint>
#include <streambuf>
#include <string>

namespace spn {

// Is `dir` trustworthy? (owned by the effective user, not group/other-writable) -
// prerequisite for the status and log files. Empty dir: false.
bool runtimeDirTrusted(const std::string &dir);

// Is the tray pausing the output right now (fresh heartbeat file in `dir`)?
class SuspendProbe {
public:
    explicit SuspendProbe(const std::string &dir);
    bool operator()() const;
private:
    std::string path_;
    bool dir_ok_ = false;
};

// Writes the status file atomically (temp file + rename), errors are never fatal.
class StatusWriter {
public:
    explicit StatusWriter(const std::string &dir);
    // body = lines without "updated"; written on change or after one second.
    void update(const std::string &body, int64_t now_ns);
    void remove();
private:
    std::string path_, last_body_;
    int64_t next_refresh_ns_ = 0;
    bool warned_ = false, wrote_ = false;
    bool write(const std::string &text);
};

// Stream buffer that mirrors everything written into the rolling log file in `dir`.
// Deliberately never destroyed by the caller (a detached reader thread may still log).
std::streambuf *makeTeeLogBuf(std::streambuf *orig, const std::string &dir, bool debug);

}  // namespace spn
