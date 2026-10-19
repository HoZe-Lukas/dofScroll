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

    // ---- Windows path hazards ----
    // reserved device names: any case, with or without extension, trailing spaces, superscripts
    for (const char *r : {"con", "CON", "Prn", "aux", "NUL", "com1", "COM9", "lpt1", "LPT9", "conin$", "CONOUT$"})
        assert(isReservedDeviceStem(r));
    assert(isReservedDeviceStem("nul ") && isReservedDeviceStem("com\xC2\xB9") && isReservedDeviceStem("LPT\xC2\xB3"));
    for (const char *r : {"", "co", "conn", "console", "com0", "com10", "lpt0", "lpt", "com", "aux1", "nu", "concon"})
        assert(!isReservedDeviceStem(r));
    assert(isReservedDeviceName("con.txt") && isReservedDeviceName("NUL.tar.gz") && isReservedDeviceName("com1.conf") &&
           isReservedDeviceName("aux .x") && !isReservedDeviceName("console.txt") && !isReservedDeviceName("a.con") &&
           !isReservedDeviceName("xcon") && !isReservedDeviceName(".con"));

    assert(windowsPathHazard("C:/Users/x/AppData/Roaming/dofscroll/dofscroll.conf") == PathHazard::None);
    assert(windowsPathHazard("C:\\Users\\x\\a.conf") == PathHazard::None);
    assert(windowsPathHazard("rel/dir/../x.conf") == PathHazard::None);
    assert(windowsPathHazard("//server/share/dir/x.conf") == PathHazard::None);  // normal UNC is allowed
    assert(windowsPathHazard("./x/./y") == PathHazard::None);
    assert(windowsPathHazard("") == PathHazard::Empty);
    assert(windowsPathHazard("C:/dir/con") == PathHazard::ReservedName);
    assert(windowsPathHazard("C:/dir/NUL.txt/x") == PathHazard::ReservedName);  // any component
    assert(windowsPathHazard("modes/com1.conf") == PathHazard::ReservedName);
    assert(windowsPathHazard("modes/lpt9") == PathHazard::ReservedName);
    assert(windowsPathHazard("C:/dir/file.conf:stream") == PathHazard::AlternateStream);
    assert(windowsPathHazard("C:/dir/file.conf::$DATA") == PathHazard::AlternateStream);
    assert(windowsPathHazard("dir:x/file") == PathHazard::AlternateStream);
    assert(windowsPathHazard("1:/x") == PathHazard::AlternateStream);
    assert(windowsPathHazard("C:/dir/name.") == PathHazard::TrailingDotOrSpace);
    assert(windowsPathHazard("C:/dir /x") == PathHazard::TrailingDotOrSpace);
    assert(windowsPathHazard("C:/dir/x.conf ") == PathHazard::TrailingDotOrSpace);
    assert(windowsPathHazard("C:/dir/.../x") == PathHazard::TrailingDotOrSpace);
    assert(windowsPathHazard("C:\\dir\\x.") == PathHazard::TrailingDotOrSpace);
    assert(windowsPathHazard("\\\\?\\C:\\x") == PathHazard::VerbatimPrefix);
    assert(windowsPathHazard("//?/C:/x") == PathHazard::VerbatimPrefix);
    assert(windowsPathHazard("\\\\?\\UNC\\server\\share\\x") == PathHazard::VerbatimPrefix);
    assert(windowsPathHazard("\\\\.\\COM1") == PathHazard::DevicePrefix);
    assert(windowsPathHazard("//./pipe/x") == PathHazard::DevicePrefix);
    assert(windowsPathHazard("\\??\\C:\\x") == PathHazard::DevicePrefix);
    assert(windowsPathHazard("C:") == PathHazard::DriveRelative);
    assert(windowsPathHazard("C:foo") == PathHazard::DriveRelative);
    assert(windowsPathHazard("c:foo/bar") == PathHazard::DriveRelative);
    assert(windowsPathHazard("C:/x/a*b") == PathHazard::InvalidChar);
    assert(windowsPathHazard("C:/x/a?b") == PathHazard::InvalidChar);
    assert(windowsPathHazard("C:/x/a|b") == PathHazard::InvalidChar);
    assert(windowsPathHazard(std::string("C:/x/a\1b")) == PathHazard::InvalidChar);
    assert(std::string(pathHazardName(PathHazard::ReservedName)) == "reserved device name");

    // verbatim prefix as returned by GetFinalPathNameByHandle
    assert(stripVerbatimPrefix("\\\\?\\C:\\Users\\x\\a.conf") == "C:\\Users\\x\\a.conf");
    assert(stripVerbatimPrefix("\\\\?\\UNC\\server\\share\\dir\\a.conf") == "\\\\server\\share\\dir\\a.conf");
    assert(stripVerbatimPrefix("C:\\plain") == "C:\\plain");
    assert(stripVerbatimPrefix("\\\\server\\share") == "\\\\server\\share");
    assert(stripVerbatimPrefix("") == "");

    std::puts("test_path_rules: ok");
    return 0;
}
