// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure helpers of the Windows daemon (src/platform/win/win_paths.hpp); runs on every OS.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstring>

#include "platform/win/win_paths.hpp"

using namespace spn;

int main() {
    // directories
    {
        WinPaths p = windowsPaths({{"APPDATA", "C:\\Users\\x\\AppData\\Roaming"},
                                   {"LOCALAPPDATA", "C:\\Users\\x\\AppData\\Local"},
                                   {"USERPROFILE", "C:\\Users\\x"}});
        assert(p.configDir == "C:/Users/x/AppData/Roaming/dofscroll");
        assert(p.runtimeDir == "C:/Users/x/AppData/Local/dofscroll");
        p = windowsPaths({{"USERPROFILE", "C:\\Users\\y\\"}});
        assert(p.configDir == "C:/Users/y/AppData/Roaming/dofscroll");
        assert(p.runtimeDir == "C:/Users/y/AppData/Local/dofscroll");
        p = windowsPaths({{"APPDATA", "relative"}, {"LOCALAPPDATA", "also\\relative"}});
        assert(p.configDir == "./AppData/Roaming/dofscroll");
        assert(p.runtimeDir.empty());  // no safe per-user directory -> no status/log files
        p = windowsPaths({});
        assert(!p.configDir.empty() && p.runtimeDir.empty());
        p = windowsPaths({{"APPDATA", "D:\\Profiles\\\xc3\x9c\\Roaming"}, {"LOCALAPPDATA", "D:\\Profiles\\\xc3\x9c\\Local"}});
        assert(p.configDir == "D:/Profiles/\xc3\x9c/Roaming/dofscroll");  // UTF-8 passes through
    }
    // executable names
    assert(exeNameLower("C:\\Program Files\\Mozilla Firefox\\Firefox.EXE") == "firefox.exe");
    assert(exeNameLower("C:/Windows/System32/notepad.exe") == "notepad.exe");
    assert(exeNameLower("calc.exe") == "calc.exe" && exeNameLower("").empty());
    assert(exeNameLower("C:\\dir\\").empty());
    assert(exeNameLower("D:\\x\\\xc3\x84PP.EXE") == "\xc3\x84pp.exe");  // non-ASCII bytes untouched
    assert(isUwpHostExe(exeNameLower("C:\\Windows\\System32\\ApplicationFrameHost.exe")));
    assert(!isUwpHostExe("firefox.exe"));

    // object names are in the session namespace and distinct
    assert(std::strncmp(reloadEventName(), "Local\\", 6) == 0);
    assert(std::strncmp(singleInstanceMutexName(), "Local\\", 6) == 0);
    assert(std::strcmp(reloadEventName(), singleInstanceMutexName()) != 0);
    assert(std::strncmp(quitEventName(), "Local\\", 6) == 0);
    assert(std::strcmp(quitEventName(), reloadEventName()) != 0);

    // descriptor search path
    {
        auto c = descriptorDirCandidates("C:/Apps/dofscroll", "D:\\my\\devices");
        assert(c.size() == 3 && c[0] == "D:/my/devices" && c[1] == "C:/Apps/dofscroll/devices");
        assert(c[2] == "C:/Apps/dofscroll/../share/dofscroll/devices");
        c = descriptorDirCandidates("C:/Apps/dofscroll", "");
        assert(c.size() == 2 && c[0] == "C:/Apps/dofscroll/devices");
        assert(descriptorDirCandidates("", "").empty());
    }

    // timestamps per line, across chunk boundaries
    {
        std::string out;
        bool start = true;
        stampLines("a\nb", &start, "T ", &out);
        assert(out == "T a\nT b" && !start);
        stampLines("c\n", &start, "T ", &out);
        assert(out == "T a\nT bc\n" && start);
        stampLines("\n", &start, "T ", &out);  // empty line also gets a stamp
        assert(out == "T a\nT bc\nT \n");
    }

    // rolling log
    {
        std::deque<std::string> l = splitLines("one\ntwo\nthree\npartial");
        assert(l.size() == 3 && l[2] == "three");
        assert(!trimLogLines(&l, 3, 2, 100));  // at the limit: untouched
        l.push_back("four");
        assert(trimLogLines(&l, 3, 2, 100) && l.size() == 2 && l[0] == "three" && l[1] == "four");
        assert(joinLines(l) == "three\nfour\n");
        // byte cap alone triggers a rewrite and long lines are dropped from the front
        std::deque<std::string> big{std::string(kLogMaxBytes, 'x'), "tail"};
        assert(trimLogLines(&big, 300, 200, kLogMaxBytes + 10) && big.size() == 1 && big[0] == "tail");
        std::deque<std::string> none;
        assert(!trimLogLines(&none, 3, 2, 0));
    }

    // status file
    {
        StatusThrottle t(1000);
        assert(t.due("a=1\n", 0));
        assert(!t.due("a=1\n", 500));  // unchanged, within refresh
        assert(t.due("a=2\n", 600));   // changed
        assert(!t.due("a=2\n", 1599));
        assert(t.due("a=2\n", 1600));  // refresh passed
        assert(statusFileText("p=1\n", 42) == "p=1\nupdated=42\n");
        assert(statusFileText(std::string(kStatusMaxBytes, 'x'), 1).empty());
    }

    std::puts("test_win_paths: ok");
    return 0;
}
