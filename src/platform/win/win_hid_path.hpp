// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure helpers of the Windows raw-HID input backend (no Windows headers, unit-tested on every OS):
// vendor/product id from a HID device interface path, and the set difference used by the
// periodic rescan that implements hot-plug.
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace spn {

// Lower-cases ASCII only (device paths are compared case-insensitively on Windows).
inline std::string hidPathKey(std::string p) {
    for (char &c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return p;
}

namespace detail {
inline bool hex4(const std::string &s, size_t pos, unsigned *out) {
    if (pos + 4 > s.size()) return false;
    unsigned v = 0;
    for (size_t i = pos; i < pos + 4; ++i) {
        const char c = s[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') d = static_cast<unsigned>(c - 'a' + 10);
        else return false;
        v = v * 16 + d;
    }
    *out = v;
    return true;
}
}  // namespace detail

// Parses "\\?\hid#vid_256f&pid_c635&mi_00#7&2a3b&0&0000#{guid}" as returned by
// SetupDiGetDeviceInterfaceDetail (the "\\.\" prefix is not accepted). The vid/pid block must be
// exactly "vid_XXXX&pid_XXXX" with four hex digits each, directly after "hid#", followed by '&' or
// '#'. Returns false for anything else and then leaves the outputs untouched.
inline bool parseHidPath(const std::string &path, unsigned *vid, unsigned *pid) {
    const std::string p = hidPathKey(path);
    static const std::string kPrefix = "\\\\?\\hid#";
    if (p.compare(0, kPrefix.size(), kPrefix) != 0) return false;
    size_t i = kPrefix.size();
    if (p.compare(i, 4, "vid_") != 0) return false;
    unsigned v = 0, d = 0;
    if (!detail::hex4(p, i + 4, &v)) return false;
    i += 8;
    if (p.compare(i, 5, "&pid_") != 0) return false;
    if (!detail::hex4(p, i + 5, &d)) return false;
    i += 9;
    if (i >= p.size() || (p[i] != '&' && p[i] != '#')) return false;
    if (vid) *vid = v;
    if (pid) *pid = d;
    return true;
}

struct PathDiff {
    std::vector<std::string> added;    // present now, not open yet
    std::vector<std::string> removed;  // open, no longer present
};

// Compares the paths of the open devices with the paths found by a rescan (case-insensitive).
// The returned strings are the originals from `present` (added) and `open` (removed).
inline PathDiff diffPaths(const std::vector<std::string> &open, const std::vector<std::string> &present) {
    PathDiff d;
    auto has = [](const std::vector<std::string> &v, const std::string &key) {
        return std::any_of(v.begin(), v.end(), [&](const std::string &s) { return hidPathKey(s) == key; });
    };
    for (const std::string &p : present) {
        const std::string k = hidPathKey(p);
        if (!has(open, k) && !has(d.added, k)) d.added.push_back(p);
    }
    for (const std::string &p : open)
        if (!has(present, hidPathKey(p))) d.removed.push_back(p);
    return d;
}

// One HID interface (top-level collection) of an attached device with a known vid/pid.
struct HidCandidate {
    std::string path;
    std::string container;  // Windows container id of the physical device ("" = unknown)
    unsigned usagePage = 0;
    unsigned usage = 0;
};

// Usages that carry standard keyboard/mouse/consumer reports, never the 6-DoF reports described by
// devices/*.conf (what the OS delivers through the other interfaces of the same physical device).
inline bool isIgnoredHidUsage(unsigned page, unsigned usage) {
    if (page == 0x0C) return true;                                    // consumer control
    if (page == 0x01 && (usage == 0x02 || usage == 0x06 || usage == 0x07 || usage == 0x80)) return true;
    return false;                                                     // mouse, keyboard, keypad, system control
}

// A physical device can expose several HID interfaces; the core wants exactly one DeviceInfo per
// physical device. From `cands` picks at most one interface per container (candidates without a
// container id are each their own device), preferring generic-desktop "multi-axis controller"
// (page 0x01, usage 0x08), then any other non-ignored usage, then the smaller path. Containers in
// `busy` (already open) get nothing. Returns indices into `cands`, ordered by path.
// Which usage the individual models really use is unverified without hardware.
inline std::vector<size_t> pickInterfaces(const std::vector<HidCandidate> &cands,
                                          const std::vector<std::string> &busy) {
    auto rank = [](const HidCandidate &c) { return (c.usagePage == 0x01 && c.usage == 0x08) ? 0 : 1; };
    std::vector<size_t> order;
    for (size_t i = 0; i < cands.size(); ++i)
        if (!isIgnoredHidUsage(cands[i].usagePage, cands[i].usage)) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (rank(cands[a]) != rank(cands[b])) return rank(cands[a]) < rank(cands[b]);
        return hidPathKey(cands[a].path) < hidPathKey(cands[b].path);
    });
    std::vector<size_t> picked;
    std::vector<std::string> seen = busy;
    for (size_t i : order) {
        const std::string key = hidPathKey(cands[i].container);
        if (!key.empty()) {
            if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
            seen.push_back(key);
        }
        picked.push_back(i);
    }
    std::sort(picked.begin(), picked.end(), [&](size_t a, size_t b) {
        return hidPathKey(cands[a].path) < hidPathKey(cands[b].path);
    });
    return picked;
}

}  // namespace spn
