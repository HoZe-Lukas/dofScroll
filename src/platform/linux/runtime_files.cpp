// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "runtime_files.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <iostream>
#include <memory>
#include <mutex>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../common.hpp"
#include "../runtime_path.hpp"

namespace spn {

namespace {
const char *const LOG = "[dofscroll] ";
// Status file: when unchanged, rewrite at most this often ("updated" timestamp).
constexpr int64_t STATUS_REFRESH_NS = 1'000'000'000;
}  // namespace

namespace {
std::string fileName(RuntimeFile f, bool tmp) {
    std::string n = runtimeFileName(f);
    if (tmp) n += ".tmp." + std::to_string(static_cast<long long>(getpid()));
    return n;
}
}  // namespace

RuntimeDir::RuntimeDir(const std::string &dir) {
    if (!runtimeDirLexicallyValid(dir)) return;  // defense in depth, the checks below are the real ones
    const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return;
    struct stat ds;
    if (fstat(fd, &ds) != 0 || !S_ISDIR(ds.st_mode) || ds.st_uid != geteuid() || (ds.st_mode & 022) != 0) {
        ::close(fd);
        return;
    }
    fd_ = fd;
}
RuntimeDir::~RuntimeDir() {
    if (fd_ >= 0) ::close(fd_);
}
int RuntimeDir::open(RuntimeFile f, int flags, mode_t mode, bool tmp) const {
    return fd_ < 0 ? -1 : ::openat(fd_, fileName(f, tmp).c_str(), flags, mode);
}
bool RuntimeDir::unlink(RuntimeFile f, bool tmp) const {
    return fd_ >= 0 && ::unlinkat(fd_, fileName(f, tmp).c_str(), 0) == 0;
}
bool RuntimeDir::rename(RuntimeFile from, RuntimeFile to) const {
    return fd_ >= 0 && ::renameat(fd_, fileName(from, true).c_str(), fd_, fileName(to, false).c_str()) == 0;
}
bool RuntimeDir::lstat(RuntimeFile f, struct stat *st) const {
    return fd_ >= 0 && ::fstatat(fd_, fileName(f, false).c_str(), st, AT_SYMLINK_NOFOLLOW) == 0;
}

bool runtimeDirTrusted(const std::string &dir) { return RuntimeDir(dir).ok(); }

// Trust rule: the file must belong to the effective user and must not be accessible
// to group/others; the runtime dir itself must belong to the effective user and must
// not be group/other-writable (checked once) - otherwise the mechanism is ignored.
SuspendProbe::SuspendProbe(const std::string &dir)
    : dir_(std::make_shared<RuntimeDir>(dir)) {}

bool SuspendProbe::operator()() const {
    if (!dir_->ok()) return false;
    struct stat st;
    if (!dir_->lstat(RuntimeFile::SuspendFile, &st) || !S_ISREG(st.st_mode)) return false;
    if (st.st_uid != geteuid() || (st.st_mode & 077) != 0) return false;
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    long age_ms = (now.tv_sec - st.st_mtim.tv_sec) * 1000L +
                  (now.tv_nsec - st.st_mtim.tv_nsec) / 1000000L;
    return age_ms >= -SUSPEND_MAX_AGE_MS && age_ms <= SUSPEND_MAX_AGE_MS;
}

namespace {

// Mirrors everything that goes to std::cerr (stderr/journal stays unchanged) into the
// log file in the runtime dir as well (for display in the tray). Written on flush
// (std::endl) under a mutex, because the reader thread logs too. O_APPEND|O_NOFOLLOW,
// 0600; owner/type are checked. Rolling buffer: when max_lines (normal operation
// kLogMaxLines = 300, --debug kLogMaxLinesDebug) or kLogMaxBytes is exceeded, only the
// newest keep_lines lines remain (the file is rewritten atomically). Any error only
// disables the mirroring; the main thread never blocks (regular file on tmpfs,
// O_NONBLOCK).
class TeeLogBuf : public std::streambuf {
public:
    TeeLogBuf(std::streambuf *orig, const std::string &dir, size_t max_lines, size_t keep_lines)
        : orig_(orig), max_lines_(max_lines), keep_lines_(keep_lines) {
        rd_ = std::make_unique<RuntimeDir>(dir);
        if (!rd_->ok()) return;
        int fd = rd_->open(RuntimeFile::LogFile, O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        if (fd < 0) return;
        struct stat st;
        if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
            (st.st_mode & 077) != 0) {
            ::close(fd);
            return;
        }
        fd_ = fd;
        size_ = (size_t)st.st_size;
        loadExisting();  // a restart (Restart=always) must not lose the log
    }

protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        char ch = traits_type::to_char_type(c);
        return xsputn(&ch, 1) == 1 ? c : traits_type::eof();
    }
    std::streamsize xsputn(const char *s, std::streamsize n) override {
        std::streamsize r = orig_->sputn(s, n);
        std::lock_guard<std::mutex> lk(m_);
        if (fd_ >= 0 && pending_.size() < 64 * 1024) {
            // Timestamp (local time) only in the file, at the start of each line.
            for (std::streamsize i = 0; i < n; ++i) {
                if (at_line_start_) {
                    char ts[32];
                    std::time_t t = std::time(nullptr);
                    std::tm tmv{};
                    localtime_r(&t, &tmv);
                    if (std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S ", &tmv) > 0) pending_ += ts;
                    at_line_start_ = false;
                }
                pending_ += s[i];
                if (s[i] == '\n') at_line_start_ = true;
            }
        }
        return r;
    }
    int sync() override {
        int r = orig_->pubsync();
        std::lock_guard<std::mutex> lk(m_);
        flushPending();
        return r;
    }

private:
    std::streambuf *orig_;
    size_t max_lines_, keep_lines_;
    std::mutex m_;
    int fd_ = -1;
    size_t size_ = 0;
    std::string pending_;
    std::unique_ptr<RuntimeDir> rd_;
    std::deque<std::string> lines_;  // mirror of the file content (without \n)
    std::string partial_;            // line not yet terminated
    bool at_line_start_ = true;      // next character starts a line (timestamp)

    void loadExisting() {
        if (size_ == 0 || size_ > kLogMaxBytes) {
            if (size_ > kLogMaxBytes) rewrite();  // too long (old version): clear
            return;
        }
        int rfd = rd_->open(RuntimeFile::LogFile, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0);
        if (rfd < 0) return;
        std::string data(size_, '\0');
        ssize_t n = ::pread(rfd, &data[0], data.size(), 0);
        ::close(rfd);
        if (n <= 0) return;
        data.resize((size_t)n);
        size_t pos = 0;
        while (pos < data.size()) {
            size_t nl = data.find('\n', pos);
            if (nl == std::string::npos) break;
            lines_.push_back(data.substr(pos, nl - pos));
            pos = nl + 1;
        }
        if (lines_.size() > max_lines_) {
            while (lines_.size() > keep_lines_) lines_.pop_front();
            rewrite();
        }
    }

    // Rewrites lines_ atomically (temp + rename) and reopens the append descriptor.
    void rewrite() {
        rd_->unlink(RuntimeFile::LogFile, true);
        int tfd = rd_->open(RuntimeFile::LogFile, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600, true);
        if (tfd < 0) { disable(); return; }
        std::string out;
        for (const std::string &l : lines_) { out += l; out += '\n'; }
        const char *p = out.data();
        size_t left = out.size();
        bool ok = true;
        while (left > 0) {
            ssize_t w = ::write(tfd, p, left);
            if (w <= 0) { if (w < 0 && errno == EINTR) continue; ok = false; break; }
            p += w;
            left -= (size_t)w;
        }
        ::close(tfd);
        if (!ok || !rd_->rename(RuntimeFile::LogFile, RuntimeFile::LogFile)) {
            rd_->unlink(RuntimeFile::LogFile, true);
            disable();
            return;
        }
        if (fd_ >= 0) ::close(fd_);
        fd_ = rd_->open(RuntimeFile::LogFile, O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0);
        if (fd_ < 0) { disable(); return; }
        size_ = out.size();
    }

    void flushPending() {
        if (fd_ < 0 || pending_.empty()) {
            pending_.clear();
            return;
        }
        const char *p = pending_.data();
        size_t left = pending_.size();
        while (left > 0) {
            ssize_t w = ::write(fd_, p, left);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                disable();
                return;
            }
            p += w;
            left -= (size_t)w;
            size_ += (size_t)w;
        }
        // Count lines; on overflow trim to the newest keep_lines_.
        partial_ += pending_;
        pending_.clear();
        size_t pos = 0, nl;
        while ((nl = partial_.find('\n', pos)) != std::string::npos) {
            lines_.push_back(partial_.substr(pos, nl - pos));
            pos = nl + 1;
        }
        partial_.erase(0, pos);
        if (partial_.size() > 8192) partial_.clear();  // guard, never an endless line
        if (lines_.size() > max_lines_ || size_ > kLogMaxBytes) {
            while (lines_.size() > keep_lines_) lines_.pop_front();
            rewrite();
        }
    }
    void disable() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        pending_.clear();
    }
};

}  // namespace

std::streambuf *makeTeeLogBuf(std::streambuf *orig, const std::string &dir, bool debug) {
    return new TeeLogBuf(orig, dir, debug ? kLogMaxLinesDebug : kLogMaxLines,
                         debug ? kLogKeepLinesDebug : kLogKeepLines);
}

// Writes the status file (format: see common.hpp) atomically: temp file in the same
// directory (O_EXCL|O_NOFOLLOW, 0600) + rename. Errors are never fatal, logged once.
// Without a trusted runtime directory nothing happens.
StatusWriter::StatusWriter(const std::string &dir)
    : dir_(std::make_shared<RuntimeDir>(dir)) {}

void StatusWriter::update(const std::string &body, int64_t now_ns) {
    if (!dir_->ok()) return;
    if (body == last_body_ && now_ns < next_refresh_ns_) return;
    last_body_ = body;
    next_refresh_ns_ = now_ns + STATUS_REFRESH_NS;
    std::string text = body + "updated=" + std::to_string((long long)std::time(nullptr)) + "\n";
    if (text.size() > kStatusMaxBytes) return;
    if (!write(text) && !warned_) {
        warned_ = true;
        std::cerr << LOG << "WARNING: status file not writable (" << std::strerror(errno)
                  << ") - tray display limited" << std::endl;
    }
}

void StatusWriter::remove() {
    if (dir_->ok() && wrote_) dir_->unlink(RuntimeFile::StatusFile);
}

    bool StatusWriter::write(const std::string &text) {
        const RuntimeFile f = RuntimeFile::StatusFile;
        int fd = dir_->open(f, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600, true);
        if (fd < 0 && errno == EEXIST) {  // leftover of an earlier run with the same PID
            dir_->unlink(f, true);
            fd = dir_->open(f, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600, true);
        }
        if (fd < 0) return false;
        const char *p = text.data();
        size_t left = text.size();
        bool ok = true;
        while (left > 0) {
            ssize_t w = ::write(fd, p, left);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                ok = false;
                break;
            }
            p += w;
            left -= (size_t)w;
        }
        int e = errno;
        if (::close(fd) != 0 && ok) { ok = false; e = errno; }
        if (ok && !dir_->rename(f, f)) { ok = false; e = errno; }
        if (!ok) {
            dir_->unlink(f, true);
            errno = e;
            return false;
        }
        wrote_ = true;
        return true;
    }

}  // namespace spn
