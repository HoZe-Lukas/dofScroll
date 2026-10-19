// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// First-start seeding, see seed.hpp.
#include "seed.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "config.hpp"
#include "modes.hpp"
#include "platform/fs.hpp"
#include "platform/path_rules.hpp"

namespace spn {
namespace {

constexpr size_t kMaxFileBytes = 64 * 1024;  // per example file
constexpr size_t kMaxModeFiles = 64;         // per seeding run

bool lexists(const std::string &p) { return fs::pathExists(p); }

bool isDir(const std::string &p) {
    fs::FileInfo i;
    return fs::statPath(p, &i) && i.isDir;
}

std::string dirName(const std::string &p) {
    const size_t i = fs::lastSeparator(p);
    return i == std::string::npos ? std::string(".") : (i == 0 ? std::string("/") : p.substr(0, i));
}

// mkdir -p with mode 0700; true if the directory exists afterwards.
bool mkdirs(const std::string &dir) {
    if (dir.empty()) return false;
    if (isDir(dir)) return true;
    const std::string parent = dirName(dir);
    if (parent != dir && !isDir(parent) && !mkdirs(parent)) return false;
    return fs::mkdirPrivate(dir, nullptr) != fs::MkdirResult::Failed && isDir(dir);
}

// Copies src to dst. Refuses symlinks, non-regular and oversized sources; never overwrites dst.
bool copyFile(const std::string &src, const std::string &dst) {
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    std::string data;
    if (!fs::readFileBounded(src, kMaxFileBytes, opt, &data).ok()) return false;
    return fs::createFileExclusive(dst, data);
}

std::string exeDir() {
    const std::string exe = fs::executablePath();
    return exe.empty() ? std::string() : dirName(exe);
}

// First existing examples directory: $DOFSCROLL_EXAMPLES, <exe_dir>/examples,
// <exe_dir>/../share/dofscroll/examples, /usr/local/share/..., /usr/share/...
std::string findExamplesDir() {
    std::vector<std::string> c;
    if (const char *e = std::getenv("DOFSCROLL_EXAMPLES"); e && *e) c.push_back(toSlashes(e));
    const std::string ed = exeDir();
    if (!ed.empty()) {
        c.push_back(ed + "/examples");
        c.push_back(ed + "/../share/dofscroll/examples");
    }
#ifndef _WIN32
    c.push_back("/usr/local/share/dofscroll/examples");
    c.push_back("/usr/share/dofscroll/examples");
#endif
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
        std::vector<std::string> all;
        bool capped = false;
        if (fs::listDir(srcModes, 0, &all, &capped)) {
            for (const std::string &n : all) {
                if (n.empty() || n[0] == '.' || !endsWith(n, ".conf")) continue;
                fs::FileInfo fi;
                // lstat + regular-file check rejects symlinks
                if (!fs::lstatPath(srcModes + "/" + n, &fi) || !fi.isFile) continue;
                names.push_back(n);
            }
        }
        std::sort(names.begin(), names.end());
        if (names.size() > kMaxModeFiles) names.resize(kMaxModeFiles);
        if (!names.empty() && fs::mkdirPrivate(modesPath, nullptr) == fs::MkdirResult::Created) {
            int cnt = 0;
            for (const auto &n : names)
                if (copyFile(srcModes + "/" + n, modesPath + "/" + n)) ++cnt;
            if (cnt) msg += std::string(msg.empty() ? "" : ", ") + std::to_string(cnt) + " mode file(s)";
        }
    }
    if (msg.empty()) return false;
    if (log) *log = "seeded " + msg + " into " + cfgDir + " from " + ex;
    return true;
}

}  // namespace spn
