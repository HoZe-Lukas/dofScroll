// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Messages and decisions of platform/input_diag.hpp and their display in the status model.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "platform/input_diag.hpp"
#include "ui/model/overview_model.hpp"
#include "ui/model/status_model.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
struct Id : StringProvider {
    std::string tr(const char *s) const override { return s; }
};
bool has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }
}  // namespace

int main() {
    // keys round trip
    assert(inputIssueFromKey("blocked") == InputIssue::Blocked && inputIssueFromKey("silent") == InputIssue::Silent);
    assert(inputIssueFromKey("") == InputIssue::Clear && inputIssueFromKey("x") == InputIssue::Clear);
    assert(std::string(inputIssueKey(InputIssue::Blocked)) == "blocked" && std::string(inputIssueKey(InputIssue::Clear)).empty());

    // the texts are actionable and never advise stopping the driver
    for (InputIssue i : {InputIssue::Blocked, InputIssue::Silent}) {
        const std::string t = inputIssueText(i);
        assert(has(t, "3Dconnexion driver") && has(t, "docs/troubleshooting.md"));
        assert(has(t, "The official 3Dconnexion driver and dofscroll"));
        assert(!has(t, "quit") && !has(t, "stop the") && !has(t, "exit the"));
    }
    assert(has(inputIssueText(InputIssue::Blocked), "found but cannot be read"));
    assert(has(inputIssueText(InputIssue::Blocked), "exclusively"));
    assert(inputIssueText(InputIssue::Clear).empty());

    // Windows CreateFileW errors
    assert(winOpenErrorMeansBlocked(32) && winOpenErrorMeansBlocked(5) && !winOpenErrorMeansBlocked(2));
    assert(has(winOpenFailureMessage(32), "error 32") && has(winOpenFailureMessage(32), "holds it exclusively"));
    assert(winOpenFailureMessage(2) == "cannot open (error 2)");

    // the decision
    assert(!inputSourceCanBeBlocked("spacenavd") && inputSourceCanBeBlocked("hid") && inputSourceCanBeBlocked("iokit"));
    assert(decideInputIssue("spacenavd", true, 1, 0, kSilentInputNs * 2) == InputIssue::Clear);
    assert(decideInputIssue("hid", true, 0, 0, 0) == InputIssue::Blocked);
    assert(decideInputIssue("iokit", true, 1, 5, 0) == InputIssue::Blocked);
    assert(decideInputIssue("hid", false, 1, 0, kSilentInputNs - 1) == InputIssue::Clear);
    assert(decideInputIssue("hid", false, 1, 0, kSilentInputNs) == InputIssue::Silent);
    assert(decideInputIssue("hid", false, 1, 1, kSilentInputNs * 3) == InputIssue::Clear);  // it did send something
    assert(decideInputIssue("hid", false, 0, 0, kSilentInputNs * 3) == InputIssue::Clear);  // no device at all

    // status file -> UI line (the English literals in status_model.hpp equal the daemon texts)
    const Id id;
    StatusModel m;
    m.update("device=A\ndevices_seen=1\ninput_issue=blocked\nupdated=5\n", true, 5);
    assert(m.status().input_issue == InputIssue::Blocked);
    assert(m.warningLine(id) == inputIssueText(InputIssue::Blocked));
    m.update("device=A\ndevices_seen=1\ninput_issue=silent\nupdated=5\n", true, 5);
    assert(m.warningLine(id) == inputIssueText(InputIssue::Silent));
    // it wins over the several-devices hint
    m.update("device=A\ndevices_seen=2\ninput_issue=blocked\nupdated=5\n", true, 5);
    assert(m.warningLine(id) == inputIssueText(InputIssue::Blocked));
    // unknown value or an old daemon: nothing
    m.update("device=A\ndevices_seen=1\ninput_issue=zzz\nupdated=5\n", true, 5);
    assert(m.warningLine(id).empty());
    // a stale file shows nothing
    m.update("input_issue=blocked\nupdated=5\n", true, 500);
    assert(m.warningLine(id).empty());

    // the Overview carries it
    SettingsModel sm;
    m.update("device=A\ndevices_seen=1\ninput_issue=blocked\nupdated=5\n", true, 5);
    const OverviewData d = buildOverview(sm, m, id);
    assert(d.warningLine == inputIssueText(InputIssue::Blocked));

    std::puts("input diag tests passed");
    return 0;
}
