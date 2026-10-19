// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// First-start seeding, see seed.hpp.
#include "seed.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>

#include "config.hpp"
#include "modes.hpp"

namespace spn {
namespace {

constexpr off_t kMaxFileBytes = 64 * 1024;  // per example file
constexpr size_t kMaxModeFiles = 64;        // per seeding run

bool lexists(const std::string &p) {
    struct stat st;
    return ::lstat(p.c_str(), &st) == 0;
}

bool isDir(const std::string &p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string dirName(const std::string &p) {
    const size_t i = p.rfind('/');
    return i == std::string::npos ? std::string(".") : (i == 0 ? std::string("/") : p.substr(0, i));
}

// mkdir -p with mode 0700; true if the directory exists afterwards.
bool mkdirs(const std::string &dir) {
    if (dir.empty()) return false;
    if (isDir(dir)) return true;
    const std::string parent = dirName(dir);
    if (parent != dir && !isDir(parent) && !mkdirs(parent)) return false;
    return (::mkdir(dir.c_str(), 0700) == 0 || errno == EEXIST) && isDir(dir);
}

// Copies src to dst. Refuses symlinks, non-regular and oversized sources; never overwrites dst.
bool copyFile(const std::string &src, const std::string &dst) {
    const int in = ::open(src.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (in < 0) return false;
    struct stat st;
    if (::fstat(in, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > kMaxFileBytes) {
        ::close(in);
        return false;
    }
    std::string data;
    char buf[4096];
    for (;;) {
        const ssize_t n = ::read(in, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        data.append(buf, static_cast<size_t>(n));
        if (static_cast<off_t>(data.size()) > kMaxFileBytes) {
            ::close(in);
            return false;
        }
    }
    ::close(in);
    const std::string tmp = dst + ".tmp." + std::to_string(static_cast<long>(::getpid()));
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
    if (ok && ::link(tmp.c_str(), dst.c_str()) != 0) ok = false;
    ::unlink(tmp.c_str());
    return ok;
}

// Removes the regular files of a temporary directory and the directory itself (best effort).
void removeTempDir(const std::string &dir) {
    if (DIR *d = ::opendir(dir.c_str())) {
        while (const dirent *e = ::readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            struct stat st;
            const std::string f = dir + "/" + n;
            if (::lstat(f.c_str(), &st) == 0 && !S_ISDIR(st.st_mode)) ::unlink(f.c_str());
        }
        ::closedir(d);
    }
    ::rmdir(dir.c_str());
}

// Cleans up "modes.tmp.*" directories that an interrupted earlier run left behind.
void removeStaleModeTemps(const std::string &cfgDir) {
    std::vector<std::string> stale;
    if (DIR *d = ::opendir(cfgDir.c_str())) {
        while (const dirent *e = ::readdir(d)) {
            const std::string n = e->d_name;
            struct stat st;
            if (n.rfind("modes.tmp.", 0) == 0 && ::lstat((cfgDir + "/" + n).c_str(), &st) == 0 &&
                S_ISDIR(st.st_mode))
                stale.push_back(cfgDir + "/" + n);
        }
        ::closedir(d);
    }
    for (const auto &p : stale) removeTempDir(p);
}

std::string exeDir() {
    char buf[4096];
    const ssize_t n = ::readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    buf[n] = 0;
    return dirName(buf);
}

// First existing examples directory: $DOFSCROLL_EXAMPLES, <exe_dir>/examples,
// <exe_dir>/../share/dofscroll/examples, /usr/local/share/..., /usr/share/...
std::string findExamplesDir() {
    std::vector<std::string> c;
    if (const char *e = std::getenv("DOFSCROLL_EXAMPLES"); e && *e) c.push_back(e);
    const std::string ed = exeDir();
    if (!ed.empty()) {
        c.push_back(ed + "/examples");
        c.push_back(ed + "/../share/dofscroll/examples");
    }
    c.push_back("/usr/local/share/dofscroll/examples");
    c.push_back("/usr/share/dofscroll/examples");
    for (const auto &d : c)
        if (isDir(d)) return d;
    return "";
}

bool endsWith(const std::string &s, const std::string &suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

}  // namespace

bool seedDefaults(std::string *log) {
    const std::string prev = previousConfigPath();
    const std::string modesPath = modesDir();  // <base>/dofscroll/modes
    if (prev.empty() || modesPath.empty()) return false;
    const std::string cfgDir = dirName(modesPath);
    const std::string cfgFile = cfgDir + "/dofscroll.conf";

    const bool needCfg = !lexists(cfgFile) && !lexists(prev) && !lexists(legacyConfigPath());
    const bool needModes = !lexists(modesPath);
    if (!needCfg && !needModes) return false;

    const std::string ex = findExamplesDir();
    if (ex.empty()) return false;
    if (!mkdirs(cfgDir)) return false;

    std::string msg;
    if (needCfg && copyFile(ex + "/default/dofscroll.conf", cfgFile)) msg += "default config";
    if (needModes) {
        const std::string srcModes = ex + "/modes";
        std::vector<std::string> names;
        if (DIR *d = ::opendir(srcModes.c_str())) {
            while (const dirent *e = ::readdir(d)) {
                const std::string n = e->d_name;
                if (n.empty() || n[0] == '.' || !endsWith(n, ".conf")) continue;
                struct stat st;
                // lstat + S_ISREG rejects symlinks
                if (::lstat((srcModes + "/" + n).c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                names.push_back(n);
            }
            ::closedir(d);
        }
        std::sort(names.begin(), names.end());
        if (names.size() > kMaxModeFiles) names.resize(kMaxModeFiles);
        if (!names.empty()) {
            removeStaleModeTemps(cfgDir);
            const std::string tmpDir = cfgDir + "/modes.tmp." + std::to_string(static_cast<long>(::getpid()));
            if (::mkdir(tmpDir.c_str(), 0700) == 0) {
                int cnt = 0;
                for (const auto &n : names)
                    if (copyFile(srcModes + "/" + n, tmpDir + "/" + n)) ++cnt;
                // Publish the complete set in one step: an interrupted run never leaves a partial
                // modes/ directory that would be treated as "already seeded".
                if (cnt > 0 && ::rename(tmpDir.c_str(), modesPath.c_str()) == 0)
                    msg += std::string(msg.empty() ? "" : ", ") + std::to_string(cnt) + " mode file(s)";
                else
                    removeTempDir(tmpDir);
            }
        }
    }
    if (msg.empty()) return false;
    if (log) *log = "seeded " + msg + " into " + cfgDir + " from " + ex;
    return true;
}

}  // namespace spn
