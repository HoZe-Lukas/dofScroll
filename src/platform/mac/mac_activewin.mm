// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Frontmost application on macOS via NSWorkspace (see mac_activewin.hpp). All Objective-C of the
// daemon lives in this file.
#include "mac_activewin.hpp"
#include "../../utf8.hpp"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace spn {

namespace {

class MacActiveWindowProvider : public ActiveWindowProvider {
public:
    bool init() override { return true; }

    // NSWorkspace.frontmostApplication is documented as updated by the window server and may be
    // read from any thread; it is polled by the daemon core about ten times a second. Without a
    // running NSApplication the value can lag behind app switches (it is KVO/notification
    // driven): unverified on hardware, see docs/port-macos.md.
    std::string poll() override {
        @autoreleasepool {
            NSRunningApplication *app = [[NSWorkspace sharedWorkspace] frontmostApplication];
            if (app == nil) return std::string();
            NSString *name = app.bundleIdentifier;
            if (name == nil || name.length == 0) name = app.localizedName;
            if (name == nil) return std::string();
            name = [name precomposedStringWithCanonicalMapping];  // NFC: patterns are written in NFC
            const char *utf8 = [name UTF8String];
            if (utf8 == nullptr) return std::string();
            std::string s(utf8);
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return spn::utf8::lowerChar(static_cast<char>(c)); });
            return s;
        }
    }
};

}  // namespace

std::unique_ptr<ActiveWindowProvider> makeMacActiveWindowProvider() {
    return std::make_unique<MacActiveWindowProvider>();
}

}  // namespace spn
