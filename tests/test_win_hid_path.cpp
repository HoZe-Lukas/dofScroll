// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure helpers of the Windows HID input backend: path parsing and the rescan diff.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/win/win_hid_path.hpp"

using namespace spn;

int main() {
    unsigned v = 0, p = 0;
    const std::string guid = "#{4d1e55b2-f16f-11cf-88cb-001111000030}";
    assert(parseHidPath("\\\\?\\hid#vid_256f&pid_c635&mi_00#7&2a3b4c&0&0000" + guid, &v, &p));
    assert(v == 0x256f && p == 0xc635);
    // upper case, '#' terminator
    assert(parseHidPath("\\\\?\\HID#VID_046D&PID_C62B#8&1&0&0000", &v, &p) && v == 0x046d && p == 0xc62b);
    assert(parseHidPath("\\\\?\\hid#vid_0001&pid_0002#x", &v, &p) && v == 1 && p == 2);
    // null outputs are allowed
    assert(parseHidPath("\\\\?\\hid#vid_0001&pid_0002&mi_01#x", nullptr, nullptr));

    // garbage is rejected and leaves the outputs untouched
    v = 7;
    p = 9;
    const char *bad[] = {
        "", "hid#vid_256f&pid_c635#x", "\\\\.\\hid#vid_256f&pid_c635#x", "\\\\?\\usb#vid_256f&pid_c635#x",
        "\\\\?\\hid#vid_256&pid_c635#x", "\\\\?\\hid#vid_256f&pid_c63#x", "\\\\?\\hid#vid_256g&pid_c635#x",
        "\\\\?\\hid#vid_256f&pid_c635", "\\\\?\\hid#vid_256f&pid_c635x", "\\\\?\\hid#vid_256f&pid__c635#x",
        "\\\\?\\hid#pid_c635&vid_256f#x", "\\\\?\\hid#", "\\\\?\\hid#vid_", "\\\\?\\hid#xvid_256f&pid_c635#x",
        "\\\\?\\hid#vid_+56f&pid_c635#x",
    };
    for (const char *b : bad) assert(!parseHidPath(b, &v, &p));
    assert(v == 7 && p == 9);
    // embedded NUL and non-ASCII bytes do not crash
    assert(!parseHidPath(std::string("\\\\?\\hid#vid_25\0f&pid_c635#x", 27), &v, &p));
    assert(!parseHidPath("\\\\?\\hid#vid_25\xc3\xa9&pid_c635#x", &v, &p));

    assert(hidPathKey("\\\\?\\HID#VID_ABCD") == "\\\\?\\hid#vid_abcd");

    // rescan diff
    PathDiff d = diffPaths({}, {});
    assert(d.added.empty() && d.removed.empty());
    d = diffPaths({}, {"A", "B"});
    assert(d.added.size() == 2 && d.removed.empty());
    d = diffPaths({"A", "B"}, {"a", "C"});  // case-insensitive: A stays, B gone, C new
    assert(d.added.size() == 1 && d.added[0] == "C" && d.removed.size() == 1 && d.removed[0] == "B");
    d = diffPaths({"A"}, {"X", "x"});  // duplicates in the scan are reported once
    assert(d.added.size() == 1 && d.added[0] == "X" && d.removed.size() == 1);
    d = diffPaths({"A"}, {"A"});
    assert(d.added.empty() && d.removed.empty());

    // interface picking: one per container, multi-axis controller first, junk usages never
    {
        auto c = [](const char *path, const char *cont, unsigned page, unsigned usage) {
            HidCandidate h;
            h.path = path;
            h.container = cont;
            h.usagePage = page;
            h.usage = usage;
            return h;
        };
        std::vector<HidCandidate> v = {
            c("p1", "{A}", 0x01, 0x06),    // keyboard: ignored
            c("p2", "{A}", 0xFF00, 0x01),  // vendor page
            c("p3", "{A}", 0x01, 0x08),    // multi-axis controller: wins in {A}
            c("p4", "{B}", 0x0C, 0x01),    // consumer: ignored, so {B} has nothing
            c("p5", "{C}", 0x01, 0x02),    // mouse: ignored
            c("p6", "{C}", 0xFF00, 0x02),  // the only usable one of {C}
            c("p7", "", 0x01, 0x08),       // unknown container: its own device
            c("p8", "", 0x01, 0x08),       // ... and so is this one
        };
        std::vector<size_t> r = pickInterfaces(v, {});
        assert((r == std::vector<size_t>{2, 5, 6, 7}));
        r = pickInterfaces(v, {"{a}"});  // {A} already open (case-insensitive)
        assert((r == std::vector<size_t>{5, 6, 7}));
        r = pickInterfaces({}, {});
        assert(r.empty());
        // equal rank: the smaller path wins
        std::vector<HidCandidate> w = {c("zz", "{X}", 0x01, 0x08), c("aa", "{X}", 0x01, 0x08)};
        r = pickInterfaces(w, {});
        assert((r == std::vector<size_t>{1}));
        assert(isIgnoredHidUsage(0x0C, 5) && isIgnoredHidUsage(0x01, 0x06) && !isIgnoredHidUsage(0x01, 0x08) &&
               !isIgnoredHidUsage(0xFF00, 1));
    }

    std::puts("test_win_hid_path: ok");
    return 0;
}
