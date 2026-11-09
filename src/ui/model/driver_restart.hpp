// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Restart of the official 3Dconnexion driver (Windows and macOS), toolkit-free. The driver reads its
// settings files when it starts, so after the driver cleanup (driver_cleanup_model.hpp) or after any
// change in the driver's own panel it has to be started again. The user cannot always do that by hand:
// the settings window must be closed first, and the driver has no menu entry for it.
//
// What this does, in this order, and only to processes that
//   - belong to the signed-in user (the OS layer lists nothing else),
//   - run from the driver's own folder (an executable of the same name elsewhere is never touched),
//   - have one of the file names in the rules:
//   1. asks the driver's windows (its settings window) to close,
//   2. ends the driver service of this user (and the programs of stopNames, none by default),
//   3. starts the driver service again (unless the system already did),
//   4. waits until it runs again.
// A process the user may not end (for example one that runs with administrator rights) is left alone and
// named; the driver service itself decides whether the restart counts as done. No window is forced shut,
// no other user's process is touched, nothing is installed or written.
//
//   DriverRestartRules rules = driverRestartRules(DriverOs::Windows, "C:/Program Files", "C:/Program Files (x86)", "");
//   DriverRestartReport rep;  restartDriver(*ops, rules, &rep);
#pragma once
#include <string>
#include <vector>

namespace spn::ui {

enum class DriverOs { Windows, MacOs, Other };

struct DriverProcess {
    int pid = 0;
    std::string exe;  // the full path of the program file, as the OS reports it
};

// What the OS layer does. Implemented per OS (src/platform/{win,mac}/driver_restart_*.cpp) and by fakes in tests.
class DriverRestartOps {
public:
    virtual ~DriverRestartOps() = default;
    // The processes of the signed-in user (nothing of other users, nothing of the system). false + *err when impossible.
    virtual bool list(std::vector<DriverProcess> *out, std::string *err) = 0;
    // Asks the windows of the process to close (the same as the close button); does nothing where there is no such thing.
    virtual void askToClose(int pid) = 0;
    // Ends the process and waits a short while for it. false + *err ("access denied", ...) when that is not possible.
    virtual bool end(int pid, std::string *err) = 0;
    // Starts a program detached from this one.
    virtual bool start(const std::string &exe, const std::vector<std::string> &args, std::string *err) = 0;
    virtual void wait(int milliseconds) = 0;
};

struct DriverRestartRules {
    // Lower case, '/' as separator, with a trailing '/': the program file must be below one of these folders.
    std::vector<std::string> folders;
    // Lower case file names. closeNames: windows are asked to close, then the program is ended.
    std::vector<std::string> closeNames;
    // Ended (not started again by this code): the driver's helpers, which its service starts when it needs them.
    std::vector<std::string> stopNames;
    // The driver service: ended and started again with `serviceArgs`.
    std::string serviceName;
    std::vector<std::string> serviceArgs;
};

struct DriverRestartReport {
    bool supported = true;                // false: no restart on this system
    bool serviceFound = false;            // the driver service was running when the restart began
    bool startedAgain = false;            // the service runs again (started here or by the system)
    bool startedByUs = false;             // ... and it was this code that started it
    std::vector<std::string> closed;      // file names whose windows were asked to close
    std::vector<std::string> ended;       // file names ended
    std::vector<std::string> left;        // "name: reason": not ended, the restart does not depend on them
    std::vector<std::string> failed;      // "text": the restart did not work
    bool ok() const { return supported && failed.empty() && serviceFound && startedAgain; }
};

namespace driver_restart_detail {

inline std::string norm(const std::string &s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) o.push_back(c == '\\' ? '/' : (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);  // ASCII only
    return o;
}

inline std::string folderOf(std::string p) {  // normalized folder with trailing '/'
    p = norm(p);
    while (!p.empty() && p.back() == '/') p.pop_back();
    return p.empty() ? p : p + "/";
}

inline std::string baseName(const std::string &normalized) {
    const size_t s = normalized.rfind('/');
    return s == std::string::npos ? normalized : normalized.substr(s + 1);
}

inline bool listed(const std::vector<std::string> &v, const std::string &n) {
    for (const std::string &x : v)
        if (x == n) return true;
    return false;
}

enum class Role { None, Close, Stop, Service };

inline Role roleOf(const DriverRestartRules &r, const std::string &exe) {
    const std::string p = norm(exe);
    if (p.empty() || p.find("/../") != std::string::npos || p.find("/./") != std::string::npos) return Role::None;
    bool below = false;
    for (const std::string &f : r.folders)
        if (!f.empty() && p.size() > f.size() && p.compare(0, f.size(), f) == 0) below = true;
    if (!below) return Role::None;
    const std::string n = baseName(p);
    if (!r.serviceName.empty() && n == r.serviceName) return Role::Service;
    if (listed(r.closeNames, n)) return Role::Close;
    if (listed(r.stopNames, n)) return Role::Stop;
    return Role::None;
}

struct Found {
    DriverProcess p;
    Role role = Role::None;
};

inline bool scan(DriverRestartOps &ops, const DriverRestartRules &r, std::vector<Found> *out, std::string *err) {
    std::vector<DriverProcess> all;
    out->clear();
    if (!ops.list(&all, err)) return false;
    for (const DriverProcess &p : all) {
        const Role role = roleOf(r, p.exe);
        if (role != Role::None) out->push_back({p, role});
    }
    return true;
}

inline std::string shortName(const std::string &exe) {  // the file name in its original spelling, for messages
    const size_t s = exe.find_last_of("/\\");
    return s == std::string::npos ? exe : exe.substr(s + 1);
}

}  // namespace driver_restart_detail

// The rules for this system. programFiles / programFilesX86 are the folders of %ProgramFiles% and
// %ProgramFiles(x86)% (Windows), home is the user's home folder (macOS); empty entries are left out.
// The names are the ones seen on a Windows machine with 3DxWare 10.9 (3DxService.exe -quiet is the entry
// "3DxWare Service" in the Run key). The macOS names are from the vendor's support pages (the driver
// daemon is 3DconnexionHelper) and have not been run against a Mac.
inline DriverRestartRules driverRestartRules(DriverOs os, const std::string &programFiles, const std::string &programFilesX86,
                                             const std::string &home) {
    using namespace driver_restart_detail;
    DriverRestartRules r;
    if (os == DriverOs::Windows) {
        for (const std::string &pf : {programFiles, programFilesX86})
            if (!folderOf(pf).empty()) r.folders.push_back(folderOf(pf) + "3dconnexion/");
        r.closeNames = {"3dxsmartui.exe", "3dxhome.exe"};
        // 3DxProfileServer.exe is left alone on purpose: nothing starts it again after it was ended (measured), and the
        // service, which owns the settings files, is enough to read them anew.
        r.serviceName = "3dxservice.exe";
        r.serviceArgs = {"-quiet"};
    } else if (os == DriverOs::MacOs) {
        r.folders = {"/library/application support/3dconnexion/", "/library/3dconnexion/", "/applications/", "/library/systemextensions/"};
        if (!folderOf(home).empty()) r.folders.push_back(folderOf(home) + "library/");
        r.serviceName = "3dconnexionhelper";
    }
    return r;
}

// Restarts the driver. Always fills *rep; never throws. See the header comment for the steps.
inline void restartDriver(DriverRestartOps &ops, const DriverRestartRules &rules, DriverRestartReport *rep) {
    using namespace driver_restart_detail;
    *rep = DriverRestartReport();
    if (rules.serviceName.empty() || rules.folders.empty()) {
        rep->supported = false;
        return;
    }
    std::vector<Found> found;
    std::string err;
    if (!scan(ops, rules, &found, &err)) {
        rep->failed.push_back("The list of running programs cannot be read" + (err.empty() ? std::string() : ": " + err));
        return;
    }
    std::string serviceExe;
    for (const Found &f : found)
        if (f.role == Role::Service) {
            rep->serviceFound = true;
            if (serviceExe.empty()) serviceExe = f.p.exe;
        }
    if (!rep->serviceFound) {
        rep->failed.push_back("The 3Dconnexion driver is not running, so there is nothing to restart");
        return;
    }
    // 1. the windows
    bool asked = false;
    for (const Found &f : found)
        if (f.role == Role::Close) {
            ops.askToClose(f.p.pid);
            rep->closed.push_back(shortName(f.p.exe));
            asked = true;
        }
    if (asked) ops.wait(1500);
    // 2. end the programs (the closed windows' programs too, if they are still there)
    if (!scan(ops, rules, &found, &err)) found.clear();
    std::vector<int> endedServicePids;
    bool serviceEndFailed = false;
    for (const Found &f : found) {
        std::string e;
        if (ops.end(f.p.pid, &e)) {
            rep->ended.push_back(shortName(f.p.exe));
            if (f.role == Role::Service) endedServicePids.push_back(f.p.pid);
        } else if (f.role == Role::Service) {
            serviceEndFailed = true;
            rep->failed.push_back(shortName(f.p.exe) + ": " + (e.empty() ? std::string("cannot be ended") : e));
        } else {
            rep->left.push_back(shortName(f.p.exe) + ": " + (e.empty() ? std::string("cannot be ended") : e));
        }
    }
    if (serviceEndFailed) return;  // never start a second copy beside one that still runs
    // let the system finish ending them, and see whether something starts the service again by itself
    std::vector<Found> now;
    auto serviceRunsAgain = [&]() {
        if (!scan(ops, rules, &now, &err)) return false;
        for (const Found &f : now)
            if (f.role == Role::Service) {
                bool old = false;
                for (int p : endedServicePids) old = old || p == f.p.pid;
                if (!old) return true;
            }
        return false;
    };
    for (int i = 0; i < 4; ++i) {
        ops.wait(500);
        if (serviceRunsAgain()) {
            rep->startedAgain = true;
            return;
        }
    }
    // 3. start it
    std::string e;
    if (!ops.start(serviceExe, rules.serviceArgs, &e)) {
        rep->failed.push_back("The driver could not be started" + (e.empty() ? std::string() : ": " + e));
        return;
    }
    rep->startedByUs = true;
    // 4. wait until it runs
    for (int i = 0; i < 16; ++i) {
        ops.wait(500);
        if (serviceRunsAgain()) {
            rep->startedAgain = true;
            return;
        }
    }
    rep->failed.push_back("The driver was started but does not show up in the list of running programs");
}

}  // namespace spn::ui
