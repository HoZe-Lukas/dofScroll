// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Running programs on macOS (see platform/running_apps.hpp) via NSWorkspace. matchString uses the
// same rule as mac_activewin.mm (macMatchString: bundle identifier, else localized name,
// lowercased). Compile-checked on the macOS runner; behaviour on a real desktop is unverified.
#include "../running_apps.hpp"
#include "../running_apps_os.hpp"

#import <AppKit/AppKit.h>

#include <unistd.h>

#include <memory>
#include <string>
#include <vector>

namespace spn {

namespace {

std::string utf8(NSString *s) {
    if (s == nil) return std::string();
    const char *p = [s UTF8String];
    return p ? std::string(p) : std::string();
}

class MacRunningApps : public RunningAppsProvider {
public:
    // Thread limits: NSWorkspace.runningApplications is read-only and may be called from any
    // thread, but the array only refreshes through the main run loop. In a process without a
    // running NSApplication (the daemon, the self-test) later calls can return a stale snapshot;
    // from the Qt UI's main thread it is current. Without a window server session (headless
    // runner) the list may be empty; true is still returned.
    bool list(std::vector<RunningApp> *out, std::string *err) override {
        (void)err;
        if (out) out->clear();
        std::vector<RunningApp> apps;
        @autoreleasepool {
            NSArray<NSRunningApplication *> *running = [[NSWorkspace sharedWorkspace] runningApplications];
            const pid_t self = getpid();
            for (NSRunningApplication *app in running) {
                if (app.activationPolicy != NSApplicationActivationPolicyRegular) continue;
                if (app.processIdentifier == self) continue;
                RunningApp a;
                a.matchString = macMatchString(utf8(app.bundleIdentifier), utf8(app.localizedName));
                if (a.matchString.empty()) continue;
                a.displayName = truncateUtf8(utf8(app.localizedName), 128);
                if (a.displayName.empty()) a.displayName = a.matchString;
                a.iconHint = utf8(app.bundleURL.path);  // bundle path; the UI asks NSWorkspace for the icon
                a.pid = static_cast<int>(app.processIdentifier);
                apps.push_back(std::move(a));
            }
        }
        // No window titles without the screen-recording permission: left empty.
        if (out) *out = dedupeApps(apps);
        return true;
    }
};

}  // namespace

std::unique_ptr<RunningAppsProvider> makeRunningAppsProvider() {
    return std::make_unique<MacRunningApps>();
}

}  // namespace spn
