// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure helpers of the macOS daemon (src/platform/mac/mac_paths.hpp).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/mac/mac_paths.hpp"

using namespace spn;

int main() {
    // directory layout
    MacPaths p = macPaths("/Users/x");
    assert(p.configDir == "/Users/x/Library/Application Support/dofscroll");
    assert(p.runtimeDir == "/Users/x/Library/Caches/dofscroll");
    p = macPaths("/Users/x/");
    assert(p.configDir == "/Users/x/Library/Application Support/dofscroll");
    p = macPaths("");
    assert(p.configDir == "./Library/Application Support/dofscroll" && p.runtimeDir.empty());
    p = macPaths("relative/home");
    assert(!p.configDir.empty() && p.runtimeDir.empty());

    // reload socket: normal case
    ReloadSocket r = reloadSocketPath("/Users/x/Library/Caches/dofscroll", 501, "/tmp");
    assert(r.path == "/Users/x/Library/Caches/dofscroll/reload.sock" && !r.fallback);
    // a path of exactly 103 characters fits, 104 does not (sun_path holds 104 bytes with the NUL)
    std::string rt103(103 - std::string("/reload.sock").size(), 'a');
    rt103[0] = '/';
    r = reloadSocketPath(rt103, 501, "/tmp");
    assert(r.path.size() == 103 && !r.fallback);
    r = reloadSocketPath(rt103 + "a", 501, "/tmp");
    assert(r.fallback && r.path == "/tmp/dofscroll-501/reload.sock");
    // very long home: fallback under tmp, a slash-terminated tmp dir is normalized
    r = reloadSocketPath("/Users/" + std::string(200, 'u') + "/Library/Caches/dofscroll", 7, "/tmp/");
    assert(r.fallback && r.path == "/tmp/dofscroll-7/reload.sock");
    // no runtime dir: fallback; nothing fits: empty path
    r = reloadSocketPath("", 501, "/tmp");
    assert(r.fallback && r.path == "/tmp/dofscroll-501/reload.sock");
    r = reloadSocketPath("", 501, "/" + std::string(120, 't'));
    assert(r.fallback && r.path.empty());
    assert(socketDir("/a/b/reload.sock") == "/a/b" && socketDir("/reload.sock") == "/" && socketDir("x") == ".");

    // descriptor directory search order
    auto c = descriptorDirCandidates("", "/Applications/dofscroll.app/Contents/MacOS");
    assert(c.size() == 2 && c[0] == "/Applications/dofscroll.app/Contents/MacOS/devices");
    assert(c[1] == "/Applications/dofscroll.app/Contents/MacOS/../share/dofscroll/devices");
    c = descriptorDirCandidates("/opt/dev", "/usr/local/bin");
    assert(c.size() == 3 && c[0] == "/opt/dev" && c[1] == "/usr/local/bin/devices");
    assert(descriptorDirCandidates("", "").empty());
    assert(pickDescriptorDir(c, [](const std::string &d) { return d == "/usr/local/bin/devices"; }) ==
           "/usr/local/bin/devices");
    assert(pickDescriptorDir(c, [](const std::string &) { return false; }) == "/opt/dev");  // none exists: first
    assert(pickDescriptorDir({}, [](const std::string &) { return true; }).empty());

    // permissions
    assert(permissionsStatusLine(true, true) == "permissions=ok\n");
    assert(permissionsStatusLine(false, true) == "permissions=missing:accessibility\n");
    assert(permissionsStatusLine(true, false) == "permissions=missing:input_monitoring\n");
    assert(permissionsStatusLine(false, false) == "permissions=missing:accessibility,input_monitoring\n");
    assert(permissionHint(true, true).empty());
    assert(permissionHint(false, true).find("Accessibility") != std::string::npos &&
           permissionHint(false, true).find("Input Monitoring") == std::string::npos);
    assert(permissionHint(false, false).find("Input Monitoring") != std::string::npos);

    // log trimming
    assert(countLines("a\nb\nc") == 2 && countLines("") == 0);
    const std::string t = "l1\nl2\nl3\nl4\npartial";
    assert(trimLogText(t, 2, 1000) == "l3\nl4\n");
    assert(trimLogText(t, 10, 1000) == "l1\nl2\nl3\nl4\n");
    assert(trimLogText(t, 10, 6) == "l3\nl4\n");  // the byte cap cuts at a line start
    assert(trimLogText(t, 10, 2).empty());        // even one line too long: nothing
    assert(trimLogText("", 5, 5).empty() && trimLogText("no newline", 5, 100).empty());
    assert(trimLogText(t, 0, 1000).empty());

    std::puts("test_mac_paths: ok");
    return 0;
}
