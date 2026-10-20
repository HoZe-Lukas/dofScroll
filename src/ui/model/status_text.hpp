// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Status messages never rely on colour alone (docs/ui.md, "Accessibility"): an error, a warning and a
// success message start with a symbol and keep their words ("Could not save: ...", "Invalid value: ...",
// "Warning: ...", "Saved"). Toolkit-free, so the rule is tested without a window.
//
//   withMark(StatusKind::Error, "Invalid value: x")   // "<error mark> Invalid value: x"
//   hasMark(text)                                      // starts with one of the marks
//   footerKind(text, isError, strings)                 // kind of a footer message (error / warning / success)
#pragma once
#include <string>

#include "ui/model/strings.hpp"

namespace spn::ui {

enum class StatusKind { Error, Warning, Success };

// Heavy multiplication x, warning sign, heavy check mark (UTF-8).
inline const char *statusMark(StatusKind k) {
    switch (k) {
        case StatusKind::Error: return "\xE2\x9C\x96";
        case StatusKind::Warning: return "\xE2\x9A\xA0";
        case StatusKind::Success: return "\xE2\x9C\x94";
    }
    return "";
}

inline std::string withMark(StatusKind k, const std::string &text) {
    if (text.empty()) return text;
    return std::string(statusMark(k)) + " " + text;
}

inline bool hasMark(const std::string &text) {
    for (StatusKind k : {StatusKind::Error, StatusKind::Warning, StatusKind::Success})
        if (text.rfind(statusMark(k), 0) == 0) return true;
    return false;
}

// What a footer message is: an error, a warning (a save that did not reach the daemon, a startup note about
// something not moved) or a success ("Saved", "Profile exported", ...).
inline StatusKind footerKind(const std::string &text, bool isError, const StringProvider &s) {
    if (isError) return StatusKind::Error;
    for (const char *warn : {"Saved, but the daemon was not notified: %1", "Not moved: "}) {
        std::string head = s.tr(warn);
        const size_t cut = head.find("%1");
        if (cut != std::string::npos) head.erase(cut);
        if (!head.empty() && text.rfind(head, 0) == 0) return StatusKind::Warning;
    }
    return StatusKind::Success;
}

}  // namespace spn::ui
