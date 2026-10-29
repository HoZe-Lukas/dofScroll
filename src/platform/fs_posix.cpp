// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// POSIX implementation of the file-system layer (Linux and macOS), see fs.hpp. The bodies are the
// code that used to live in config.cpp, modes.cpp, seed.cpp and hid_descriptor.cpp.
#include "fs.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/random.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "path_rules.hpp"

namespace spn::fs {

namespace {

void fill(const struct stat &st, FileInfo *out) {
    out->isFile = S_ISREG(st.st_mode);
    out->isDir = S_ISDIR(st.st_mode);
    out->isLink = S_ISLNK(st.st_mode);
    out->size = static_cast<int64_t>(st.st_size);
#ifdef __APPLE__
    out->mtimeNs = static_cast<int64_t>(st.st_mtimespec.tv_sec) * 1'000'000'000 + st.st_mtimespec.tv_nsec;
    out->ctimeNs = static_cast<int64_t>(st.st_ctimespec.tv_sec) * 1'000'000'000 + st.st_ctimespec.tv_nsec;
#else
    out->mtimeNs = static_cast<int64_t>(st.st_mtim.tv_sec) * 1'000'000'000 + st.st_mtim.tv_nsec;
    out->ctimeNs = static_cast<int64_t>(st.st_ctim.tv_sec) * 1'000'000'000 + st.st_ctim.tv_nsec;
#endif
    out->fileId = static_cast<uint64_t>(st.st_ino);
}

bool setMsg(std::string *err, const std::string &m) {
    if (err) *err = m;
    return false;
}

bool isAbsEnv(const char *p) { return p && p[0] == '/'; }

}  // namespace

bool statPath(const std::string &path, FileInfo *out) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    if (out) fill(st, out);
    return true;
}

bool lstatPath(const std::string &path, FileInfo *out) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (out) fill(st, out);
    return true;
}

namespace {
ReadResult readAt(int dirfd, const std::string &path, size_t cap, const ReadOptions &opt, std::string *out) {
    ReadResult r;
    int flags = O_RDONLY | O_NONBLOCK | O_CLOEXEC;
    if (opt.refuseLinks) flags |= O_NOFOLLOW;
    const int fd = ::openat(dirfd, path.c_str(), flags);
    if (fd < 0) {
        r.status = errno == ENOENT ? ReadStatus::NotFound : ReadStatus::OpenFailed;
        r.sysError = std::strerror(errno);
        return r;
    }
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        r.status = ReadStatus::NotRegular;
        return r;
    }
    if (opt.requireOwner && st.st_uid != ::geteuid()) {
        ::close(fd);
        r.status = ReadStatus::NotOwned;
        return r;
    }
    if (opt.requirePrivate && (st.st_mode & 077) != 0) {
        ::close(fd);
        r.status = ReadStatus::NotOwned;
        return r;
    }
    r.foreignOwner = st.st_uid != ::geteuid();
    // Group write counts only for a group that is not the user's own (user-private groups with umask 002 are common).
    r.looseMode = (st.st_mode & S_IWOTH) != 0 || ((st.st_mode & S_IWGRP) != 0 && st.st_gid != ::getegid());
    if (st.st_size > 0 && static_cast<size_t>(st.st_size) > cap) {
        ::close(fd);
        r.status = ReadStatus::TooLargeBySize;
        return r;
    }
    out->clear();
    char buf[16384];
    for (;;) {
        const ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            r.status = ReadStatus::ReadFailed;
            r.sysError = std::strerror(errno);
            break;
        }
        if (n == 0) break;
        if (out->size() + static_cast<size_t>(n) > cap) {
            r.status = ReadStatus::TooLargeWhileReading;
            break;
        }
        out->append(buf, static_cast<size_t>(n));
    }
    ::close(fd);
    return r;
}
}  // namespace

ReadResult readFileBounded(const std::string &path, size_t cap, const ReadOptions &opt, std::string *out) {
    return readAt(AT_FDCWD, path, cap, opt, out);
}

DirHandle::~DirHandle() {
    if (fd_ >= 0) ::close(fd_);
}

bool DirHandle::open(const std::string &dir, std::string *err) {
    fd_ = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd_ < 0) {
        if (err) *err = std::strerror(errno);
        return false;
    }
    dir_ = dir;
    return true;
}

ReadResult DirHandle::readFileBounded(const std::string &name, size_t cap, const ReadOptions &opt,
                                      std::string *out) const {
    return readAt(fd_, name, cap, opt, out);
}

bool createFileExclusive(const std::string &path, const std::string &data) {
    const std::string tmp = path + ".tmp." + std::to_string(static_cast<long>(::getpid()));
    ::unlink(tmp.c_str());  // leftover of an earlier run with the same pid
    const int out = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (out < 0) return false;
    size_t off = 0;
    bool ok = true;
    while (off < data.size()) {
        const ssize_t n = ::write(out, data.data() + off, data.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            ok = false;
            break;
        }
        off += static_cast<size_t>(n);
    }
    if (::close(out) != 0) ok = false;
    // link() fails with EEXIST instead of overwriting, and the final name only ever holds a
    // complete file (a kill in between leaves only the temp file).
    if (ok && ::link(tmp.c_str(), path.c_str()) != 0) ok = false;
    ::unlink(tmp.c_str());
    return ok;
}

bool touchFilePrivate(const std::string &path) {
    // O_NOFOLLOW: a link at the final name is refused; O_NONBLOCK: a FIFO cannot hang the open.
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
    if (fd < 0) return false;
    struct stat st;
    bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == ::geteuid() && (st.st_mode & 077) == 0;
    if (ok) ok = ::futimens(fd, nullptr) == 0;
    ::close(fd);
    return ok;
}

bool removeFile(const std::string &path) { return ::unlink(path.c_str()) == 0; }
bool removeDir(const std::string &path) { return ::rmdir(path.c_str()) == 0; }
bool renamePath(const std::string &from, const std::string &to) { return ::rename(from.c_str(), to.c_str()) == 0; }
bool renameNoReplace(const std::string &from, const std::string &to, bool *exists) {
    if (exists) *exists = false;
    // link() fails with EEXIST instead of replacing: the new name appears atomically, then the old one goes.
    if (::link(from.c_str(), to.c_str()) == 0) {
        ::unlink(from.c_str());
        return true;
    }
    if (errno == EEXIST) {
        if (exists) *exists = true;
        return false;
    }
    // A file system without hard links: the check and the rename are two steps there (best effort).
    struct stat st;
    if (::lstat(to.c_str(), &st) == 0) {
        if (exists) *exists = true;
        return false;
    }
    return ::rename(from.c_str(), to.c_str()) == 0;
}
long processId() { return static_cast<long>(::getpid()); }

MkdirResult mkdirPrivate(const std::string &path, std::string *err) {
    if (::mkdir(path.c_str(), 0700) == 0) return MkdirResult::Created;
    if (errno == EEXIST) return MkdirResult::Exists;
    if (err) *err = std::strerror(errno);
    return MkdirResult::Failed;
}

bool mkdirsPrivate(const std::string &dir, std::string *emsg) {
    size_t i = 0;
    while (i <= dir.size()) {
        size_t j = dir.find('/', i);
        if (j == std::string::npos) j = dir.size();
        std::string cur = dir.substr(0, j);
        if (!cur.empty() && cur != "." && cur != ".." && j > i) {
            if (::mkdir(cur.c_str(), 0700) != 0 && errno != EEXIST) {
                if (emsg) *emsg = cur + ": " + std::strerror(errno);
                return false;
            }
        }
        i = j + 1;
    }
    // The directory that will hold the files may have existed: it must be ours (or root's) and not writable by every
    // user, otherwise another user could swap the files in it. A sticky directory (like /tmp) is fine. Group write is
    // not refused (a private group is common); the loader says so for the configuration file itself.
    struct stat st;
    if (!dir.empty() && ::stat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        if (st.st_uid != ::geteuid() && st.st_uid != 0) {
            if (emsg) *emsg = dir + ": directory is owned by another user";
            return false;
        }
        if ((st.st_mode & S_IWOTH) && !(st.st_mode & S_ISVTX)) {
            if (emsg) *emsg = dir + ": directory is writable by every user (no sticky bit)";
            return false;
        }
    }
    return true;
}

bool listDir(const std::string &dir, size_t maxEntries, std::vector<std::string> *names, bool *capped) {
    DIR *d = ::opendir(dir.c_str());
    if (!d) return false;
    if (capped) *capped = false;
    size_t seen = 0;
    while (struct dirent *e = ::readdir(d)) {
        if (maxEntries && ++seen > maxEntries) {
            if (capped) *capped = true;
            break;
        }
        names->push_back(e->d_name);
    }
    ::closedir(d);
    return true;
}

void randomHex16(char out[17]) {
    unsigned char r[8];
    bool have = false;
#ifdef __linux__
    have = ::getrandom(r, sizeof r, 0) == static_cast<ssize_t>(sizeof r);
#elif defined(__APPLE__)
    have = ::getentropy(r, sizeof r) == 0;
#endif
    if (!have) {
        const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        ssize_t n = fd >= 0 ? ::read(fd, r, sizeof r) : -1;
        if (fd >= 0) ::close(fd);
        have = n == static_cast<ssize_t>(sizeof r);
    }
    if (!have) {  // last resort: time + PID
        struct timespec ts;
        ::clock_gettime(CLOCK_REALTIME, &ts);
        unsigned long long v = static_cast<unsigned long long>(ts.tv_nsec) * 6364136223846793005ULL ^
                               static_cast<unsigned long long>(::getpid());
        std::memcpy(r, &v, sizeof r);
    }
    for (int k = 0; k < 8; k++) std::snprintf(out + 2 * k, 3, "%02x", r[k]);
}

static bool replaceFileImpl(const std::string &path, const std::string &content, bool followLink,
                            std::string *err) {
    // Resolve symlink: temp next to the real target, so the link is preserved.
    std::string real = path;
    bool resolved = false;
    for (int depth = 0; depth <= 16; depth++) {
        if (!followLink) { resolved = true; break; }  // runtime files: the final name is never resolved
        struct stat lst;
        if (::lstat(real.c_str(), &lst) != 0 || !S_ISLNK(lst.st_mode)) { resolved = true; break; }
        if (depth == 16) break;
        std::string l(4096, '\0');
        ssize_t n = ::readlink(real.c_str(), &l[0], l.size());
        if (n < 0) return setMsg(err, real + ": " + std::strerror(errno));
        l.resize(static_cast<size_t>(n));
        if (l.empty()) return setMsg(err, real + ": empty symlink");
        if (l[0] == '/') {
            real = l;
        } else {
            size_t slash = real.rfind('/');
            real = (slash == std::string::npos ? std::string() : real.substr(0, slash + 1)) + l;
        }
    }
    if (!resolved) return setMsg(err, path + ": " + std::strerror(ELOOP) + " (more than 16 symlinks)");

    std::string dir, base;
    {
        size_t slash = real.rfind('/');
        if (slash == std::string::npos) { dir = "."; base = real; }
        else { dir = slash == 0 ? "/" : real.substr(0, slash); base = real.substr(slash + 1); }
    }
    if (base.empty() || base == "." || base == "..") return setMsg(err, real + ": invalid file name");
    if (dir != "." && dir != "/") {
        std::string emsg;
        if (!mkdirsPrivate(dir, &emsg)) return setMsg(err, emsg);
    }
    // Open the directory once; temp file, rename and fsync all go through this handle.
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) return setMsg(err, dir + ": " + std::strerror(errno));
    struct DirGuard { int fd; ~DirGuard() { ::close(fd); } } guard{dfd};

    mode_t mode = 0600;
    struct stat est;
    if (::fstatat(dfd, base.c_str(), &est, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(est.st_mode)) return setMsg(err, real + ": not a regular file");
        mode = est.st_mode & 0777 & ~mode_t(022);  // never carry over setuid/setgid/sticky; a file loosened to 0666 loses group/other write
    }

    std::string tmp;
    int fd = -1;
    for (int attempt = 0; attempt < 8 && fd < 0; attempt++) {
        char suf[17];
        randomHex16(suf);
        tmp = base + ".tmp." + suf;
        fd = ::openat(dfd, tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST) return setMsg(err, dir + "/" + tmp + ": " + std::strerror(errno));
    }
    if (fd < 0) return setMsg(err, dir + "/" + base + ".tmp.*: " + std::strerror(EEXIST));

    std::string m;
    const std::string &data = content;
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { m = tmp + ": write failed: " + std::strerror(n < 0 ? errno : EIO); break; }
        off += static_cast<size_t>(n);
    }
    if (m.empty() && ::fchmod(fd, mode) != 0) m = tmp + ": chmod: " + std::strerror(errno);
    if (m.empty() && ::fsync(fd) != 0) m = tmp + ": fsync: " + std::strerror(errno);
    if (::close(fd) != 0 && m.empty()) m = tmp + ": close: " + std::strerror(errno);
    if (m.empty() && ::renameat(dfd, tmp.c_str(), dfd, base.c_str()) != 0) m = real + ": " + std::strerror(errno);
    if (!m.empty()) {
        (void)::unlinkat(dfd, tmp.c_str(), 0);
        return setMsg(err, m);
    }
    (void)::fsync(dfd);  // persist the directory entry (best effort)
    return true;
}

bool writeFileReplace(const std::string &path, const std::string &content, std::string *err) {
    return replaceFileImpl(path, content, true, err);
}

bool writeFileReplaceNoFollow(const std::string &path, const std::string &content, std::string *err) {
    return replaceFileImpl(path, content, false, err);
}

std::string executablePath() {
#ifdef __APPLE__
    char buf[4096];
    uint32_t sz = sizeof buf;
    if (_NSGetExecutablePath(buf, &sz) != 0) return "";
    char *rp = ::realpath(buf, nullptr);
    std::string s = rp ? rp : buf;
    std::free(rp);
    return s;
#else
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    buf[n] = 0;
    return buf;
#endif
}

std::string getEnvUtf8(const char *name) {
    const char *v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

std::string userConfigBase() {
#ifdef __APPLE__
    const char *home = std::getenv("HOME");
    if (home && *home) return macConfigBase(home);
    struct passwd *pw = ::getpwuid(::getuid());
    if (pw && isAbsEnv(pw->pw_dir)) return macConfigBase(pw->pw_dir);
    return macConfigBase("");
#else
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (isAbsEnv(xdg)) return xdg;  // relative -> ignore (XDG spec)
    const char *home = std::getenv("HOME");
    if (home && *home) return std::string(home) + "/.config";
    struct passwd *pw = ::getpwuid(::getuid());
    if (pw && isAbsEnv(pw->pw_dir)) return std::string(pw->pw_dir) + "/.config";
    return "./.config";
#endif
}

size_t lastSeparator(const std::string &p) { return p.rfind('/'); }

}  // namespace spn::fs
