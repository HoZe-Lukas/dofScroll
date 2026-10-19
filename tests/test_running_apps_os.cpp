// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure rules of the Windows/macOS running-programs providers (platform/running_apps_os.hpp).
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "platform/running_apps_os.hpp"
#include "testutil.hpp"

using namespace spn;

int main() {
    // Windows match string = foreground provider rule
    assert(winMatchString("C:\\Program Files\\Mozilla Firefox\\Firefox.EXE") == "firefox.exe");
    assert(winMatchString("D:/x/Code.exe") == "code.exe");
    assert(winMatchString("") == "");
    assert(winMatchString("C:\\Windows\\System32\\ApplicationFrameHost.exe") == "");
    {
        RunningApp own;
        own.matchString = winMatchString("C:\\a\\Dofscroll.exe");
        assert(own.matchString == "dofscroll.exe");
        assert(dedupeApps({own}).empty());
    }

    // display name
    assert(winDisplayName("  Mozilla Firefox ", "C:\\a\\firefox.exe") == "Mozilla Firefox");
    assert(winDisplayName("", "C:\\a\\My.Tool.EXE") == "My.Tool");
    assert(winDisplayName("   ", "tool") == "tool");
    assert(winDisplayName("", "C:\\a\\.hidden") == ".hidden");

    // truncation never splits UTF-8
    assert(truncateUtf8("abc", 10) == "abc");
    assert(truncateUtf8("ab\xc3\xa4", 3) == "ab");  // would cut the 2-byte char
    assert(truncateUtf8("ab\xc3\xa4", 4) == "ab\xc3\xa4");
    assert(truncateUtf8(std::string(600, 'x')).size() == kMaxAppTitleLen);

    // window filter
    WinWindowFlags f;
    f.visible = f.hasTitle = true;
    assert(winWindowListable(f));
    f.cloaked = true;
    assert(!winWindowListable(f));
    f.cloaked = false;
    f.toolWindow = true;
    assert(!winWindowListable(f));
    f.toolWindow = false;
    f.hasTitle = false;
    assert(!winWindowListable(f));
    f.hasTitle = true;
    f.visible = false;
    assert(!winWindowListable(f));

    // macOS match string
    assert(macMatchString("com.Apple.Safari", "Safari") == "com.apple.safari");
    assert(macMatchString("", "Some App") == "some app");
    assert(macMatchString("", "") == "");

    // device snapshot
    DeviceInfo a, b;
    a.name = "3Dconnexion SpaceMouse Pro";
    auto d = deviceListFromSnapshot({a, b});
    assert(d.size() == 1 && d[0].name == a.name);

    std::puts("test_running_apps_os: ok");
    return 0;
}
