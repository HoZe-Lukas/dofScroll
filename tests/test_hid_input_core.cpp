// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// HidInputCore: device matching, per-device state, report -> InputEvent conversion.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/hid_input_core.hpp"

using namespace spn;

static HidDescriptor mk(const char *text) {
    HidDescriptor d;
    std::string err;
    const bool ok = parseHidDescriptor(text, d, err);
    assert(ok && err.empty());
    return d;
}

static const char *kCompact =
    "name = SpaceMouse Compact\nid = 0x256f:0xc635\nfactory_buttons = 2\n"
    "axis.tx = report=1 offset=0 type=s16le\naxis.ty = report=1 offset=2 type=s16le\n"
    "axis.tz = report=1 offset=4 type=s16le\naxis.rx = report=2 offset=0 type=s16le\n"
    "axis.ry = report=2 offset=2 type=s16le\naxis.rz = report=2 offset=4 type=s16le\n"
    "buttons = report=3 offset=0 bits=2\n";
static const char *kPro =
    "name = SpaceMouse Pro\nid = 0x046d:0xc62b\nfactory_buttons = 15\n"
    "axis.tx = report=1 offset=0 type=s16le scale=-1\naxis.ty = report=1 offset=2 type=s16le\n"
    "axis.tz = report=1 offset=4 type=s16le\n"
    "buttons = report=3 offset=0 bits=32\n";

static void s16(std::vector<uint8_t> &v, int x) {
    const unsigned u = static_cast<unsigned>(static_cast<uint16_t>(x));  // two's complement, no negative shift
    v.push_back(static_cast<uint8_t>(u & 0xff));
    v.push_back(static_cast<uint8_t>((u >> 8) & 0xff));
}

int main() {
    HidInputCore core({mk(kCompact), mk(kPro)});
    assert(core.match(0x256f, 0xc635) == 0 && core.match(0x046d, 0xc62b) == 1);
    assert(core.match(0x1234, 0x5678) < 0 && !core.known(0, 0));

    std::vector<InputEvent> ev;
    assert(core.addDevice(0x1234, 0x5678, "Mouse", &ev) == 0 && ev.empty());  // unknown: ignored
    const int a = core.addDevice(0x256f, 0xc635, "OS name", &ev);
    const int b = core.addDevice(0x046d, 0xc62b, "OS name", &ev);
    assert(a == 1 && b == 2 && ev.size() == 2 && ev[0].type == InputEventType::DevicesChanged);
    auto devs = core.devices();
    assert(devs.size() == 2);
    assert(devs[0].id == a && devs[0].name == "SpaceMouse Compact" && devs[0].reportedButtons == 2);
    assert(devs[0].vendor == 0x256f && devs[0].product == 0xc635 && devs[0].axes == 6);
    assert(devs[1].reportedButtons == 15);  // factory count, not the 32 mask bits

    // translation and rotation are separate reports; state is merged
    ev.clear();
    std::vector<uint8_t> r1{1};
    s16(r1, 100); s16(r1, -200); s16(r1, 300);
    assert(core.onReport(a, r1.data(), r1.size(), &ev) == 1);
    assert(ev[0].type == InputEventType::Motion && ev[0].device == a);
    assert((ev[0].axes == std::array<int, 6>{100, -200, 300, 0, 0, 0}));
    ev.clear();
    std::vector<uint8_t> r2{2};
    s16(r2, 5); s16(r2, 6); s16(r2, -7);
    assert(core.onReport(a, r2.data(), r2.size(), &ev) == 1);
    assert((ev[0].axes == std::array<int, 6>{100, -200, 300, 5, 6, -7}));
    // identical report: no event
    ev.clear();
    assert(core.onReport(a, r2.data(), r2.size(), &ev) == 0 && ev.empty());

    // states are per device
    ev.clear();
    assert(core.onReport(b, r1.data(), r1.size(), &ev) == 1);
    assert((ev[0].axes == std::array<int, 6>{-100, -200, 300, 0, 0, 0}) && ev[0].device == b);  // scale=-1 on tx

    // buttons: edges only, lowest bit first
    ev.clear();
    std::vector<uint8_t> btn{3, 0x03};
    assert(core.onReport(a, btn.data(), btn.size(), &ev) == 2);
    assert(ev[0].type == InputEventType::Button && ev[0].button == 0 && ev[0].down);
    assert(ev[1].button == 1 && ev[1].down);
    ev.clear();
    btn[1] = 0x02;
    assert(core.onReport(a, btn.data(), btn.size(), &ev) == 1 && ev[0].button == 0 && !ev[0].down);
    ev.clear();
    assert(core.onReport(a, btn.data(), btn.size(), &ev) == 0);

    // short, foreign and unknown-device reports
    ev.clear();
    std::vector<uint8_t> shortr{1, 0x10};
    assert(core.onReport(a, shortr.data(), shortr.size(), &ev) == 0);
    std::vector<uint8_t> foreign{9, 1, 2, 3};
    assert(core.onReport(a, foreign.data(), foreign.size(), &ev) == 0);
    assert(core.onReport(99, r1.data(), r1.size(), &ev) == 0 && ev.empty());
    assert(core.onReport(a, nullptr, 0, &ev) == 0);

    // clamping of out-of-range values
    HidInputCore c2({mk("name = Wide\nid = 1:2\naxis.tx = report=1 offset=0 type=s32le scale=1000\n")});
    ev.clear();
    const int w = c2.addDevice(1, 2, "", &ev);
    std::vector<uint8_t> big{1, 0x00, 0x10, 0x00, 0x00};  // 4096 * 1000 > limit
    ev.clear();
    assert(c2.onReport(w, big.data(), big.size(), &ev) == 1 && ev[0].axes[0] == kHidAxisLimit);

    // reset releases buttons and zeroes motion
    ev.clear();
    btn[1] = 0x03;
    core.onReport(a, btn.data(), btn.size(), &ev);
    ev.clear();
    core.reset(a, &ev);
    int ups = 0, motions = 0;
    for (const auto &e : ev) {
        if (e.type == InputEventType::Button && !e.down) ++ups;
        if (e.type == InputEventType::Motion) { ++motions; assert((e.axes == std::array<int, 6>{})); }
    }
    assert(ups == 2 && motions == 1);
    ev.clear();
    core.reset(a, &ev);
    assert(ev.empty());

    // removal
    ev.clear();
    core.removeDevice(a, &ev);
    assert(ev.size() == 1 && ev[0].type == InputEventType::DevicesChanged && core.devices().size() == 1);
    ev.clear();
    core.removeDevice(a, &ev);
    assert(ev.empty());
    const int a2 = core.addDevice(0x256f, 0xc635, "", &ev);
    assert(a2 == 3);  // ids are never reused

    // the shipped descriptions all load and are matchable
    std::vector<HidDescriptor> all;
    std::vector<std::string> errors;
    assert(loadHidDescriptorDir("devices", all, errors) && errors.empty() && all.size() >= 6);
    HidInputCore real(all);
    for (const auto &d : all)
        for (const auto &id : d.ids) assert(real.known(id.first, id.second));

    // report decoding bounds: garbage, zero and oversized/undersized lengths never read out of range
    // (ASan/UBSan in CI catch an over-read) and never produce events from a too-short report
    {
        HidInputCore bc({mk(kCompact)});
        std::vector<InputEvent> bev;
        const int bid = bc.addDevice(0x256f, 0xc635, "", &bev);
        assert(bid != 0);
        bev.clear();
        assert(bc.onReport(bid, nullptr, 0, &bev) == 0 && bev.empty());
        const uint8_t one[1] = {1};
        assert(bc.onReport(bid, one, 0, &bev) == 0 && bc.onReport(bid, one, 1, &bev) == 0 && bev.empty());
        std::vector<uint8_t> shortRep = {1, 0x10};  // one payload byte: no s16 axis fits
        assert(bc.onReport(bid, shortRep.data(), shortRep.size(), &bev) == 0 && bev.empty());
        assert(bc.onReport(bid, shortRep.data(), 1, &bev) == 0);  // length 1
        std::vector<uint8_t> big(65536, 0xFF);  // oversized, all ones, every known report id
        for (unsigned rid = 0; rid < 256; ++rid) {
            big[0] = static_cast<uint8_t>(rid);
            bc.onReport(bid, big.data(), big.size(), &bev);
            bc.onReport(bid, big.data(), 7, &bev);
            bc.onReport(bid, big.data(), 2, &bev);
        }
        std::vector<uint8_t> zero(64, 0);
        for (unsigned rid = 0; rid < 256; ++rid) {
            zero[0] = static_cast<uint8_t>(rid);
            bc.onReport(bid, zero.data(), zero.size(), &bev);
        }
        uint32_t seed = 12345;  // deterministic garbage
        for (int i = 0; i < 2000; ++i) {
            std::vector<uint8_t> g(seed % 40);
            for (uint8_t &b : g) {
                seed = seed * 1664525u + 1013904223u;
                b = static_cast<uint8_t>(seed >> 24);
            }
            seed = seed * 1664525u + 1013904223u;
            bc.onReport(bid, g.data(), g.size(), &bev);
        }
        for (const InputEvent &e : bev)
            if (e.type == InputEventType::Motion)
                for (int a : e.axes) assert(a >= -kHidAxisLimit && a <= kHidAxisLimit);
        assert(bc.onReport(bid + 99, big.data(), big.size(), &bev) == 0);  // unknown device id
        // a descriptor with a huge offset must not overflow the bounds check
        HidDescriptor evil = mk(kCompact);
        evil.axes[0].offset = 0xFFFFFFFFu;
        evil.buttonOffset = 0xFFFFFFFFu;
        HidDecoded dec;
        (void)decodeHidReport(evil, big.data(), 16, dec);
        assert(!dec.axisSet[0] && !dec.buttonsSet);
    }

    std::puts("test_hid_input_core: ok");
    return 0;
}
