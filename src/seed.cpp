// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// First-start seeding, see seed.hpp.
#include "seed.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "config.hpp"
#include "button_modes.hpp"
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

// Copies src to dst. Refuses symlinks, non-regular and oversized sources; never overwrites dst;
// dst only ever holds the complete file (see fs::createFileExclusive).
bool copyFile(const std::string &src, const std::string &dst) {
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    std::string data;
    if (!fs::readFileBounded(src, kMaxFileBytes, opt, &data).ok()) return false;
    return fs::createFileExclusive(dst, data);
}

// Removes the regular files of a temporary directory and the directory itself (best effort).
void removeTempDir(const std::string &dir) {
    std::vector<std::string> names;
    bool capped = false;
    if (fs::listDir(dir, 0, &names, &capped)) {
        for (const std::string &n : names) {
            if (n == "." || n == "..") continue;
            fs::FileInfo fi;
            const std::string f = dir + "/" + n;
            if (fs::lstatPath(f, &fi) && !fi.isDir) fs::removeFile(f);
        }
    }
    fs::removeDir(dir);
}

// Cleans up "<name>.tmp.*" directories that an interrupted earlier run left behind.
void removeStaleTemps(const std::string &cfgDir, const std::string &name) {
    std::vector<std::string> stale, names;
    bool capped = false;
    if (fs::listDir(cfgDir, 0, &names, &capped)) {
        for (const std::string &n : names) {
            fs::FileInfo fi;
            if (n.rfind(name + ".tmp.", 0) == 0 && fs::lstatPath(cfgDir + "/" + n, &fi) && fi.isDir)
                stale.push_back(cfgDir + "/" + n);
        }
    }
    for (const auto &p : stale) removeTempDir(p);
}

std::string exeDir() {
    const std::string exe = fs::executablePath();
    return exe.empty() ? std::string() : dirName(exe);
}

// First existing examples directory: $DOFSCROLL_EXAMPLES, <exe_dir>/examples,
// <exe_dir>/../share/dofscroll/examples, /usr/local/share/..., /usr/share/...
std::string findExamplesDir() {
    std::vector<std::string> c;
    const std::string envEx = fs::getEnvUtf8("DOFSCROLL_EXAMPLES");
    if (!envEx.empty()) c.push_back(toSlashes(envEx));
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

// Copies the *.conf files of srcDir into a new directory dstPath (named name under cfgDir), all at
// once: the complete set is published by one rename, so an interrupted run never leaves a partial
// directory that would count as "already seeded". Returns the number of files copied.
int seedDirectory(const std::string &srcDir, const std::string &cfgDir, const std::string &name,
                  const std::string &dstPath) {
    std::vector<std::string> names;
    std::vector<std::string> all;
    bool capped = false;
    if (fs::listDir(srcDir, 0, &all, &capped)) {
        for (const std::string &n : all) {
            if (n.empty() || n[0] == '.' || !endsWith(n, ".conf")) continue;
            fs::FileInfo fi;
            // lstat + regular-file check rejects symlinks
            if (!fs::lstatPath(srcDir + "/" + n, &fi) || !fi.isFile) continue;
            names.push_back(n);
        }
    }
    std::sort(names.begin(), names.end());
    if (names.size() > kMaxModeFiles) names.resize(kMaxModeFiles);
    if (names.empty()) return 0;
    removeStaleTemps(cfgDir, name);
    const std::string tmpDir = cfgDir + "/" + name + ".tmp." + std::to_string(fs::processId());
    if (fs::mkdirPrivate(tmpDir, nullptr) != fs::MkdirResult::Created) return 0;
    int cnt = 0;
    for (const auto &n : names)
        if (copyFile(srcDir + "/" + n, tmpDir + "/" + n)) ++cnt;
    if (cnt > 0 && fs::renamePath(tmpDir, dstPath)) return cnt;
    removeTempDir(tmpDir);
    return 0;
}

}  // namespace

std::string examplesDir() { return findExamplesDir(); }

bool seedDefaults(std::string *log) {
    const std::string prev = previousConfigPath();
    const std::string modesPath = modesDir();  // <base>/dofscroll/modes
    if (prev.empty() || modesPath.empty()) return false;
    const std::string cfgDir = dirName(modesPath);
    const std::string cfgFile = cfgDir + "/dofscroll.conf";

    const bool needCfg = !lexists(cfgFile) && !lexists(prev) && !lexists(legacyConfigPath());
    const bool needModes = !lexists(modesPath);
    const std::string buttonsPath = buttonModesDir();  // <base>/dofscroll/buttons
    const bool needButtons = !lexists(buttonsPath);
    if (!needCfg && !needModes && !needButtons) return false;

    const std::string ex = findExamplesDir();
    if (ex.empty()) return false;
    if (!mkdirs(cfgDir)) return false;

    std::string msg;
    if (needCfg && copyFile(ex + "/default/dofscroll.conf", cfgFile)) msg += "default config";
    if (needModes) {
        const int cnt = seedDirectory(ex + "/modes", cfgDir, "modes", modesPath);
        if (cnt > 0) msg += std::string(msg.empty() ? "" : ", ") + std::to_string(cnt) + " mode file(s)";
    }
    if (needButtons) {
        const int cnt = seedDirectory(ex + "/buttons", cfgDir, "buttons", buttonsPath);
        if (cnt > 0) msg += std::string(msg.empty() ? "" : ", ") + std::to_string(cnt) + " button mode file(s)";
    }
    if (msg.empty()) return false;
    if (log) *log = "seeded " + msg + " into " + cfgDir + " from " + ex;
    return true;
}

}  // namespace spn
