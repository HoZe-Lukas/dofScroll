// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure helpers of the macOS HID backend (compiles and runs on every OS).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "platform/mac/mac_hid_ids.hpp"

using namespace spn;

int main() {
    // matching pairs: sorted, unique, across descriptions
    std::vector<HidDescriptor> ds(2);
    ds[0].ids = {{0x256f, 0xc635}, {0x046d, 0xc62b}};
    ds[1].ids = {{0x256f, 0xc635}, {0x046d, 0xc603}};
    const auto pairs = macMatchingPairs(ds);
    assert(pairs.size() == 3);
    assert(pairs[0] == std::make_pair(0x046du, 0xc603u) && pairs[1] == std::make_pair(0x046du, 0xc62bu) &&
           pairs[2] == std::make_pair(0x256fu, 0xc635u));
    assert(macMatchingPairs({}).empty());

    // the shipped descriptions give a non-empty list
    std::vector<HidDescriptor> all;
    std::vector<std::string> errors;
    assert(loadHidDescriptorDir("devices", all, errors) && errors.empty());
    assert(macMatchingPairs(all).size() >= 6);

    // report reassembly
    std::vector<uint8_t> out;
    const uint8_t prefixed[] = {1, 0x10, 0x00, 0x20, 0x00};
    macRawReport(1, prefixed, sizeof prefixed, false, &out);
    assert((out == std::vector<uint8_t>{1, 0x10, 0x00, 0x20, 0x00}));
    const uint8_t bare[] = {0x10, 0x00, 0x20, 0x00};
    macRawReport(1, bare, sizeof bare, false, &out);
    assert((out == std::vector<uint8_t>{1, 0x10, 0x00, 0x20, 0x00}));
    // ambiguous: payload starts with the id byte; forcePrepend decides
    const uint8_t amb[] = {1, 0x02};
    macRawReport(1, amb, sizeof amb, false, &out);
    assert((out == std::vector<uint8_t>{1, 0x02}));
    macRawReport(1, amb, sizeof amb, true, &out);
    assert((out == std::vector<uint8_t>{1, 1, 0x02}));
    // no report ids
    macRawReport(0, bare, sizeof bare, false, &out);
    assert((out == std::vector<uint8_t>{0, 0x10, 0x00, 0x20, 0x00}));
    macRawReport(3, nullptr, 0, false, &out);
    assert((out == std::vector<uint8_t>{3}));

    // buffer size
    assert(macReportBufferSize(-5) == 64 && macReportBufferSize(0) == 64 && macReportBufferSize(13) == 64);
    assert(macReportBufferSize(100) == 101 && macReportBufferSize(1 << 20) == 4096);

    // IOReturn / permission messages
    assert(kMacIoReturnNotPermitted == -536870174);
    assert(kMacIoReturnExclusiveAccess == -536870203);
    assert(macAccessFromInt(0) == MacAccess::Granted && macAccessFromInt(1) == MacAccess::Denied &&
           macAccessFromInt(2) == MacAccess::Unknown && macAccessFromInt(99) == MacAccess::Unknown);
    assert(macPermissionMessage(MacAccess::Denied).find("Input Monitoring permission missing") == 0);
    assert(macOpenFailureMessage(kMacIoReturnNotPermitted).find("Input Monitoring") != std::string::npos);
    assert(macOpenFailureMessage(kMacIoReturnExclusiveAccess).find("exclusively") != std::string::npos);
    assert(macOpenFailureMessage(-1).find("IOReturn 0xFFFFFFFF") != std::string::npos);

    std::puts("test_mac_hid_ids: ok");
    return 0;
}
