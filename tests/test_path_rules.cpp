// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure path rules of the Windows and macOS backends (src/platform/path_rules.hpp); they run on
// every OS because they take the environment as arguments.
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "platform/path_rules.hpp"

using namespace spn;

int main() {
    assert(toSlashes("C:\\Users\\x\\AppData") == "C:/Users/x/AppData");
    assert(toSlashes("/a/b") == "/a/b");

    assert(isAbsolutePath("/x") && isAbsolutePath("C:/x") && isAbsolutePath("c:\\x") && isAbsolutePath("//srv/share"));
    assert(!isAbsolutePath("") && !isAbsolutePath("x/y") && !isAbsolutePath("C:") && !isAbsolutePath("C:x"));

    assert(stripTrailingSlashes("C:/a/") == "C:/a");
    assert(stripTrailingSlashes("C:/") == "C:/");
    assert(stripTrailingSlashes("/") == "/");

    // Windows
    assert(windowsConfigBase("C:\\Users\\x\\AppData\\Roaming", "C:\\Users\\x") == "C:/Users/x/AppData/Roaming");
    assert(windowsConfigBase("C:\\Users\\x\\AppData\\Roaming\\", "") == "C:/Users/x/AppData/Roaming");
    assert(windowsConfigBase("", "C:\\Users\\x") == "C:/Users/x/AppData/Roaming");
    assert(windowsConfigBase("relative", "C:\\Users\\x") == "C:/Users/x/AppData/Roaming");
    assert(windowsConfigBase("", "") == "./AppData/Roaming");
    assert(windowsRuntimeBase("C:\\Users\\x\\AppData\\Local", "") == "C:/Users/x/AppData/Local");
    assert(windowsRuntimeBase("", "C:\\Users\\x") == "C:/Users/x/AppData/Local");
    assert(windowsRuntimeBase("", "").empty());  // no safe per-user directory -> no runtime files

    // macOS
    assert(macConfigBase("/Users/x") == "/Users/x/Library/Application Support");
    assert(macConfigBase("/Users/x/") == "/Users/x/Library/Application Support");
    assert(macConfigBase("") == "./Library/Application Support");
    assert(macConfigBase("rel") == "./Library/Application Support");
    assert(macRuntimeBase("/Users/x") == "/Users/x/Library/Caches");
    assert(macRuntimeBase("").empty());

    std::puts("test_path_rules: ok");
    return 0;
}
