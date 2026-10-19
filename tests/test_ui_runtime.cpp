// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for the UI's runtime plumbing: runtime directory per OS, the status file path shared
// by daemon and UI, and reading it into the StatusModel (src/ui/model/runtime_files.hpp).
#include <cassert>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>

#include "common.hpp"
#include "platform/win/win_paths.hpp"
#include "testutil.hpp"
#include "ui/model/runtime_files.hpp"
#include "ui/model/status_model.hpp"

using namespace spn;
using namespace spn::ui;

static void testRuntimeDirPerOs() {
    // Windows: %LOCALAPPDATA%\dofscroll, never the roaming %APPDATA%
    std::map<std::string, std::string> env = {
        {"APPDATA", "C:\\Users\\a\\AppData\\Roaming"}, {"LOCALAPPDATA", "C:\\Users\\a\\AppData\\Local"}, {"USERPROFILE", "C:\\Users\\a"}};
    const WinPaths w = windowsPaths(env);
    assert(!w.runtimeDir.empty());
    assert(w.runtimeDir.find("Local") != std::string::npos && w.runtimeDir.find("Roaming") == std::string::npos);
    assert(w.runtimeDir.size() > 9 && w.runtimeDir.substr(w.runtimeDir.size() - 9) == "dofscroll");
    // (macOS ~/Library/Caches/dofscroll is covered by test_mac_paths)
    const std::string macRt = "/Users/a/Library/Caches/dofscroll";
    // Both sides build the file names from the same directory
    assert(statusFilePathIn(macRt) == macRt + "/dofscroll-status");
    assert(logFilePathIn(w.runtimeDir) == w.runtimeDir + "/dofscroll.log");
    assert(suspendFilePathIn(w.runtimeDir) == w.runtimeDir + "/dofscroll-suspend");
    assert(statusFilePathIn("").empty() && logFilePathIn("").empty() && suspendFilePathIn("").empty());
    std::puts("ok runtime dir per OS");
}

static void testStatusRead() {
    const std::string dir = tu::makeTempDir("dofscroll-ui-rt");
    const std::string rt = dir + "/run";  // does not exist yet: the daemon creates it
    std::string text;
    assert(!readStatusFile(rt, &text));  // nothing there: not running
    assert(!readStatusFile("", &text));  // no runtime directory

    // What the daemon writes (every OS) is what the UI reads, and the model sees "running"
    const long long now = static_cast<long long>(std::time(nullptr));
    std::string err;
    assert(fs::writeFileReplace(statusFilePathIn(rt), statusFileText("profile=global\nmode=normal\nenabled=1\n", now), &err));
    assert(readStatusFile(rt, &text));
    StatusModel st;
    st.update(text, true, now);
    assert(st.state() == StatusModel::State::Running);
    st.update(text, true, now + 3600);  // a daemon that stopped writing is stale
    assert(st.state() != StatusModel::State::Running);
    st.update("", readStatusFile(dir + "/other", &text), now);
    assert(st.state() == StatusModel::State::NoStatus);

    // oversized files are refused (bounded read)
    assert(fs::writeFileReplace(statusFilePathIn(rt), std::string(kStatusMaxBytes + 1, 'x'), &err));
    assert(!readStatusFile(rt, &text));
    tu::removeAll(dir);
    std::puts("ok status read");
}

#ifndef _WIN32
static void testStatusSymlinkRefused() {
    const std::string dir = tu::makeTempDir("dofscroll-ui-rt");
    std::string err;
    assert(fs::writeFileReplace(dir + "/real", "profile=x\n", &err));
    assert(::symlink((dir + "/real").c_str(), statusFilePathIn(dir).c_str()) == 0);
    std::string text;
    assert(!readStatusFile(dir, &text));
    tu::removeAll(dir);
    std::puts("ok status symlink refused");
}
#endif

int main() {
    testRuntimeDirPerOs();
    testStatusRead();
#ifndef _WIN32
    testStatusSymlinkRefused();
#endif
    std::puts("test_ui_runtime: all ok");
    return 0;
}
