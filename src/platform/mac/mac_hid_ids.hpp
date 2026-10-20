// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure helpers of the macOS IOHID input backend (no Apple headers), unit-tested on every OS
// (tests/test_mac_hid_ids.cpp): matching pairs, report reassembly, IOReturn/permission messages.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "../../hid_descriptor.hpp"
#include "../input_diag.hpp"

namespace spn {

// ---- matching ---------------------------------------------------------------------------------

// Sorted, de-duplicated (vendor, product) pairs of all descriptions: one IOHIDManager matching
// dictionary per pair. Fallback criterion if a model is missing from devices/*.conf:
// usage page 0x01 (generic desktop) usage 0x08 (multi-axis controller) - not used by default
// because it would also match other multi-axis controllers.
constexpr unsigned kMacFallbackUsagePage = 0x01;
constexpr unsigned kMacFallbackUsage = 0x08;

inline std::vector<std::pair<unsigned, unsigned>> macMatchingPairs(const std::vector<HidDescriptor> &descs) {
    std::vector<std::pair<unsigned, unsigned>> v;
    for (const HidDescriptor &d : descs)
        for (const auto &id : d.ids) v.push_back(id);
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
    return v;
}

// ---- reports ----------------------------------------------------------------------------------

// The IOHID input-report callback passes the report id separately. Whether the report buffer
// also starts with the id byte for devices that use report ids is not verified without hardware,
// so both cases are handled: if reportId != 0 and the buffer already starts with that byte it is
// taken as is, otherwise the id is prepended. (Ambiguous only if a report without an id prefix
// has its first data byte equal to its report id; `forcePrepend` - env DOFSCROLL_MAC_PREPEND_ID=1 -
// resolves that for hardware debugging.) reportId 0 = device without report ids: the buffer is
// the payload, and byte 0 of the result is 0.
inline void macRawReport(uint32_t reportId, const uint8_t *data, size_t len, bool forcePrepend,
                         std::vector<uint8_t> *out) {
    out->clear();
    if (!data && len) return;
    const bool prefixed = !forcePrepend && reportId != 0 && len >= 1 && data[0] == static_cast<uint8_t>(reportId);
    if (!prefixed) out->push_back(static_cast<uint8_t>(reportId));
    out->insert(out->end(), data, data + len);
}

// Size of the per-device report buffer: the device's maximum input report size (+ the id byte),
// at least 64 bytes, at most 4096.
inline size_t macReportBufferSize(long maxInputReportSize) {
    if (maxInputReportSize < 0) maxInputReportSize = 0;
    const size_t n = static_cast<size_t>(maxInputReportSize) + 1;
    return std::min<size_t>(4096, std::max<size_t>(64, n));
}

// ---- IOReturn / permission ----------------------------------------------------------------------

constexpr int32_t kMacIoReturnSuccess = 0;
constexpr int32_t kMacIoReturnNotPermitted = static_cast<int32_t>(0xE00002E2u);      // kIOReturnNotPermitted
constexpr int32_t kMacIoReturnExclusiveAccess = static_cast<int32_t>(0xE00002C5u);   // kIOReturnExclusiveAccess
constexpr int32_t kMacIoReturnNotPrivileged = static_cast<int32_t>(0xE00002C1u);     // kIOReturnNotPrivileged

// IOHIDAccessType: 0 granted, 1 denied, 2 unknown (not asked yet).
enum class MacAccess { Granted, Denied, Unknown };
inline MacAccess macAccessFromInt(int v) {
    if (v == 0) return MacAccess::Granted;
    if (v == 1) return MacAccess::Denied;
    return MacAccess::Unknown;
}

inline std::string macPermissionMessage(MacAccess a) {
    switch (a) {
    case MacAccess::Granted: return "Input Monitoring permission granted";
    case MacAccess::Denied:
        return "Input Monitoring permission missing: enable dofscroll in System Settings > Privacy & Security > "
               "Input Monitoring, then restart it";
    case MacAccess::Unknown:
        return "Input Monitoring permission not decided yet: macOS asks once; allow dofscroll in System Settings > "
               "Privacy & Security > Input Monitoring";
    }
    return "";
}

inline std::string macOpenFailureMessage(int32_t ioReturn) {
    if (ioReturn == kMacIoReturnNotPermitted || ioReturn == kMacIoReturnNotPrivileged)
        return macPermissionMessage(MacAccess::Denied);
    if (ioReturn == kMacIoReturnExclusiveAccess)
        return inputIssueTextBlocked();
    char buf[48];
    std::snprintf(buf, sizeof buf, "IOHIDManagerOpen failed (IOReturn 0x%08X)", static_cast<unsigned>(ioReturn));
    return buf;
}

}  // namespace spn
