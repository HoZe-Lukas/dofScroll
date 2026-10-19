// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tests for the pure helpers of platform/running_apps_util.hpp. The X11 walk itself is not part
// of this test (needs a display); see tools/ or run the daemon's UI manually.
#include <cassert>
#include <cstdio>

#include "platform/running_apps_util.hpp"

using namespace spn;

static RunningApp w(const char *m, const char *title, int pid) {
    RunningApp a;
    a.matchString = m;
    a.windowTitle = title;
    a.pid = pid;
    return a;
}

static void testClass() {
    assert(joinWmClass("Navigator", "Firefox") == "navigator firefox");
    assert(joinWmClass("", "Code") == "code");
    assert(joinWmClass("xterm", "") == "xterm");
    assert(joinWmClass(nullptr, nullptr).empty());
    assert(joinWmClass(std::string(300, 'A').c_str(), nullptr).size() == kMaxWmClassLen);
}

static void testOwn() {
    assert(isOwnAppString("dofscroll-ui dofscroll-ui"));
    assert(isOwnAppString("navigator dofscroll"));
    assert(!isOwnAppString("navigator firefox"));
    assert(!isOwnAppString("xdofscroll"));  // token must start with the name
    assert(!isOwnAppString(""));
}

static void testDedupe() {
    auto r = dedupeApps({w("a a", "t1", 10), w("b b", "u1", 20), w("a a", "t2", 11), w("", "x", 1),
                         w("dofscroll dofscroll", "me", 2), w("a a", "t3", 0)});
    assert(r.size() == 2);
    assert(r[0].matchString == "a a" && r[0].windowCount == 3);
    assert(r[0].windowTitle == "t3" && r[0].pid == 11);  // latest title, last known pid
    assert(r[1].matchString == "b b" && r[1].windowCount == 1);
    assert(dedupeApps({}).empty());
}

int main() {
    testClass();
    testOwn();
    testDedupe();
    std::puts("test_running_apps_x11: all tests passed");
    return 0;
}
