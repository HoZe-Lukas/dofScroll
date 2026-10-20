// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Status messages never rely on colour alone: every kind has its own symbol, a message gets it in front of
// its words, and the footer classifies its messages (error, warning, success).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

#include "ui/model/autosave.hpp"
#include "ui/model/status_text.hpp"

using namespace spn::ui;

int main() {
    IdentityStrings id;
    // three different symbols
    std::set<std::string> marks = {statusMark(StatusKind::Error), statusMark(StatusKind::Warning), statusMark(StatusKind::Success)};
    assert(marks.size() == 3);
    for (const std::string &m : marks) assert(!m.empty());

    const std::string t = withMark(StatusKind::Error, "Invalid value: x");
    assert(t.rfind(statusMark(StatusKind::Error), 0) == 0 && t.find("Invalid value: x") != std::string::npos && hasMark(t));
    assert(!hasMark("Invalid value: x") && withMark(StatusKind::Success, "").empty());

    // the footer
    assert(footerKind("Could not save: disk full", true, id) == StatusKind::Error);
    assert(footerKind("Saved", false, id) == StatusKind::Success);
    assert(footerKind("Profile exported", false, id) == StatusKind::Success);
    assert(footerKind("Saved, but the daemon was not notified: no pid", false, id) == StatusKind::Warning);
    assert(footerKind("Not moved: a.conf", false, id) == StatusKind::Warning);

    // every text the autosave rule produces is classified as it is meant
    Autosave a([] { return true; }, [] { return Autosave::Outcome{true, false, "no daemon"}; });
    a.request(Autosave::Urgency::Immediate, 0);
    a.flush(1);
    const std::string notified = a.statusText(2, id);
    assert(!notified.empty() && footerKind(notified, false, id) == StatusKind::Warning);
    Autosave b([] { return true; }, [] { return Autosave::Outcome{false, true, "disk full"}; });
    b.request(Autosave::Urgency::Immediate, 0);
    b.flush(1);
    assert(footerKind(b.statusText(2, id), true, id) == StatusKind::Error);
    std::puts("test_ui_status_text: ok");
    return 0;
}
