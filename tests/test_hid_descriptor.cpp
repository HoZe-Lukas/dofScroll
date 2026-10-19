// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <string>

#include "hid_descriptor.hpp"
#include "testutil.hpp"

using namespace spn;

static const char *kGood =
    "# test device\n"
    "name = Test Mouse\n"
    "id = 0x256f:0xc635\n"
    "id = 0x1234:0x5678  # second id\n"
    "factory_buttons = 2\n"
    "axis.tx = report=1 offset=0 type=s16le\n"
    "axis.ty = report=1 offset=2 type=s16le scale=0.5\n"
    "axis.tz = report=1 offset=4 type=s16be\n"
    "axis.rx = report=2 offset=0 type=s8\n"
    "buttons = report=3 offset=0 bits=12\n";

static void testParse() {
    HidDescriptor d;
    std::string err;
    assert(parseHidDescriptor(kGood, d, err));
    assert(d.name == "Test Mouse");
    assert(d.ids.size() == 2 && d.ids[0].first == 0x256f && d.ids[1].second == 0x5678);
    assert(d.factoryButtons == 2);
    assert(d.axes[1].scale == 0.5 && d.axes[2].type == HidType::S16BE);
    assert(!d.axes[5].present);
    assert(d.hasButtons && d.buttonBits == 12);

    const char *bad[] = {
        "name = X\nid = 0x1:0x2\n",                                            // nothing to decode
        "id = 0x1:0x2\naxis.tx = report=1 offset=0 type=s16le\n",              // no name
        "name = X\naxis.tx = report=1 offset=0 type=s16le\n",                  // no id
        "name = X\nid = 1:2\naxis.qq = report=1 offset=0 type=s16le\n",        // axis name
        "name = X\nid = 1:2\naxis.tx = report=1 offset=0 type=f32\n",          // type
        "name = X\nid = 1:2\naxis.tx = report=1 offset=0\n",                   // missing type
        "name = X\nid = 1:2\naxis.tx = report=1 offset=0 type=s8 foo=1\n",     // extra option
        "name = X\nid = 1:2\naxis.tx = report=300 offset=0 type=s8\n",         // report > 255
        "name = X\nid = 1:2\naxis.tx = report=1 offset=0 type=s8 scale=0\n",   // zero scale
        "name = X\nid = 70000:2\naxis.tx = report=1 offset=0 type=s8\n",       // vid > 16 bit
        "name = X\nid = 1:2\nbuttons = report=3 offset=0 bits=33\n",           // too many bits
        "name = X\nid = 1:2\nfactory_buttons = 9\nbuttons = report=3 offset=0 bits=4\n",
        "name = X\nid = 1:2\nbogus = 1\nbuttons = report=3 offset=0 bits=4\n",  // unknown key
        "name = X\nid = 1:2\ngarbage line\n",
    };
    for (const char *b : bad) {
        HidDescriptor x;
        std::string e;
        assert(!parseHidDescriptor(b, x, e));
        assert(!e.empty());
    }
}

static void testDecode() {
    HidDescriptor d;
    std::string err;
    assert(parseHidDescriptor(kGood, d, err));
    HidDecoded o;

    // translation report: tx=+350 (0x015e LE), ty=-100 (0xff9c LE) scaled 0.5, tz=-2 (0xfffe BE)
    const uint8_t r1[] = {1, 0x5e, 0x01, 0x9c, 0xff, 0xff, 0xfe};
    assert(decodeHidReport(d, r1, sizeof r1, o));
    assert(o.axisSet[0] && o.axis[0] == 350.0);
    assert(o.axisSet[1] && o.axis[1] == -50.0);
    assert(o.axisSet[2] && o.axis[2] == -2.0);
    assert(!o.axisSet[3] && !o.buttonsSet);

    // rotation report with an s8 axis
    const uint8_t r2[] = {2, 0xf6};
    assert(decodeHidReport(d, r2, sizeof r2, o));
    assert(o.axisSet[3] && o.axis[3] == -10.0);

    // buttons: 12 bits, bits 0 and 11 set; high nibble of the second byte is masked off
    const uint8_t r3[] = {3, 0x01, 0xf8};
    assert(decodeHidReport(d, r3, sizeof r3, o));
    assert(o.buttonsSet && o.buttons == 0x801u);

    // truncated, unknown id, empty
    const uint8_t shortR[] = {1, 0x5e, 0x01, 0x9c};
    assert(decodeHidReport(d, shortR, sizeof shortR, o));  // tx fits, others do not
    assert(o.axisSet[0] && !o.axisSet[1] && !o.axisSet[2]);
    const uint8_t unk[] = {9, 1, 2, 3};
    assert(!decodeHidReport(d, unk, sizeof unk, o));
    assert(!decodeHidReport(d, r3, 2, o));
    assert(!decodeHidReport(d, nullptr, 0, o));
    const uint8_t idOnly[] = {1};
    assert(!decodeHidReport(d, idOnly, 1, o));

    // 32-bit buttons and s32le
    HidDescriptor w;
    assert(parseHidDescriptor("name = W\nid = 1:2\naxis.tx = report=1 offset=1 type=s32le\n"
                              "buttons = report=3 offset=0 bits=32\n", w, err));
    const uint8_t r4[] = {1, 0, 0x00, 0x00, 0x00, 0x80};
    assert(decodeHidReport(w, r4, sizeof r4, o) && o.axis[0] == -2147483648.0);
    const uint8_t r5[] = {3, 0xff, 0xff, 0xff, 0xff};
    assert(decodeHidReport(w, r5, sizeof r5, o) && o.buttons == 0xffffffffu);
}

static void testFiles(const char *devicesDir) {
    // shipped descriptions must all parse
    std::vector<HidDescriptor> all;
    std::vector<std::string> errs;
    assert(loadHidDescriptorDir(devicesDir, all, errs));
    for (const auto &e : errs) std::fprintf(stderr, "descriptor error: %s\n", e.c_str());
    assert(errs.empty());
    assert(all.size() >= 6);
    for (const auto &d : all) assert(d.factoryButtons > 0);

    // limits, symlinks, directories
    std::string base = tu::makeTempDir("test_hid_descriptor");
    {
        std::ofstream(base + "/ok.conf") << kGood;
        std::string big = kGood;
        big += "# " + std::string(kMaxDescriptorBytes, 'x') + "\n";
        std::ofstream(base + "/big.conf") << big;
        std::ofstream(base + "/bad.conf") << "name = X\n";
        std::ofstream(base + "/ignored.txt") << kGood;
#ifndef _WIN32  // symlinks need privileges on Windows: not exercised there
        assert(symlink((base + "/ok.conf").c_str(), (base + "/link.conf").c_str()) == 0);
#endif
        assert(std::filesystem::create_directory(base + "/dir.conf"));
    }
    all.clear();
    errs.clear();
    assert(loadHidDescriptorDir(base, all, errs));
    assert(all.size() == 1 && all[0].name == "Test Mouse");
#ifndef _WIN32
    assert(errs.size() == 4);  // big, bad, dir, link
#else
    assert(errs.size() == 3);  // big, bad, dir
#endif

    HidDescriptor d;
    std::string err;
#ifndef _WIN32
    assert(!loadHidDescriptorFile(base + "/link.conf", d, err));
#endif
    assert(!loadHidDescriptorFile(base + "/missing.conf", d, err));
    assert(!loadHidDescriptorDir(base + "/nope", all, errs));

    // file count limit
    for (unsigned i = 0; i < kMaxDescriptorFiles + 5; ++i) {
        char n[32];
        std::snprintf(n, sizeof n, "/n%03u.conf", i);
        std::ofstream(base + n) << kGood;
    }
    all.clear();
    errs.clear();
    assert(loadHidDescriptorDir(base, all, errs));
    assert(all.size() <= kMaxDescriptorFiles);

    tu::removeAll(base);
}

int main() {
    testParse();
    testDecode();
    testFiles("devices");
    std::puts("test_hid_descriptor OK");
    return 0;
}
