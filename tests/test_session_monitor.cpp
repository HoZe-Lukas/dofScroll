// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Session monitor: pure parsing/decision layer for all three OSes plus the logind file monitor
// against a temp directory standing in for /run/systemd/sessions.
#undef NDEBUG
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "platform/session_monitor.hpp"
#include "testutil.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

static void put(const std::string &path, const std::string &text) {
    std::ofstream f(path, std::ios::trunc | std::ios::binary);
    f << text;
}

static void testParse() {
    CHECK(parseLogindSession("ACTIVE=1\nSTATE=active\n") == SessionState::Active, "active");
    CHECK(parseLogindSession("ACTIVE=0\nSTATE=online\n") == SessionState::Inactive, "online/0");
    CHECK(parseLogindSession("STATE=active\n") == SessionState::Active, "state only active");
    CHECK(parseLogindSession("STATE=online\n") == SessionState::Inactive, "state only online");
    CHECK(parseLogindSession("STATE=closing\n") == SessionState::Inactive, "state only closing");
    CHECK(parseLogindSession("ACTIVE=0\nSTATE=active\n") == SessionState::Inactive, "ACTIVE wins");
    CHECK(parseLogindSession("ACTIVE=1\nSTATE=online\n") == SessionState::Active, "ACTIVE wins 2");
    CHECK(parseLogindSession("ACTIVE=1") == SessionState::Active, "no trailing newline");
    CHECK(parseLogindSession("ACTIVE=1\r\n") == SessionState::Unknown, "CR is not a valid value");
    CHECK(parseLogindSession("") == SessionState::Unknown, "empty");
    CHECK(parseLogindSession("ACTIVE=maybe\nSTATE=weird\n") == SessionState::Unknown, "garbage");
    CHECK(parseLogindSession("ACTIVE=maybe\nSTATE=online\n") == SessionState::Inactive, "bad ACTIVE falls back");
    CHECK(parseLogindSession("XACTIVE=0\n  ACTIVE=0\n") == SessionState::Unknown, "keys are anchored");
}

static void testIds() {
    CHECK(validLogindSessionId("3"), "3");
    CHECK(validLogindSessionId("c12"), "c12");
    CHECK(!validLogindSessionId(""), "empty");
    CHECK(!validLogindSessionId("../etc/passwd"), "traversal");
    CHECK(!validLogindSessionId("a/b"), "slash");
    CHECK(!validLogindSessionId(".."), "dots");
    CHECK(!validLogindSessionId(std::string(33, '1')), "too long");
}

static void testWindows() {
    const int64_t NONE = 0xFFFFFFFFLL;
    CHECK(decideWindowsSession(2, 2, 0) == SessionState::Active, "console + active");
    CHECK(decideWindowsSession(2, 2, 4) == SessionState::Active, "console wins over state");
    CHECK(decideWindowsSession(3, 2, 0) == SessionState::Active, "remote session, WTSActive");
    CHECK(decideWindowsSession(3, 2, 4) == SessionState::Inactive, "disconnected, not console");
    CHECK(decideWindowsSession(3, 2, 1) == SessionState::Inactive, "connected but not active");
    CHECK(decideWindowsSession(3, NONE, 4) == SessionState::Inactive, "no console, disconnected");
    CHECK(decideWindowsSession(3, 2, -1) == SessionState::Unknown, "state query failed");
    CHECK(decideWindowsSession(-1, 2, 0) == SessionState::Unknown, "own id unknown");
}

static void testMac() {
    CHECK(decideMacSession(true, true, true) == SessionState::Active, "on console");
    CHECK(decideMacSession(true, true, false) == SessionState::Inactive, "fast user switched away");
    CHECK(decideMacSession(true, false, false) == SessionState::Unknown, "no key");
    CHECK(decideMacSession(false, false, false) == SessionState::Unknown, "no dictionary");
}

static void testFileMonitor() {
    const std::string dir = tu::makeTempDir("dofscroll-session-test");
    std::string detail;

    put(dir + "/7", "# comment\nUSER=1000\nACTIVE=1\nSTATE=active\nSEAT=seat0\n");
    put(dir + "/8", "ACTIVE=0\nSTATE=online\n");
    put(dir + "/9", "STATE=closing\n");
    put(dir + "/10", "nonsense\n");

    CHECK(LogindFileMonitor(dir, "7").isActive(&detail), "7 active");
    CHECK(!LogindFileMonitor(dir, "8").isActive(&detail), "8 inactive");
    CHECK(!detail.empty(), "detail set");
    CHECK(!LogindFileMonitor(dir, "9").isActive(nullptr), "9 closing, null detail is fine");
    CHECK(LogindFileMonitor(dir, "10").isActive(&detail), "unknown content = active");
    CHECK(LogindFileMonitor(dir, "99").isActive(&detail), "missing file = active");
    CHECK(LogindFileMonitor(dir, "").isActive(&detail), "no id = active");
    CHECK(LogindFileMonitor(dir, "../x").isActive(&detail), "invalid id = active");
    CHECK(LogindFileMonitor(dir + "/nonexistent", "7").isActive(&detail), "missing dir = active");

    // Changes are seen on the next poll (no caching).
    LogindFileMonitor m(dir, "8");
    CHECK(!m.isActive(nullptr), "before");
    put(dir + "/8", "ACTIVE=1\nSTATE=active\n");
    CHECK(m.isActive(nullptr), "after switching back");

    // A symlink is refused and therefore unknown = active (inactive target must not count).
    std::error_code ec;
    std::filesystem::create_symlink(dir + "/8x", dir + "/link", ec);
    put(dir + "/8x", "ACTIVE=0\n");
    if (!ec) CHECK(LogindFileMonitor(dir, "link").isActive(&detail), "symlink refused = active");

    // A directory in place of the file: unreadable = active.
    std::filesystem::create_directory(dir + "/11");
    CHECK(LogindFileMonitor(dir, "11").isActive(&detail), "directory = active");

    tu::removeAll(dir);
}

int main() {
    testParse();
    testIds();
    testWindows();
    testMac();
    testFileMonitor();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("session monitor: ok");
    return 0;
}
