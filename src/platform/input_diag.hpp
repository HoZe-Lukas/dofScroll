// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Messages and decisions about a SpaceMouse that is found but cannot be read (pure, no OS headers,
// unit-tested on every OS: tests/test_input_diag.cpp).
//
// Two situations are told apart:
//   Blocked: the device exists, but opening it was refused (Windows sharing violation or access
//            denied, macOS kIOReturnExclusiveAccess): another program, possibly the official
//            3Dconnexion driver, holds it.
//   Silent:  the device opened, but no report arrived for a long time after it was found. An idle
//            SpaceMouse sends nothing, so this is only a hint ("move the cap"), never an error.
// dofscroll is meant to run next to the official driver. Whether both can read the device at the
// same time on a real Windows or macOS machine is not verified yet (docs/troubleshooting.md).
#pragma once

#include <cstdint>
#include <string>

namespace spn {

enum class InputIssue { Clear, Blocked, Silent };

// Value of the `input_issue=` line of the status file; "" for Clear.
inline const char *inputIssueKey(InputIssue i) {
    switch (i) {
    case InputIssue::Blocked: return "blocked";
    case InputIssue::Silent: return "silent";
    case InputIssue::Clear: break;
    }
    return "";
}

inline InputIssue inputIssueFromKey(const std::string &k) {
    if (k == "blocked") return InputIssue::Blocked;
    if (k == "silent") return InputIssue::Silent;
    return InputIssue::Clear;
}

// English texts (the UI translates them through gettext; the daemon log uses them as they are).
inline const char *inputIssueTextBlocked() {
    return "SpaceMouse found but cannot be read: another program, possibly the 3Dconnexion driver, holds it "
           "exclusively. See docs/troubleshooting.md, section \"The official 3Dconnexion driver and dofscroll\"";
}
inline const char *inputIssueTextSilent() {
    return "No input from the SpaceMouse yet. Move the cap. If nothing arrives, another program, possibly the "
           "3Dconnexion driver, may be holding the device. See docs/troubleshooting.md, section \"The official "
           "3Dconnexion driver and dofscroll\"";
}
inline std::string inputIssueText(InputIssue i) {
    switch (i) {
    case InputIssue::Blocked: return inputIssueTextBlocked();
    case InputIssue::Silent: return inputIssueTextSilent();
    case InputIssue::Clear: break;
    }
    return "";
}

// Windows: GetLastError() of CreateFileW on a HID interface. 32 = ERROR_SHARING_VIOLATION,
// 5 = ERROR_ACCESS_DENIED.
constexpr unsigned kWinErrorAccessDenied = 5;
constexpr unsigned kWinErrorSharingViolation = 32;
inline bool winOpenErrorMeansBlocked(unsigned e) { return e == kWinErrorSharingViolation || e == kWinErrorAccessDenied; }

// Log line for a HID interface that could not be opened on Windows.
inline std::string winOpenFailureMessage(unsigned e) {
    if (winOpenErrorMeansBlocked(e))
        return std::string("cannot open (error ") + std::to_string(e) + "): " + inputIssueTextBlocked();
    return std::string("cannot open (error ") + std::to_string(e) + ")";
}

// How long a HID source may be connected to a device without a single event before the
// hint is shown.
constexpr int64_t kSilentInputNs = 45'000'000'000;  // 45 s

// Only the raw HID sources ("hid" on Windows, "iokit" on macOS) can compete with a driver for the
// device. spacenavd and the X11 path never report an issue.
inline bool inputSourceCanBeBlocked(const std::string &sourceName) {
    return sourceName == "hid" || sourceName == "iokit";
}

// The issue to report now. `blocked`: the source says a device was found but refused to open.
inline InputIssue decideInputIssue(const std::string &sourceName, bool blocked, int devicesSeen,
                                   uint64_t eventsSinceConnect, int64_t nsSinceConnect) {
    if (!inputSourceCanBeBlocked(sourceName)) return InputIssue::Clear;
    if (blocked) return InputIssue::Blocked;
    if (devicesSeen > 0 && eventsSinceConnect == 0 && nsSinceConnect >= kSilentInputNs) return InputIssue::Silent;
    return InputIssue::Clear;
}

}  // namespace spn
