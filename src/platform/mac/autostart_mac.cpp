// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS "Start at login": per-user LaunchAgents (no sudo, nothing outside $HOME). Plists are
// rendered by autostart_os.hpp (XML-escaped paths) and written atomically through the file layer;
// launchctl is called with an argument vector, never through a shell.
#include "autostart_mac.hpp"

#include <mach-o/dyld.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>

#include "../exec_posix.hpp"
#include "../fs.hpp"

namespace spn {
namespace autostart {

std::string agentPlistPath(const std::string &dir, const std::string &label) {
    return dir + "/" + label + ".plist";
}

namespace {

bool launchctlLoaded(unsigned uid, const std::string &label) {
    const ExecResult r = execCapture(launchctlPrintArgv(uid, label), 10000);
    return r.started && r.status == 0;
}

void sleep200ms() {
    struct timespec ts = {0, 200 * 1000 * 1000};
    nanosleep(&ts, nullptr);
}

// bootout (failure ignored, like the install script) and wait until the job is really gone.
bool bootoutAndWait(unsigned uid, const std::string &label, std::string *err) {
    if (!launchctlLoaded(uid, label)) return true;
    execCapture(launchctlBootoutArgv(uid, label));
    if (waitUntilGone([&] { return launchctlLoaded(uid, label); }, sleep200ms)) return true;
    if (err) *err = label + " is still loaded after bootout";
    return false;
}

std::string homeDir() {
    const char *h = getenv("HOME");
    return h ? h : "";
}

std::string exeDir() {
    char buf[PATH_MAX];
    uint32_t n = sizeof buf;
    if (_NSGetExecutablePath(buf, &n) != 0) return "";
    char real[PATH_MAX];
    if (!realpath(buf, real)) return "";
    std::string p = real;
    const size_t slash = p.find_last_of('/');
    return slash == std::string::npos ? "" : p.substr(0, slash + 1);
}

std::vector<AgentSpec> defaultSpecs() {
    const std::string dir = exeDir();
    return {{kMacDaemonLabel, dir + "dofscroll", true, false}, {kMacUiLabel, dir + "dofscroll-ui", false, true}};
}

}  // namespace

AutostartState queryAgents(const std::string &dir, const std::vector<AgentSpec> &specs, std::string *detail) {
    std::vector<Entry> es;
    for (const AgentSpec &s : specs) {
        Entry e;
        e.name = s.label;
        e.optional = s.optional;
        fs::FileInfo fi;
        if (!fs::statPath(s.program, &fi) || !fi.isFile) {
            e.kind = Entry::Missing;
        } else if (!plistPathOk(s.program)) {
            e.kind = Entry::Blocked;
            e.why = "unsupported path";
        } else {
            std::string content;
            const fs::ReadResult rr = fs::readFileBounded(agentPlistPath(dir, s.label), 64 * 1024, fs::ReadOptions(), &content);
            if (rr.status == fs::ReadStatus::NotFound) e.kind = Entry::Absent;
            else if (!rr.ok()) { e.kind = Entry::Failed; e.why = rr.sysError.empty() ? "cannot read plist" : rr.sysError; }
            else e.kind = content == renderLaunchAgentPlist(s.label, s.program, s.daemon) ? Entry::Present : Entry::Different;
        }
        es.push_back(e);
    }
    return combineEntries(es, detail);
}

bool setAgents(const std::string &dir, const std::vector<AgentSpec> &specs, bool enabled, bool applyNow,
               unsigned uid, std::string *err) {
    auto fail = [&](const std::string &m) { if (err) *err = m; return false; };
    const bool dirExisted = fs::pathExists(dir);
    for (const AgentSpec &s : specs) {
        const std::string plist = agentPlistPath(dir, s.label);
        if (!enabled) {
            if (applyNow) {
                std::string e;
                if (!bootoutAndWait(uid, s.label, &e)) return fail(e);
            }
            if (fs::pathExists(plist) && !fs::removeFile(plist)) return fail("cannot remove " + plist);
            continue;
        }
        fs::FileInfo fi;
        if (!fs::statPath(s.program, &fi) || !fi.isFile) {
            if (s.optional) continue;
            return fail(s.program + " not found");
        }
        if (!plistPathOk(s.program)) return fail("unsupported character in path: " + s.program);
        std::string e;
        if (!fs::writeFileReplace(plist, renderLaunchAgentPlist(s.label, s.program, s.daemon), &e)) return fail(e);
        // launchd refuses plists that are group- or world-writable; 0644 is what the script uses.
        if (chmod(plist.c_str(), 0644) != 0) return fail("cannot set permissions on " + plist);
        if (applyNow) {
            if (!bootoutAndWait(uid, s.label, &e)) return fail(e);
            const ExecResult r = execCapture(launchctlBootstrapArgv(uid, plist));
            if (!r.started) return fail(r.error);
            if (r.status != 0) return fail("launchctl bootstrap failed for " + s.label);
        }
    }
    if (enabled && !dirExisted) chmod(dir.c_str(), 0755);  // created by the file layer as 0700
    return true;
}

AutostartState queryAutostart(std::string *detail) {
    const std::string home = homeDir();
    if (home.empty() || home[0] != '/') {
        if (detail) *detail = "HOME is not set";
        return AutostartState::Unavailable;
    }
    return queryAgents(home + "/Library/LaunchAgents", defaultSpecs(), detail);
}

bool setAutostart(bool enabled, std::string *err) {
    const std::string home = homeDir();
    if (home.empty() || home[0] != '/') {
        if (err) *err = "HOME is not set";
        return false;
    }
    return setAgents(home + "/Library/LaunchAgents", defaultSpecs(), enabled, false, getuid(), err);
}

}  // namespace autostart
}  // namespace spn
