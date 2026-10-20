// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The fixed-location rules for loading the driver interface (src/platform/driver_paths.hpp).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/driver_paths.hpp"

using namespace spn;

int main() {
    // ---- Windows: normalization ----
    assert(normalizeWinDriverPath("C:\\Program Files\\3Dconnexion\\x.dll") == "c:/program files/3dconnexion/x.dll");
    assert(normalizeWinDriverPath("c:/Program Files/a/b.dll") == "c:/program files/a/b.dll");
    for (const char *bad : {"", "x", "relative\\a.dll", "\\\\server\\share\\a.dll", "\\\\?\\C:\\a\\b.dll", "\\\\.\\C:\\a",
                            "C:\\a\\..\\b.dll", "C:\\a\\.\\b.dll", "C:\\a\\..", "C:a\\b.dll", "C:\\a\\b.dll:stream",
                            "C:\\a\\\\b.dll", "C:\\a\\b\x01.dll", "C:\\a\\b\x7f.dll"})
        assert(normalizeWinDriverPath(bad).empty());
    assert(normalizeWinDriverPath(std::string(2000, 'a')).empty());

    // ---- Windows: strictly below a root ----
    const std::vector<std::string> roots = {"C:\\Program Files", "C:\\Program Files (x86)\\"};
    assert(winDriverPathAllowed("C:\\Program Files\\3Dconnexion\\3DxWare\\TDxInput.dll", roots));
    assert(winDriverPathAllowed("c:/program files (x86)/x/y.dll", roots));
    assert(!winDriverPathAllowed("C:\\Program Files", roots));                 // the root itself
    assert(!winDriverPathAllowed("C:\\Program Files2\\x.dll", roots));          // a sibling with the same prefix
    assert(!winDriverPathAllowed("C:\\Users\\me\\Program Files\\x.dll", roots));
    assert(!winDriverPathAllowed("C:\\Program Files\\..\\Users\\x.dll", roots));  // traversal
    assert(!winDriverPathAllowed("D:\\Program Files\\x.dll", roots));
    assert(!winDriverPathAllowed("\\\\?\\C:\\Program Files\\x.dll", roots));
    assert(!winDriverPathAllowed("C:\\Program Files\\x.dll", {}));
    assert(!winDriverPathAllowed("C:\\Program Files\\x.dll", {"", "relative", "C:\\"}));

    // ---- macOS ----
    assert(macLibraryPathAllowed(std::string(kMacClientFrameworkDir) + "/Versions/A/3DconnexionClient"));
    assert(macLibraryPathAllowed(kMacClientLibrary));
    assert(!macLibraryPathAllowed("/Library/Frameworks/3DconnexionClient.framework"));
    assert(!macLibraryPathAllowed("/Library/Frameworks/3DconnexionClient.framework/../Evil/lib"));
    assert(!macLibraryPathAllowed("/Library/Frameworks/3DconnexionClient.frameworkX/lib"));
    assert(!macLibraryPathAllowed("/tmp/3DconnexionClient"));
    assert(!macLibraryPathAllowed(""));
    std::string why;
    assert(macLibraryOwnerOk(0, 0100755, &why));
    assert(macLibraryOwnerOk(0, 0100644, nullptr));
    assert(!macLibraryOwnerOk(501, 0100755, &why) && why == "not owned by root");
    assert(!macLibraryOwnerOk(0, 0100775, &why) && why == "writable by group or others");
    assert(!macLibraryOwnerOk(0, 0100757, &why));

    // the fixed identifiers are what the documentation says
    assert(std::string(kWinDriverProgId) == "TDxInput.Device");
    assert(std::string(kMacClientLibrary) == "/Library/Frameworks/3DconnexionClient.framework/3DconnexionClient");

    std::puts("test_driver_paths: ok");
    return 0;
}
