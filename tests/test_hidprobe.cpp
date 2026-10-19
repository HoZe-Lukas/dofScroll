// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tests for the pure parts of dofscroll-hidprobe and the fd report stream (pipe, no hardware).
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <string>

#include "../tools/hidprobe/hidprobe_posix.hpp"

using namespace hidprobe;

static spn::HidDescriptor makeDesc() {
    spn::HidDescriptor d;
    std::string err;
    bool ok = spn::parseHidDescriptor(
        "name = Test\nid = 0x046d:0xc62b\nfactory_buttons = 2\n"
        "axis.tx = report=1 offset=0 type=s16le\naxis.ty = report=1 offset=2 type=s16le\n"
        "axis.tz = report=1 offset=4 type=s16le\naxis.rx = report=2 offset=0 type=s16le\n"
        "axis.ry = report=2 offset=2 type=s16le\naxis.rz = report=2 offset=4 type=s16le\n"
        "buttons = report=3 offset=0 bits=2\n",
        d, err);
    assert(ok);
    (void)ok;
    return d;
}

static bool has(const std::vector<Verdict> &v, Status s, const char *sub) {
    for (const auto &x : v)
        if (x.status == s && x.text.find(sub) != std::string::npos) return true;
    return false;
}

static void testUevent() {
    DeviceInfo d;
    bool ok = parseUevent("DRIVER=hid-generic\nHID_ID=0003:0000046D:0000C62B\n"
                          "HID_NAME=3Dconnexion SpaceMouse Pro\nHID_UNIQ=ABC123\n",
                          d);
    assert(ok && d.vid == 0x046d && d.pid == 0xc62b);
    assert(d.name == "3Dconnexion SpaceMouse Pro" && d.serial == "ABC123");
    DeviceInfo e;
    assert(!parseUevent("HID_NAME=x\nHID_ID=garbage\n", e));
    (void)ok;
}

static void testHelpers() {
    assert(hexBytes(reinterpret_cast<const uint8_t *>("\x01\xab"), 2) == "01 ab");
    assert(vidpid(0x46d, 0xc62b) == "046d:c62b");
    assert(hashSerial("") == "none");
    assert(hashSerial("ABC123") != "ABC123" && hashSerial("ABC123") == hashSerial("ABC123"));
    assert(hashSerial("ABC123") != hashSerial("ABC124"));
    std::string s = scrubReport("/home/alice/x alice", "/home/alice", "alice");
    assert(s.find("alice") == std::string::npos);
    assert(scrubReport("keep", "", "") == "keep");
}

static void testSummary() {
    // Multi-axis controller; report id 1: 3 x 16 bit (6 bytes); report id 3: 2 buttons + padding
    std::vector<uint8_t> d = {
        0x05, 0x01, 0x09, 0x08, 0xa1, 0x01,                          // page GD, usage 8, collection
        0xa1, 0x00, 0x85, 0x01, 0x16, 0x00, 0x80, 0x26, 0xff, 0x7f,  // nested, id 1, min/max
        0x75, 0x10, 0x95, 0x03, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x81, 0x02, 0xc0,
        0x85, 0x03, 0x75, 0x01, 0x95, 0x02, 0x81, 0x02, 0x75, 0x06, 0x95, 0x01, 0x81, 0x03,
        0xc0};
    DescriptorSummary s = summarizeDescriptor(d);
    assert(s.usesReportIds);
    assert(s.inputBytes.at(1) == 7);
    assert(s.inputBytes.at(3) == 2);
    assert(s.lines[0].find("Multi-axis Controller") != std::string::npos);
    assert(s.lines[0].find("Application") != std::string::npos);
    assert(summarizeDescriptor({}).lines.size() == 1);
    // truncated item must not crash
    summarizeDescriptor({0x06, 0x01});
}

static void put16(uint8_t *p, int v) {
    const uint16_t u = static_cast<uint16_t>(v);  // two's complement without shifting a negative int
    p[0] = uint8_t(u & 0xff);
    p[1] = uint8_t((u >> 8) & 0xff);
}

static void testChecker() {
    spn::HidDescriptor d = makeDesc();
    {
        Checker c(d);
        assert(has(c.verdicts(), Status::Mismatch, "No reports"));
    }
    Checker c(d);
    c.setDescriptorLengths({{1, 7}, {2, 7}, {3, 2}});
    uint8_t t[7] = {1}, r[7] = {2}, b[3] = {3, 0, 0};
    put16(t + 1, 0);
    c.feed(t, 7);
    put16(t + 1, 350);
    c.feed(t, 7);
    put16(t + 1, -350);
    c.feed(t, 7);
    put16(r + 1, 100);  // rx moves by a little only
    c.feed(r, 7);
    b[1] = 0x01;
    c.feed(b, 3);
    b[1] = 0x00;
    c.feed(b, 3);
    assert(c.axisMoved(0) && c.axisMin(0) == -350 && c.axisMax(0) == 350);
    assert(c.seenButtons() == 1);
    assert(c.liveLine().find("tx=") == 0);
    std::vector<Verdict> v = c.verdicts();
    assert(has(v, Status::Ok, "Axis tx"));
    assert(has(v, Status::Warn, "Axis ty: no data") == false);  // ty decoded (id 1)
    assert(has(v, Status::Warn, "Axis ty: did not move"));
    assert(has(v, Status::Warn, "Axis rx: observed") == false);
    assert(has(v, Status::Ok, "length 7 covers"));
    assert(has(v, Status::Warn, "descriptor says 2 bytes, observed 3"));  // buttons: 3 bytes seen
    assert(has(v, Status::Warn, "not all buttons pressed"));
    assert(has(v, Status::Info, "Report ids seen: 1, 2, 3"));
    // second button + a bit beyond bits=2 + a short report
    b[1] = 0x06;
    c.feed(b, 3);
    uint8_t shortR[3] = {1, 0, 0};
    c.feed(shortR, 3);
    v = c.verdicts();
    assert(has(v, Status::Mismatch, "Raw bit 2"));
    assert(has(v, Status::Mismatch, "SHORTER"));
    assert(has(v, Status::Warn, "varying lengths"));
    // saturated axis (wrong endianness symptom)
    Checker c2(d);
    uint8_t sat[7] = {1, 0, 0x80, 0, 0, 0, 0};
    c2.feed(sat, 7);
    sat[2] = 0x7f;
    sat[1] = 0xff;
    c2.feed(sat, 7);
    assert(has(c2.verdicts(), Status::Mismatch, "type limit"));
    // dominant axis
    double a0[6] = {0, 0, 0, 0, 0, 0}, a1[6] = {10, -200, 5, 0, 0, 90};
    assert(dominantAxis(a0, a1, 30) == 1);
    assert(dominantAxis(a0, a0, 30) == -1);
}

static void testFindDescriptor() {
    std::vector<spn::HidDescriptor> all{makeDesc()};
    assert(findDescriptor(all, 0x046d, 0xc62b) != nullptr);
    assert(findDescriptor(all, 0x046d, 0x0001) == nullptr);
}

static void testStream() {
    int fds[2];
    int rc = ::pipe(fds);
    assert(rc == 0);
    (void)rc;
    FdReportStream s(fds[0]);
    std::vector<uint8_t> out;
    assert(s.read(10, out) == ReadResult::Timeout);
    const uint8_t rep[3] = {3, 1, 0};
    ssize_t w = ::write(fds[1], rep, 3);
    assert(w == 3);
    (void)w;
    assert(s.read(100, out) == ReadResult::Report && out.size() == 3 && out[0] == 3);
    ::close(fds[1]);
    assert(s.read(100, out) == ReadResult::Closed);
}

int main() {
    testUevent();
    testHelpers();
    testSummary();
    testChecker();
    testFindDescriptor();
    testStream();
    std::puts("test_hidprobe: OK");
    return 0;
}
