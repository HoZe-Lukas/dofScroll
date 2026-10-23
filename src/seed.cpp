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

constexpr size_t kMaxFileBytes = 64 * 1024;  // the example configuration file

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

}  // namespace

// The example configuration of this operating system: the axis order of the official driver differs
// between Linux (spacenavd), Windows and macOS, so each has its own file in examples/default/<os>/.
const char *seedOsName() {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#else
    return "linux";
#endif
}

std::string examplesDir() { return findExamplesDir(); }

bool seedDefaults(std::string *log) {
    // The joystick modes and button modes are built into the program (builtin_library.hpp), so
    // only the default configuration is copied here.
    const std::string prev = previousConfigPath();
    const std::string modesPath = modesDir();  // <base>/dofscroll/modes
    if (prev.empty() || modesPath.empty()) return false;
    const std::string cfgDir = dirName(modesPath);
    const std::string cfgFile = cfgDir + "/dofscroll.conf";

    if (lexists(cfgFile) || lexists(prev) || lexists(legacyConfigPath())) return false;

    const std::string ex = findExamplesDir();
    if (ex.empty()) return false;
    if (!mkdirs(cfgDir)) return false;
    if (!copyFile(ex + "/default/" + seedOsName() + "/dofscroll.conf", cfgFile)) return false;
    if (log) *log = "seeded default config into " + cfgDir + " from " + ex;
    return true;
}

}  // namespace spn
