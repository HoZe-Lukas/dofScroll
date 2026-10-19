// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS: kCGSessionOnConsoleKey of CGSessionCopyCurrentDictionary. It is false while another
// user's session owns the console (fast user switching). Limits: a locked screen of the same user
// still counts as active; without a window-server connection the dictionary is missing (unknown).
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/Foundation.h>

#include "../session_monitor.hpp"

namespace spn {
namespace {

class CgSessionMonitor : public SessionMonitor {
public:
    bool isActive(std::string *detail) override {
        bool have = false, key = false, onConsole = true;
        @autoreleasepool {
            CFDictionaryRef d = CGSessionCopyCurrentDictionary();
            if (d) {
                have = true;
                const void *v = CFDictionaryGetValue(d, kCGSessionOnConsoleKey);
                if (v && CFGetTypeID(v) == CFBooleanGetTypeID()) {
                    key = true;
                    onConsole = CFBooleanGetValue(static_cast<CFBooleanRef>(v));
                }
                CFRelease(d);
            }
        }
        if (detail)
            *detail = have ? (key ? (onConsole ? "on console" : "not on console") : "no console key")
                           : "no session dictionary";
        return decideMacSession(have, key, onConsole) != SessionState::Inactive;
    }
};

}  // namespace

std::unique_ptr<SessionMonitor> makeSessionMonitor() { return std::make_unique<CgSessionMonitor>(); }

}  // namespace spn
