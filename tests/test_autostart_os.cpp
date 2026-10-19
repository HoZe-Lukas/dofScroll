// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/platform/autostart_os.hpp (portable, assert-based, own main).
#include <cassert>
#include <cstdio>
#include <string>

#include "platform/autostart_os.hpp"
#include "testutil.hpp"

using namespace spn::autostart;

static Entry E(const char *n, Entry::Kind k, bool opt = false, const char *why = "") {
    Entry e;
    e.name = n;
    e.kind = k;
    e.optional = opt;
    e.why = why;
    return e;
}

int main() {
    std::string d;
    // state combination
    assert(combineEntries({E("a", Entry::Present), E("b", Entry::Present)}, &d) == AutostartState::Enabled);
    assert(combineEntries({E("a", Entry::Absent), E("b", Entry::Absent)}, &d) == AutostartState::Disabled);
    assert(combineEntries({E("a", Entry::Present), E("b", Entry::Absent)}, &d) == AutostartState::Partial);
    assert(combineEntries({E("a", Entry::Present), E("b", Entry::Different)}, &d) == AutostartState::Partial);
    assert(d.find("b points to a different program") == 0);
    assert(combineEntries({E("a", Entry::Different), E("b", Entry::Different)}, &d) == AutostartState::Partial);
    // optional UI missing is ignored, a missing daemon is not
    assert(combineEntries({E("a", Entry::Present), E("ui", Entry::Missing, true)}, &d) == AutostartState::Enabled);
    assert(combineEntries({E("a", Entry::Absent), E("ui", Entry::Missing, true)}, &d) == AutostartState::Disabled);
    assert(combineEntries({E("a", Entry::Missing), E("ui", Entry::Present, true)}, &d) == AutostartState::Unavailable);
    assert(d == "a is not installed");
    assert(combineEntries({E("a", Entry::Missing, true)}, &d) == AutostartState::Unavailable);
    assert(combineEntries({}, &d) == AutostartState::Unavailable);
    assert(combineEntries({E("a", Entry::Present), E("b", Entry::Blocked, false, "masked")}, &d) == AutostartState::Unavailable);
    assert(d == "b: masked");
    assert(combineEntries({E("a", Entry::Failed, false, "boom"), E("b", Entry::Present)}, &d) == AutostartState::Error);
    assert(d == "a: boom");
    assert(combineEntries({E("a", Entry::Present)}, nullptr) == AutostartState::Enabled);

    assert(autostartUi(AutostartState::Enabled).checked && autostartUi(AutostartState::Enabled).can_toggle);
    assert(!autostartUi(AutostartState::Partial).checked && autostartUi(AutostartState::Partial).can_toggle);
    assert(!autostartUi(AutostartState::Disabled).checked && autostartUi(AutostartState::Disabled).can_toggle);
    assert(!autostartUi(AutostartState::Unavailable).can_toggle && !autostartUi(AutostartState::Error).can_toggle);

    // Run-key value quoting: spaces and non-ASCII (UTF-8) stay, quotes and relative paths are refused
    std::string v, err;
    assert(quoteRunValue("C:\\Program Files\\dofscroll\\dofscroll.exe", &v));
    assert(v == "\"C:\\Program Files\\dofscroll\\dofscroll.exe\"");
    assert(unquoteRunValue(v) == "C:\\Program Files\\dofscroll\\dofscroll.exe");
    assert(quoteRunValue("D:\\Pr\xC3\xB6gr\xC3\xA4mmchen\\dofscroll-ui.exe", &v));
    assert(v == "\"D:\\Pr\xC3\xB6gr\xC3\xA4mmchen\\dofscroll-ui.exe\"");
    assert(quoteRunValue("\\\\server\\share\\dofscroll.exe", &v));
    assert(!quoteRunValue("", &v, &err) && err == "empty path");
    assert(!quoteRunValue("dofscroll.exe", &v, &err) && err == "path is not absolute");
    assert(!quoteRunValue("C:dofscroll.exe", &v));
    assert(!quoteRunValue("C:\\a\"b\\x.exe", &v, &err) && err == "unsupported character in path");
    assert(!quoteRunValue("C:\\a\nb\\x.exe", &v));
    assert(unquoteRunValue("C:\\x.exe").empty());
    assert(unquoteRunValue("\"C:\\x.exe\" --flag").empty());
    assert(unquoteRunValue("\"\"").empty());

    // plist: escaping and structure
    assert(xmlEscape("a&b<c>\"d'") == "a&amp;b&lt;c&gt;&quot;d&apos;");
    assert(plistPathOk("/Applications/dofscroll/dofscroll"));
    assert(!plistPathOk("relative/x") && !plistPathOk("") && !plistPathOk("/a\nb"));
    const std::string daemon = renderLaunchAgentPlist(kMacDaemonLabel, "/Users/a&b/<x>/dofscroll", true);
    assert(daemon.find("<string>io.github.dofscroll.daemon</string>") != std::string::npos);
    assert(daemon.find("<string>/Users/a&amp;b/&lt;x&gt;/dofscroll</string>") != std::string::npos);
    assert(daemon.find("a&b") == std::string::npos);
    assert(daemon.find("<key>KeepAlive</key>") != std::string::npos);
    assert(daemon.find("<key>RunAtLoad</key>") != std::string::npos);
    assert(daemon.find("<string>Aqua</string>") != std::string::npos);
    const std::string ui = renderLaunchAgentPlist(kMacUiLabel, "/Applications/Dof Scroll/dofscroll-ui", false);
    assert(ui.find("<string>io.github.dofscroll.ui</string>") != std::string::npos);
    assert(ui.find("<string>/Applications/Dof Scroll/dofscroll-ui</string>") != std::string::npos);
    assert(ui.find("KeepAlive") == std::string::npos && ui.find("/dev/null") == std::string::npos);
    assert(ui.compare(0, 5, "<?xml") == 0 && ui.back() == '\n');

    // launchctl and systemctl argument vectors (one element per argument, no shell)
    using V = std::vector<std::string>;
    assert((launchctlBootstrapArgv(501, "/Users/x y/Library/LaunchAgents/l.plist") ==
            V{"launchctl", "bootstrap", "gui/501", "/Users/x y/Library/LaunchAgents/l.plist"}));
    assert((launchctlBootoutArgv(501, "io.github.dofscroll.ui") == V{"launchctl", "bootout", "gui/501/io.github.dofscroll.ui"}));
    assert((launchctlPrintArgv(0, "l") == V{"launchctl", "print", "gui/0/l"}));
    assert((systemctlArgv("enable", {kLinuxDaemonUnit, kLinuxUiUnit}) ==
            V{"systemctl", "--user", "enable", "dofscroll.service", "dofscroll-ui.service"}));
    assert((systemctlArgv("disable", {kLinuxDaemonUnit}) == V{"systemctl", "--user", "disable", "dofscroll.service"}));

    // wait until gone: succeeds as soon as the job is gone, gives up after maxTries sleeps
    int polls = 0, sleeps = 0;
    const bool gone = waitUntilGone([&] { return ++polls < 4; }, [&] { sleeps++; });
    assert(gone);
    assert(polls == 4 && sleeps == 3);
    polls = sleeps = 0;
    const bool never = waitUntilGone([&] { polls++; return true; }, [&] { sleeps++; }, 5);
    assert(!never);
    assert(sleeps == 5 && polls == 6);
    assert(waitUntilGone([] { return false; }, [] { assert(false); }));

    // systemctl is-enabled answers -> entries
    assert(entryFromIsEnabled("dofscroll.service", "enabled\n", "", false).kind == Entry::Present);
    assert(entryFromIsEnabled("dofscroll.service", "disabled\n", "", false).kind == Entry::Absent);
    assert(entryFromIsEnabled("dofscroll.service", "linked\n", "", false).kind == Entry::Absent);
    assert(entryFromIsEnabled("dofscroll.service", "masked\n", "", false).kind == Entry::Blocked);
    assert(entryFromIsEnabled("dofscroll.service", "static\n", "", false).kind == Entry::Blocked);
    assert(entryFromIsEnabled("dofscroll.service", "enabled-runtime\n", "", false).kind == Entry::Blocked);
    assert(entryFromIsEnabled("dofscroll-ui.service", "", "Failed to get unit file state: No such file or directory", true).kind == Entry::Missing);
    assert(entryFromIsEnabled("x", "not-found\n", "", true).kind == Entry::Missing);
    assert(entryFromIsEnabled("x", "garbage\n", "", false).kind == Entry::Failed);
    assert(entryFromIsEnabled("x", "", "", false).kind == Entry::Failed);
    // Linux combination: UI unit not installed -> daemon decides; mix -> Partial
    assert(combineEntries({entryFromIsEnabled("d", "enabled\n", "", false),
                           entryFromIsEnabled("u", "", "No such file", true)}, &d) == AutostartState::Enabled);
    assert(combineEntries({entryFromIsEnabled("d", "enabled\n", "", false),
                           entryFromIsEnabled("u", "disabled\n", "", true)}, &d) == AutostartState::Partial);

    std::printf("test_autostart_os: ok\n");
    return 0;
}
