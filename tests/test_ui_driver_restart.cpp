// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Restart of the official 3Dconnexion driver (ui/model/driver_restart.hpp), against a fake process table.
#undef NDEBUG
#include <cassert>
#include <map>
#include <string>
#include <vector>

#include "ui/model/driver_restart.hpp"

using namespace spn::ui;

namespace {

const char *kSvc = "C:\\Program Files\\3Dconnexion\\3DxWare\\3DxWinCore\\3DxService.exe";
const char *kProf = "C:\\Program Files\\3Dconnexion\\3DxWare\\3DxWinCore\\3DxProfileServer.exe";
const char *kUi = "C:\\Program Files\\3Dconnexion\\3DxWare\\3DxWinCore\\3DxSmartUi.exe";

struct Fake : DriverRestartOps {
    std::vector<DriverProcess> procs;
    int nextPid = 1000;
    bool listFails = false, startFails = false, systemRestarts = false;
    std::vector<int> denied;   // pids that cannot be ended
    std::vector<int> asked;
    std::vector<std::string> started;
    std::vector<std::vector<std::string>> startedArgs;
    int waited = 0;
    bool startedProgramAppears = true;

    bool list(std::vector<DriverProcess> *out, std::string *err) override {
        if (listFails) {
            *err = "no access";
            return false;
        }
        *out = procs;
        return true;
    }
    void askToClose(int pid) override {
        asked.push_back(pid);
        for (size_t i = 0; i < procs.size(); ++i)
            if (procs[i].pid == pid) {
                procs.erase(procs.begin() + long(i));  // a window program ends when its window closes
                return;
            }
    }
    bool end(int pid, std::string *err) override {
        for (int d : denied)
            if (d == pid) {
                *err = "access denied";
                return false;
            }
        for (size_t i = 0; i < procs.size(); ++i)
            if (procs[i].pid == pid) {
                const std::string exe = procs[i].exe;
                procs.erase(procs.begin() + long(i));
                if (systemRestarts && exe == kSvc) procs.push_back({nextPid++, exe});
                return true;
            }
        *err = "gone";
        return false;
    }
    bool start(const std::string &exe, const std::vector<std::string> &args, std::string *err) override {
        started.push_back(exe);
        startedArgs.push_back(args);
        if (startFails) {
            *err = "file not found";
            return false;
        }
        if (startedProgramAppears) procs.push_back({nextPid++, exe});
        return true;
    }
    void wait(int ms) override { waited += ms; }
};

DriverRestartRules win() { return driverRestartRules(DriverOs::Windows, "C:\\Program Files", "C:\\Program Files (x86)", ""); }
// the rules with a helper that is ended as well (the mechanism stays; the Windows rules do not use it)
DriverRestartRules winWithHelper() {
    DriverRestartRules r = win();
    r.stopNames = {"3dxprofileserver.exe"};
    return r;
}

Fake running() {
    Fake f;
    f.procs = {{10, kSvc}, {11, kProf}, {12, kProf}, {13, kUi}, {14, "C:\\Windows\\System32\\notepad.exe"}};
    return f;
}

}  // namespace

int main() {
    // ---- rules
    {
        const DriverRestartRules r = win();
        assert(r.folders.size() == 2 && r.folders[0] == "c:/program files/3dconnexion/");
        assert(r.folders[1] == "c:/program files (x86)/3dconnexion/");
        assert(r.serviceName == "3dxservice.exe" && r.serviceArgs.size() == 1 && r.serviceArgs[0] == "-quiet");
        using driver_restart_detail::roleOf;
        using driver_restart_detail::Role;
        assert(roleOf(r, kSvc) == Role::Service);
        assert(roleOf(r, "c:/PROGRAM FILES/3dconnexion/x/3DXSERVICE.EXE") == Role::Service);  // case and separators do not matter
        assert(roleOf(r, kProf) == Role::None);  // the profile server is not touched
        assert(roleOf(winWithHelper(), kProf) == Role::Stop);
        assert(roleOf(r, kUi) == Role::Close);
        // the same name from another folder, a sibling folder with the same start, a path with ".." : never touched
        assert(roleOf(r, "C:\\Users\\x\\Downloads\\3DxService.exe") == Role::None);
        assert(roleOf(r, "C:\\Program Files\\3Dconnexion-evil\\3DxService.exe") == Role::None);
        assert(roleOf(r, "C:\\Program Files\\3Dconnexion\\..\\Evil\\3DxService.exe") == Role::None);
        assert(roleOf(r, "C:\\Program Files\\3Dconnexion\\3DxWare\\notepad.exe") == Role::None);  // a name that is not in the rules
        assert(roleOf(r, "") == Role::None);
        assert(roleOf(r, "C:\\Program Files\\3Dconnexion\\") == Role::None);
        // empty folders are dropped
        assert(driverRestartRules(DriverOs::Windows, "", "", "").folders.empty());
        // no rules, no restart
        Fake f = running();
        DriverRestartReport rep;
        restartDriver(f, driverRestartRules(DriverOs::Other, "", "", ""), &rep);
        assert(!rep.supported && !rep.ok() && f.started.empty());
        // macOS
        const DriverRestartRules m = driverRestartRules(DriverOs::MacOs, "", "", "/Users/me");
        assert(m.serviceName == "3dconnexionhelper");
        assert(roleOf(m, "/Library/Application Support/3Dconnexion/3DconnexionHelper.app/Contents/MacOS/3DconnexionHelper") == Role::Service);
        assert(roleOf(m, "/Users/me/Library/3DconnexionHelper") == Role::Service);
        assert(roleOf(m, "/tmp/3DconnexionHelper") == Role::None);
        assert(roleOf(m, "/Users/other/Library/3DconnexionHelper") == Role::None);
    }

    // ---- a full restart
    {
        Fake f = running();
        DriverRestartReport rep;
        restartDriver(f, win(), &rep);
        assert(rep.ok());
        assert(rep.serviceFound && rep.startedAgain && rep.startedByUs);
        assert(f.asked.size() == 1 && f.asked[0] == 13);                 // only the window program is asked to close
        assert(rep.closed.size() == 1 && rep.closed[0] == "3DxSmartUi.exe");
        assert(rep.ended.size() == 1 && rep.ended[0] == "3DxService.exe");  // only the service
        assert(f.started.size() == 1 && f.started[0] == kSvc);           // started from the path of the program that was ended
        assert(f.startedArgs[0].size() == 1 && f.startedArgs[0][0] == "-quiet");
        assert(rep.failed.empty() && rep.left.empty());
        bool notepad = false, driverLeft = false;
        for (const DriverProcess &p : f.procs) {
            notepad = notepad || p.exe.find("notepad") != std::string::npos;
            driverLeft = driverLeft || p.exe == kUi;
        }
        assert(notepad && !driverLeft);  // unrelated programs stay, the closed window program is gone
        int profile = 0;
        for (const DriverProcess &p : f.procs) profile += p.exe == kProf ? 1 : 0;
        assert(profile == 2);  // both profile servers still run
    }

    // ---- a helper that cannot be ended (administrator rights) is named and left; the restart still counts
    {
        Fake f = running();
        f.denied = {12};
        DriverRestartReport rep;
        restartDriver(f, winWithHelper(), &rep);
        assert(rep.ok());
        assert(rep.left.size() == 1 && rep.left[0] == "3DxProfileServer.exe: access denied");
        assert(rep.ended.size() == 2);  // the service and the other profile server
    }

    // ---- the service itself cannot be ended: nothing is started beside it
    {
        Fake f = running();
        f.denied = {10};
        DriverRestartReport rep;
        restartDriver(f, win(), &rep);
        assert(!rep.ok() && f.started.empty());
        assert(rep.failed.size() == 1 && rep.failed[0] == "3DxService.exe: access denied");
    }

    // ---- the system starts the service again by itself: no second copy
    {
        Fake f = running();
        f.systemRestarts = true;
        DriverRestartReport rep;
        restartDriver(f, win(), &rep);
        assert(rep.ok() && !rep.startedByUs && f.started.empty());
    }

    // ---- the driver is not running
    {
        Fake f;
        f.procs = {{14, "C:\\Windows\\System32\\notepad.exe"}, {15, "C:\\Users\\x\\3DxService.exe"}};
        DriverRestartReport rep;
        restartDriver(f, win(), &rep);
        assert(!rep.ok() && !rep.serviceFound && f.started.empty() && f.asked.empty() && rep.failed.size() == 1);
        assert(f.procs.size() == 2);  // nothing was ended
    }

    // ---- the list cannot be read
    {
        Fake f = running();
        f.listFails = true;
        DriverRestartReport rep;
        restartDriver(f, win(), &rep);
        assert(!rep.ok() && rep.failed.size() == 1 && rep.failed[0].find("no access") != std::string::npos);
        assert(f.started.empty());
    }

    // ---- starting fails
    {
        Fake f = running();
        f.startFails = true;
        DriverRestartReport rep;
        restartDriver(f, win(), &rep);
        assert(!rep.ok() && rep.serviceFound && !rep.startedAgain);
        assert(rep.failed.size() == 1 && rep.failed[0].find("file not found") != std::string::npos);
    }

    // ---- started, but it never shows up
    {
        Fake f = running();
        f.startedProgramAppears = false;
        DriverRestartReport rep;
        restartDriver(f, win(), &rep);
        assert(!rep.ok() && rep.startedByUs && !rep.startedAgain && rep.failed.size() == 1);
        assert(f.waited < 20000);  // bounded
    }

    // ---- macOS: the helper is ended and started again from its own path
    {
        Fake f;
        const std::string helper = "/Library/Application Support/3Dconnexion/3DconnexionHelper.app/Contents/MacOS/3DconnexionHelper";
        f.procs = {{20, helper}, {21, "/usr/bin/top"}};
        DriverRestartReport rep;
        restartDriver(f, driverRestartRules(DriverOs::MacOs, "", "", "/Users/me"), &rep);
        assert(rep.ok() && rep.startedByUs && f.started.size() == 1 && f.started[0] == helper && f.startedArgs[0].empty());
        assert(f.asked.empty());
    }
    return 0;
}
