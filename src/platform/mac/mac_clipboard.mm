// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Clipboard on macOS via NSPasteboard (see mac_clipboard.hpp). Not run on hardware, see docs/port-macos.md.
#include "mac_clipboard.hpp"

#import <AppKit/AppKit.h>

namespace spn {

bool macClipboardGetText(std::string *out) {
    @autoreleasepool {
        NSString *s = [[NSPasteboard generalPasteboard] stringForType:NSPasteboardTypeString];
        if (s == nil) return false;
        const char *u = [s UTF8String];
        if (u == nullptr) return false;
        out->assign(u);
        return true;
    }
}

long macClipboardSetText(const std::string &utf8) {
    @autoreleasepool {
        NSString *s = [[NSString alloc] initWithBytes:utf8.data() length:utf8.size() encoding:NSUTF8StringEncoding];
        if (s == nil) return -1;
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        if (![pb setString:s forType:NSPasteboardTypeString]) return -1;
        return static_cast<long>([pb changeCount]);
    }
}

long macClipboardClear() {
    @autoreleasepool {
        NSPasteboard *pb = [NSPasteboard generalPasteboard];
        [pb clearContents];
        return static_cast<long>([pb changeCount]);
    }
}

long macClipboardChangeCount() {
    @autoreleasepool { return static_cast<long>([[NSPasteboard generalPasteboard] changeCount]); }
}

}  // namespace spn
