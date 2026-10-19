// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for the UI's runtime plumbing: runtime directory per OS, the status file path shared
// by daemon and UI, and reading it into the StatusModel (src/ui/model/runtime_files.hpp).
#include <cassert>
#include <cstdio>
#include <ctime>
#include <map>
#include <string>

#include <chrono>
#include <filesystem>

#include "common.hpp"
#include "platform/suspend_probe.hpp"
#include "platform/win/win_paths.hpp"
#include "testutil.hpp"
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "ui/model/action_runner.hpp"
#include "ui/model/runtime_files.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/suspend_heartbeat.hpp"
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

// Every page that saves goes through SettingsModel::save(); with the reload callback of the UI
// context the daemon is notified exactly once per successful save, after the file is written.
static void testReloadAfterSave() {
    using Kind = RecordingActionRunner::Kind;
    IdentityStrings id;
    const std::string dir = tu::makeTempDir("dofscroll-ui-reload");
    const std::string path = dir + "/dofscroll.conf";
    RecordingActionRunner runner;
    SettingsModel m;
    std::string err;
    std::vector<std::string> warn;
    assert(m.load(path, &warn, &err));
    m.setReloadCallback(reloadVia(runner));
    assert(m.setValue(0, "ENABLED", "0", &err));
    SettingsModel::SaveResult r = m.save(id);
    assert(r.saved && r.reloaded && runner.count(Kind::RequestReload) == 1);
    assert(tu::exists(path));  // the request comes after the atomic write

    // a failing channel: the file stays saved, the page gets the reason, nothing is retried
    runner.result = false;
    runner.error = "dofscroll is not running";
    assert(m.setValue(0, "ENABLED", "1", &err));
    r = m.save(id);
    assert(r.saved && !r.reloaded && r.error == "dofscroll is not running");
    assert(runner.count(Kind::RequestReload) == 2);

    // nothing is written and nobody is notified when validation fails
    ProfileSet bad = m.profiles();
    bad.sections[0].kv.emplace_back("SCROLL_V_SENS", "oops");
    SettingsModel bm;
    bm.setProfiles(bad, dir + "/bad.conf");
    bm.setReloadCallback(reloadVia(runner));
    r = bm.save(id);
    assert(!r.saved && runner.count(Kind::RequestReload) == 2 && !tu::exists(dir + "/bad.conf"));
    tu::removeAll(dir);
    std::puts("ok reload after save");
}

static void ageFile(const std::string &path, int seconds) {
    namespace sf = std::filesystem;
    sf::last_write_time(sf::u8path(path), sf::file_time_type::clock::now() - std::chrono::seconds(seconds));
}

static void testSuspendHeartbeat() {
    const int64_t ms = 1000000;
    const std::string dir = tu::makeTempDir("dofscroll-ui-susp");
    const std::string file = suspendFilePathIn(dir);
    FileSuspendProbe probe(dir);
    assert(!probe());  // no file

    {
        SuspendHeartbeat hb(dir);
        assert(hb.available() && !hb.active() && !tu::exists(file));
        hb.update(false, 0);  // nothing wanted: nothing created
        assert(!tu::exists(file));
        hb.update(true, 1000 * ms);
        assert(hb.active() && hb.fileMade() && tu::exists(file) && probe());
        // refreshed only when due (every 200 ms): a file removed behind its back stays away until then
        assert(fs::removeFile(file));
        hb.update(true, 1100 * ms);
        assert(!tu::exists(file));
        hb.update(true, 1200 * ms);
        assert(tu::exists(file));
        // an older timestamp (clock jump back) refreshes at once
        assert(fs::removeFile(file));
        hb.update(true, 500 * ms);
        assert(tu::exists(file));
        // the end of the condition removes the file
        hb.update(false, 600 * ms);
        assert(!hb.active() && !tu::exists(file) && !probe());
        hb.update(true, 700 * ms);
        assert(tu::exists(file));
    }  // destructor: the file is gone (page destroyed, application quit)
    assert(!tu::exists(file));

    // explicit stop is idempotent
    SuspendHeartbeat hb2(dir);
    hb2.update(true, 0);
    hb2.stop();
    hb2.stop();
    assert(!tu::exists(file));

    // no runtime directory: no file anywhere, no crash
    SuspendHeartbeat none("");
    none.update(true, 0);
    assert(!none.available() && !none.fileMade());

    // the daemon ignores a stale heartbeat (UI crashed) and accepts a fresh one
    hb2.update(true, 0);
    assert(probe());
    ageFile(file, 5);
    assert(!probe());
    hb2.update(true, 10 * 1000 * ms);
    assert(probe());

    // pure decision around SUSPEND_MAX_AGE_MS
    const int64_t now = 10000 * ms, maxAge = static_cast<int64_t>(SUSPEND_MAX_AGE_MS) * ms;
    assert(suspendHeartbeatFresh(now, now) && suspendHeartbeatFresh(now - maxAge, now));
    assert(!suspendHeartbeatFresh(now - maxAge - ms, now) && !suspendHeartbeatFresh(now + maxAge + ms, now));
    assert(SuspendHeartbeat::kIntervalNs < maxAge);
    hb2.stop();
    tu::removeAll(dir);
    std::puts("ok suspend heartbeat");
}

#ifndef _WIN32
static void testSuspendHeartbeatSecurity() {
    const std::string dir = tu::makeTempDir("dofscroll-ui-susp");
    const std::string file = suspendFilePathIn(dir);
    FileSuspendProbe probe(dir);
    // created user-only
    assert(fs::touchFilePrivate(file));
    struct stat st;
    assert(::lstat(file.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 077) == 0);
    fs::removeFile(file);

    // a link at the name is never followed: the target keeps its time, nothing is made or removed
    {
        std::string err;
        assert(fs::writeFileReplace(dir + "/target", "keep\n", &err));
    }
    ageFile(dir + "/target", 100);
    struct stat before;
    assert(::stat((dir + "/target").c_str(), &before) == 0);
    assert(::symlink((dir + "/target").c_str(), file.c_str()) == 0);
    assert(!fs::touchFilePrivate(file));
    SuspendHeartbeat hb(dir);
    hb.update(true, 0);
    assert(hb.active() && !hb.fileMade());
    struct stat after;
    assert(::stat((dir + "/target").c_str(), &after) == 0 && before.st_mtime == after.st_mtime);
    assert(!probe());  // and the daemon does not follow it either
    hb.stop();
    assert(tu::exists(file));  // not ours: left alone
    fs::removeFile(file);

    // a group/other-accessible file or a directory is not a heartbeat
    {
        std::string err;
        assert(fs::writeFileReplace(file, "", &err));
        ::chmod(file.c_str(), 0644);
        assert(!fs::touchFilePrivate(file));
        fs::removeFile(file);
        assert(::mkdir(file.c_str(), 0700) == 0);
        assert(!fs::touchFilePrivate(file) && !probe());
        ::rmdir(file.c_str());
    }
    tu::removeAll(dir);
    std::puts("ok suspend heartbeat security");
}
#endif

int main() {
    testSuspendHeartbeat();
#ifndef _WIN32
    testSuspendHeartbeatSecurity();
#endif
    testReloadAfterSave();
    testRuntimeDirPerOs();
    testStatusRead();
#ifndef _WIN32
    testStatusSymlinkRefused();
#endif
    std::puts("test_ui_runtime: all ok");
    return 0;
}
