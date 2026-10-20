// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Documentation links, the filter and config-key hints of the pages, the "axis assistant applied"
// progress file and the text of the log viewer.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <string>

#include "testutil.hpp"
#include "ui/model/log_view_model.hpp"
#include "ui/model/ui_filter.hpp"
#include "ui/model/ui_links.hpp"
#include "ui/model/ui_progress.hpp"

using namespace spn;
using namespace spn::ui;

static void testLinks() {
    assert(docsUrl() == "https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/ui.md");
    assert(docsUrl("modes.md") == "https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md");
    // nothing but a plain Markdown file name is put into the address
    for (const char *bad : {"", ".md", "../x.md", "a/b.md", "a b.md", "x.md?y=1", "x.txt", "x.md#top", "a%2fb.md"})
        assert(docsUrl(bad) == docsUrl());
    assert(isOpenableUrl(docsUrl()));
    assert(!isOpenableUrl("") && !isOpenableUrl("http://example.org/x") && !isOpenableUrl("file:///etc/passwd") &&
           !isOpenableUrl("https://") && !isOpenableUrl("https://a b") && !isOpenableUrl("https://a\nb") &&
           !isOpenableUrl("javascript:alert(1)") && !isOpenableUrl("https://" + std::string(3000, 'a')));

    RecordingActionRunner runner;
    std::string err;
    assert(openDocs(runner, "ui.md", &err));
    assert(runner.calls.size() == 1 && runner.calls[0].kind == RecordingActionRunner::Kind::OpenUrl &&
           runner.calls[0].path == docsUrl());
    runner.result = false;
    assert(!openDocs(runner, "modes.md", &err) && err == "failed");
    assert(runner.count(RecordingActionRunner::Kind::OpenUrl) == 2);
}

static void testFilter() {
    assert(matchesFilter("", {"anything"}));
    assert(matchesFilter("  \t", {}));
    assert(matchesFilter("sens", {"Sensitivity", "0..255", "SCROLL_V_SENS"}));
    assert(matchesFilter("scroll_v_sens", {"Sensitivity", "0..255", "SCROLL_V_SENS"}));
    assert(matchesFilter("deadzone raw", {"Deadzone", "Raw sensor units", "SCROLL_V_DEADZONE"}));  // words in any field
    assert(!matchesFilter("deadzone smoothing", {"Deadzone", "Raw sensor units", "SCROLL_V_DEADZONE"}));
    assert(!matchesFilter("x", {}));
    // only ASCII is folded: a non-ASCII letter does not match its other case
    assert(matchesFilter("\xC3\xA4", {"\xC3\xA4x"}));
    assert(!matchesFilter("\xC3\x84", {"\xC3\xA4x"}));

    IdentityStrings id;
    assert(configKeyLine("SCROLL_V_SENS", id) == "Config key: SCROLL_V_SENS");
    assert(helpWithKey("0..255, 0 = channel off", "SCROLL_V_SENS", id) == "0..255, 0 = channel off\nConfig key: SCROLL_V_SENS");
    assert(helpWithKey("", "TICK_HZ", id) == "Config key: TICK_HZ");
}

static void testProgress() {
    const std::string dir = tu::makeTempDir("dofscroll-progress");
    const std::string conf = dir + "/dofscroll.conf";
    const std::string path = uiProgressPath(conf);
    assert(path == dir + "/ui-progress.conf");
    assert(!loadUiProgress(path).axisAssistantApplied);  // missing file
    std::string err;
    assert(markAxisAssistantApplied(path, &err));
    assert(loadUiProgress(path).axisAssistantApplied);
    assert(markAxisAssistantApplied(path, &err));  // again: stays
    assert(parseUiProgress(serializeUiProgress(UiProgress{true})) == UiProgress{true});
    assert(parseUiProgress(serializeUiProgress(UiProgress{false})) == UiProgress{false});
    // tolerant: comments, unknown keys, bad lines, spaces
    assert(parseUiProgress("# x\nfoo = 1\nnonsense\n  axis_assistant_applied =  1 \r\n").axisAssistantApplied);
    assert(!parseUiProgress("axis_assistant_applied = yes\n").axisAssistantApplied);
    // the interface settings file is a different file and is not touched
    assert(!fs::pathExists(dir + "/ui.conf"));
}

static void testLogTail() {
    // short content: whole, every line ends with a newline
    assert(logTail("a\nb", 100) == "a\nb\n");
    bool cut = false;
    assert(logTail("a\nb\n", 100, &cut) == "a\nb\n" && !cut);
    // too long: the first partial line is dropped, the rest is whole lines
    cut = false;
    assert(logTail("aaaa\nbbbb\ncccc\n", 8, &cut) == "cccc\n" && cut);
    cut = false;
    assert(logTail("aaaa\nbbbb\ncccc\n", 10, &cut) == "bbbb\ncccc\n" && cut);  // the cut falls on a line start
    assert(logTail("a\nbb\n", 0) == "");
    // control characters and escape sequences are made harmless, CRLF is tolerated
    assert(logTail("x\x1b[31m red\r\n", 100) == "x?[31m red\n");
    // an overlong line is shortened
    const std::string longLine = std::string(5000, 'x');
    const std::string t = logTail(longLine + "\n", 10000);
    assert(t.size() < 2100 && t.substr(t.size() - 4) == "...\n");
}

static void testReadLogView() {
    IdentityStrings id;
    const std::string dir = tu::makeTempDir("dofscroll-logview");
    LogView v = readLogView("", id);
    assert(!v.ok && !v.error.empty());
    v = readLogView(dir, id);  // no file yet
    assert(!v.ok && v.error == "There is no log file yet. The daemon writes it while it runs.");
    std::string content;
    for (int i = 0; i < 4000; ++i) content += "line " + std::to_string(i) + " some text of the daemon\n";
    std::ofstream(dir + "/dofscroll.log", std::ios::binary) << content;
    v = readLogView(dir, id);
    assert(v.ok && v.cut && v.text.size() <= kLogViewBytes && v.text.find("line 3999 ") != std::string::npos);
    assert(v.text.compare(0, 5, "line ") == 0);  // starts at a line
    assert(v.text.find("line 0 ") == std::string::npos);
    // a small file is shown whole
    std::ofstream(dir + "/dofscroll.log", std::ios::binary | std::ios::trunc) << "one\ntwo\n";
    v = readLogView(dir, id);
    assert(v.ok && !v.cut && v.text == "one\ntwo\n");
}

int main() {
    testLinks();
    testFilter();
    testProgress();
    testLogTail();
    testReadLogView();
    return 0;
}
