// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for the UI's runtime plumbing: runtime directory per OS, the status file path shared
// by daemon and UI, and reading it into the StatusModel (src/ui/model/runtime_files.hpp).
#include <cassert>
#include <cstdio>
#include <ctime>
#include <map>
#include <vector>
#include <string>

#include <chrono>
#include <filesystem>

#include "common.hpp"
#include "mode_request.hpp"
#include "platform/runtime_path.hpp"
#include "platform/suspend_probe.hpp"
#include "platform/win/win_paths.hpp"
#include "testutil.hpp"
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "ui/model/action_runner.hpp"
#include "ui/model/release_keys.hpp"
#include "ui/model/runtime_files.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/suspend_heartbeat.hpp"
#include "ui/model/tray_menu_model.hpp"
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

static void testReleaseKeysCommand() {
    const std::string name = daemonExecutableName();
    auto never = [](const std::string &) { return false; };
    // PATH lookup when the daemon is not next to the UI (argv, no shell)
    std::vector<std::string> a = releaseKeysCommand("/opt/dofscroll/bin", never);
    assert(a.size() == 2 && a[0] == name && a[1] == "--release-keys");
    assert(releaseKeysCommand("", never)[0] == name);
    // next to the UI executable when it exists (same build or install)
    std::string asked;
    a = releaseKeysCommand("/opt/dofscroll/bin", [&](const std::string &p) { asked = p; return true; });
    assert(asked == "/opt/dofscroll/bin/" + name && a[0] == asked && a[1] == "--release-keys");
    assert(releaseKeysCommand("C:/Program Files/dofscroll/", [](const std::string &) { return true; })[0] ==
           "C:/Program Files/dofscroll/" + name);
    // the tray entry reaches the runner exactly once per click
    RecordingActionRunner runner;
    TrayEntry e;
    e.action = TrayAction::ReleaseAllKeys;
    std::string err;
    assert(performTrayAction(e, runner, TrayHooks(), &err) && runner.count(RecordingActionRunner::Kind::ReleaseKeys) == 1);
    runner.result = false;
    runner.error = "cannot start";
    assert(!performTrayAction(e, runner, TrayHooks(), &err) && err == "cannot start");
    std::puts("ok release keys command");
}

static std::string slurp(const std::string &path) {
    std::string t;
    fs::readFileBounded(path, 1 << 20, fs::ReadOptions(), &t);
    return t;
}

// The runtime mode request: what a request can and cannot do (src/mode_request.hpp).
static void testModeRequestSecurity() {
    const std::string dir = tu::makeTempDir("dofscroll-ui-modereq");
    const std::string rt = dir + "/run";  // created by the writer, private
    const std::string path = modeRequestPath(rt);
    std::string err;

    // round trip: user-only file, one line, atomic (no temp leftovers), consumed by the reader
    assert(writeModeRequest(rt, "alpha", &err));
    assert(slurp(path) == "alpha\n");
    {
        std::vector<std::string> names;
        bool capped = false;
        assert(fs::listDir(rt, 0, &names, &capped));
        for (const std::string &n : names) assert(n == "." || n == ".." || n == "dofscroll-mode-request");
    }
#ifndef _WIN32
    struct stat st;
    assert(::lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 077) == 0);
#endif
    assert(writeModeRequest(rt, "beta", &err) && slurp(path) == "beta\n");  // replaced as a whole
    assert(takeModeRequest(rt) == "beta" && !tu::exists(path));
    assert(takeModeRequest(rt).empty());  // consumed exactly once

    // the writer refuses what the reader would never accept
    const std::string tooLong(33, 'a');
    for (const std::string &bad : {std::string(), std::string("ALPHA"), std::string("../x"), std::string("a/b"),
                                   std::string("a b"), std::string("a\nb"), std::string("a\0b", 3), tooLong,
                                   std::string(100, 'a')}) {
        assert(!writeModeRequest(rt, bad, &err) && !tu::exists(path));
    }
    assert(!writeModeRequest("", "alpha", &err));  // no runtime directory: no request channel
    assert(writeModeRequest(rt, std::string(32, 'a'), &err) && takeModeRequest(rt) == std::string(32, 'a'));

    // hand-made files: invalid or oversized ids are dropped (and removed), never interpreted
    const std::vector<std::string> junk = {
        "alpha\nbeta\n", std::string("al\0pha\n", 8), "ALPHA\n", "../etc/passwd\n", "a b\n", "\n", "",
        tooLong + "\n", std::string(kModeRequestMaxBytes + 1, 'a'), std::string(100000, 'a'),
        "mode:alpha\n", "$(touch x)\n", "`id`\n"};
    for (const std::string &c : junk) {
        assert(fs::createFileExclusive(path, c));
        assert(takeModeRequest(rt).empty() && !tu::exists(path));
    }
    // CRLF and a missing newline are tolerated, nothing else
    assert(fs::createFileExclusive(path, "alpha\r\n") && takeModeRequest(rt) == "alpha");
    assert(fs::createFileExclusive(path, "alpha") && takeModeRequest(rt) == "alpha");

    // stale: a request that was never answered (reload did not arrive) must not fire later
    assert(writeModeRequest(rt, "alpha", &err));
    const int64_t now = systemNowNs(), sec = 1000000000LL;
    assert(takeModeRequest(rt, now + (kModeRequestMaxAgeSeconds + 5) * sec).empty() && !tu::exists(path));
    assert(writeModeRequest(rt, "alpha", &err));
    assert(takeModeRequest(rt, now - (kModeRequestMaxAgeSeconds + 5) * sec).empty());  // far in the future
    assert(writeModeRequest(rt, "alpha", &err));
    assert(takeModeRequest(rt, systemNowNs() + 2 * sec) == "alpha");  // a fresh one passes
    assert(writeModeRequest(rt, "alpha", &err));
    clearModeRequest(rt);  // daemon start
    assert(!tu::exists(path));

#ifndef _WIN32
    // group/other-accessible file: refused
    assert(fs::createFileExclusive(path, "alpha\n"));
    ::chmod(path.c_str(), 0644);
    assert(takeModeRequest(rt).empty() && !tu::exists(path));

    // a link is never followed: neither when reading nor when writing
    assert(fs::createFileExclusive(dir + "/secret", "alpha\n"));
    assert(::symlink((dir + "/secret").c_str(), path.c_str()) == 0);
    assert(takeModeRequest(rt).empty() && !tu::exists(path) && slurp(dir + "/secret") == "alpha\n");
    assert(::symlink((dir + "/secret").c_str(), path.c_str()) == 0);
    assert(writeModeRequest(rt, "beta", &err));
    assert(slurp(dir + "/secret") == "alpha\n");  // the target was not overwritten
    assert(::lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode));
    assert(takeModeRequest(rt) == "beta");

    // a FIFO or a directory at the name neither blocks nor is accepted
    assert(::mkfifo(path.c_str(), 0600) == 0);
    assert(takeModeRequest(rt).empty());
    assert(::mkdir(path.c_str(), 0700) == 0);
    assert(takeModeRequest(rt).empty());
    assert(!writeModeRequest(rt, "alpha", &err));
    ::rmdir(path.c_str());
#endif
    tu::removeAll(dir);
    std::puts("ok mode request security");
}

static void testCheckedRuntimeFile() {
    assert(checkedRuntimeFile("/run/user/1000", RuntimeFile::LogFile) == "/run/user/1000/dofscroll.log");
    assert(checkedRuntimeFile("/run/user/1000", RuntimeFile::StatusFile) == "/run/user/1000/dofscroll-status");
    assert(checkedRuntimeFile("/run/user/1000", RuntimeFile::SuspendFile) == "/run/user/1000/dofscroll-suspend");
    for (const char *bad : {"", "/", "run/user", "./run", "/run/../etc", "/run/./x", "/run//x", "/run/x/", "/..",
                            "/run/x/.."})
        assert(checkedRuntimeFile(bad, RuntimeFile::LogFile).empty());
    assert(checkedRuntimeFile(std::string("/run/a\0b", 8), RuntimeFile::LogFile).empty());
    assert(checkedRuntimeFile("/run/a\nb", RuntimeFile::LogFile).empty());
    assert(checkedRuntimeFile("/" + std::string(2000, 'a'), RuntimeFile::LogFile).empty());
    // the file component never contains a separator or a dot-dot
    for (RuntimeFile f : {RuntimeFile::LogFile, RuntimeFile::StatusFile, RuntimeFile::SuspendFile}) {
        const std::string n = runtimeFileName(f);
        assert(n.find('/') == std::string::npos && n.find("..") == std::string::npos && n.rfind("dofscroll", 0) == 0);
    }
    std::puts("ok checked runtime file");
}

int main() {
    testCheckedRuntimeFile();
    testModeRequestSecurity();
    testReleaseKeysCommand();
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
