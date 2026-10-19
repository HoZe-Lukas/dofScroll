// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit test for counting connected 3Dconnexion devices (pure text parser).
#include <cstdio>
#include <cstdlib>
#include <string>

#include "device_count.hpp"

using namespace spn;

static int g_fail = 0;
#define CHECK_EQ(a, b)                                                                   \
    do {                                                                                 \
        long va = (a), vb = (b);                                                         \
        if (va != vb) {                                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #a, va, vb); \
            ++g_fail;                                                                    \
        }                                                                                \
    } while (0)

static std::string entry(const char *name) {
    return std::string("I: Bus=0003 Vendor=256f Product=c62b Version=0111\nN: Name=\"") + name +
           "\"\nP: Phys=usb-0000:00:14.0-1/input0\nH: Handlers=event5 js0\nB: EV=1b\n\n";
}

int main() {
    CHECK_EQ(countSpaceMiceInText(""), 0);
    CHECK_EQ(countSpaceMiceInText(entry("Logitech USB Keyboard") + entry("AT Translated Set 2 keyboard")), 0);
    CHECK_EQ(countSpaceMiceInText(entry("3Dconnexion SpaceMouse Pro")), 1);
    // two devices (also with the same name) -> 2, foreign devices are not counted
    CHECK_EQ(countSpaceMiceInText(entry("Foo") + entry("3Dconnexion SpaceMouse Pro") +
                                  entry("3Dconnexion SpaceMouse Compact")), 2);
    CHECK_EQ(countSpaceMiceInText(entry("3Dconnexion SpaceMouse Compact") +
                                  entry("3Dconnexion SpaceMouse Compact")), 2);
    // case-insensitivity, secondary input nodes
    CHECK_EQ(countSpaceMiceInText(entry("3DCONNEXION spacemouse wireless")), 1);
    CHECK_EQ(countSpaceMiceInText(entry("3Dconnexion Universal Receiver") +
                                  entry("3Dconnexion Universal Receiver Keyboard") +
                                  entry("3Dconnexion Universal Receiver Consumer Control")), 1);
    // name only in another line (e.g. Phys/Sysfs) does not count
    CHECK_EQ(countSpaceMiceInText("N: Name=\"Foo\"\nS: Sysfs=/devices/3Dconnexion\n"), 0);
    // without a trailing newline
    CHECK_EQ(countSpaceMiceInText("N: Name=\"3Dconnexion SpaceMouse Pro\""), 1);

    if (g_fail) return 1;
    std::puts("test_device_count: ok");
    return 0;
}
