// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure rules of the Windows and macOS running-programs providers (no OS headers, unit-tested on
// every OS in tests/test_running_apps_os.cpp). The OS calls live in
// platform/win/running_apps_win.cpp and platform/mac/running_apps_mac.mm.
#pragma once
#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "platform.hpp"
#include "../utf8.hpp"
#include "running_apps.hpp"
#include "running_apps_util.hpp"
#include "win/win_paths.hpp"

namespace spn {

constexpr size_t kMaxAppTitleLen = 512;  // bytes of UTF-8, same bound as the X11 provider

// Cuts s to at most max bytes without splitting a UTF-8 sequence.
inline std::string truncateUtf8(std::string s, size_t max = kMaxAppTitleLen) {
    if (s.size() <= max) return s;
    size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) n--;
    s.resize(n);
    return s;
}

// Windows: matchString of a process, the same rule as the foreground provider (exeNameLower of the
// image path). "" when the path is empty or the process is the UWP frame host (the foreground
// provider reports "" for it, so such a window cannot be matched and is not listed).
inline std::string winMatchString(const std::string &imagePath) {
    if (imagePath.empty()) return std::string();
    const std::string exe = exeNameLower(imagePath);
    if (isUwpHostExe(exe)) return std::string();
    return exe;
}

// Windows: display name = file description from the version resource when it is non-empty,
// otherwise the file name without extension of the (original case) image path.
inline std::string winDisplayName(const std::string &fileDescription, const std::string &imagePath) {
    std::string d = fileDescription;
    while (!d.empty() && utf8::isSpace(d.back())) d.pop_back();
    size_t b = 0;
    while (b < d.size() && utf8::isSpace(d[b])) b++;
    d.erase(0, b);
    if (!d.empty()) return truncateUtf8(d, 128);
    const size_t cut = imagePath.find_last_of("/\\");
    std::string n = cut == std::string::npos ? imagePath : imagePath.substr(cut + 1);
    const size_t dot = n.find_last_of('.');
    if (dot != std::string::npos && dot > 0) n.resize(dot);
    return truncateUtf8(n, 128);
}

// Windows: window selection rules (flags come from the OS call).
struct WinWindowFlags {
    bool visible = false;
    bool toolWindow = false;  // WS_EX_TOOLWINDOW without WS_EX_APPWINDOW
    bool cloaked = false;     // DWMWA_CLOAKED (suspended UWP frames, other virtual desktops)
    bool hasTitle = false;
};
inline bool winWindowListable(const WinWindowFlags &f) {
    return f.visible && !f.toolWindow && !f.cloaked && f.hasTitle;
}

// macOS: matchString exactly as mac_activewin.mm (bundle identifier, else localized name, bytes
// lowercased, ASCII letters only; the macOS provider hands over NFC text). "" when both are empty.
inline std::string macMatchString(const std::string &bundleId, const std::string &localizedName) {
    std::string s = bundleId.empty() ? localizedName : bundleId;
    return utf8::lowerAscii(std::move(s));  // ASCII letters only; bytes >= 0x80 are untouched
}

// Device list for the DevicePickerModel: the snapshot of an opened input backend (the same
// DeviceInfo records the daemon uses). Devices without a name are dropped.
inline std::vector<DeviceInfo> deviceListFromSnapshot(const std::vector<DeviceInfo> &snap) {
    std::vector<DeviceInfo> out;
    for (const DeviceInfo &d : snap)
        if (!d.name.empty()) out.push_back(d);
    return out;
}

// Opens the platform input backend ("auto"), takes its device snapshot and closes it again (only
// meaningful on Windows/macOS where the backend is linked in; on Linux use the UI's own source).
inline std::vector<DeviceInfo> listInputDevices(std::string *err) {
    std::string log;
    std::unique_ptr<InputSource> in = makeInputSource("auto", &log);
    if (!in) {
        if (err) *err = log.empty() ? "no input backend" : log;
        return {};
    }
    std::string e;
    if (!in->open(&e)) {
        if (err) *err = e;
        return {};
    }
    std::vector<DeviceInfo> d = deviceListFromSnapshot(in->devices());
    in->close();
    return d;
}

}  // namespace spn
