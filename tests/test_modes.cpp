// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tests for src/modes.cpp - plain assert tests, no framework.
#undef NDEBUG
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#include "config.hpp"
#include "modes.hpp"

using namespace spn;

static std::string g_dir;

static void writeFile(const std::string &name, const std::string &content) {
    std::ofstream f(g_dir + "/" + name, std::ios::binary);
    f << content;
}

static void clearDir() {
    assert(system(("rm -rf '" + g_dir + "' && mkdir -p '" + g_dir + "'").c_str()) == 0);
}

static bool hasWarning(const std::vector<std::string> &w, const std::string &needle) {
    for (const std::string &s : w)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

static void testIds() {
    assert(validModeId("timeline") && validModeId("a_b-9") && validModeId(std::string(32, 'a')));
    assert(!validModeId("") && !validModeId(std::string(33, 'a')) && !validModeId("Upper") &&
           !validModeId("a b") && !validModeId("../x") && !validModeId("a.b"));
}

static void testGrammar() {
    std::string err;
    ChannelAction a;
    assert(parseChannelAction("default", &a, &err) && a.type == ChannelActionType::Default);
    assert(parseChannelAction(" NONE ", &a, &err) && a.type == ChannelActionType::None);
    assert(parseChannelAction("wheel_v", &a, &err) && a.type == ChannelActionType::WheelV);
    assert(parseChannelAction("wheel_h", &a, &err) && a.type == ChannelActionType::WheelH);
    assert(parseChannelAction("zoom", &a, &err) && a.type == ChannelActionType::Zoom);
    assert(parseChannelAction("keys:left,right", &a, &err) && a.type == ChannelActionType::Keys &&
           a.neg == Key::Left && a.pos == Key::Right);
    assert(parseChannelAction("Keys: PageUp , pagedown", &a, &err) && a.neg == Key::PageUp && a.pos == Key::PageDown);
    assert(parseChannelAction("keys:left,right", nullptr, nullptr));
    for (const char *bad : {"", "scroll", "keys:", "keys:left", "keys:left,", "keys:,right", "keys:left,right,up",
                            "keys:left,nonsense", "keys:nonsense,left", "wheel", "zoom:1"}) {
        err.clear();
        assert(!parseChannelAction(bad, &a, &err) && !err.empty());
    }
    assert(ChannelAction() == ChannelAction());

    ButtonActionType t;
    Key k;
    assert(parseButtonAction("none", &t, &k, &err) && t == ButtonActionType::None);
    assert(parseButtonAction("click", &t, &k, &err) && t == ButtonActionType::Click);
    assert(parseButtonAction("RightClick", &t, &k, &err) && t == ButtonActionType::RightClick);
    assert(parseButtonAction("middleclick", &t, &k, &err) && t == ButtonActionType::MiddleClick);
    assert(parseButtonAction("nav", &t, &k, &err) && t == ButtonActionType::Nav);
    assert(parseButtonAction("key:space", &t, &k, &err) && t == ButtonActionType::Key && k == Key::Space);
    assert(parseButtonAction(" key: F5 ", &t, &k, &err) && k == Key::F5);
    for (const char *bad : {"", "press", "key:", "key:nonsense", "keys:a,b", "click now", "key"}) {
        err.clear();
        assert(!parseButtonAction(bad, &t, &k, &err) && !err.empty());
    }
}

static void testBuiltins() {
    std::vector<std::string> w;
    ModeSet none = loadModes(g_dir + "/does-not-exist", &w);
    assert(w.empty());  // a missing directory is not an error
    assert(modesStamp(g_dir + "/does-not-exist").empty());
    assert(none.modes.size() == 3 && none.normal().id == "normal" && none.normal().builtin);
    assert(none.normal().allDefaultChannels() && none.normal().buttons.empty());
    const ModeDef *off = none.find("off");
    assert(off && off->v.type == ChannelActionType::None && off->h.type == ChannelActionType::None &&
           off->twist.type == ChannelActionType::None);
    const ModeDef *tl = none.find("timeline");
    assert(tl && tl->v.type == ChannelActionType::None);
    assert(tl->h.type == ChannelActionType::Keys && tl->h.neg == Key::Left && tl->h.pos == Key::Right);
    assert(tl->twist == tl->h);
    assert(!none.find("nope"));
    assert(!modesDir().empty() && modesDir().find("/dofscroll/modes") != std::string::npos);
}

static void testParser() {
    clearDir();
    writeFile("resolve.conf",
              "\xEF\xBB\xBF# comment\n"
              "; another\n"
              "NAME = DaVinci Resolve   # trailing comment\n"
              "DESCRIPTION = Edit page\n"
              "V_ACTION = none\n"
              "H_ACTION = keys:left,right\n"
              "twist_action = keys:Left,Right\n"
              "BUTTON_0 = key:space\n"
              "BUTTON_1 = click\n"
              "BUTTON_1 = rightclick\n"  // later wins
              "SCROLL_H_SENS = 4\n"
              "zoom_sens = 7 # note\n");
    std::vector<std::string> w;
    ModeSet ms = loadModes(g_dir, &w);
    assert(w.empty());
    const ModeDef *m = ms.find("resolve");
    assert(m && !m->builtin && m->label == "DaVinci Resolve" && m->description == "Edit page");
    assert(m->v.type == ChannelActionType::None && m->h.type == ChannelActionType::Keys);
    assert(m->twist.neg == Key::Left && m->twist.pos == Key::Right);
    assert(m->buttons.size() == 2);
    assert(m->buttons[0].button == 0 && m->buttons[0].type == ButtonActionType::Key && m->buttons[0].key == Key::Space);
    assert(m->buttons[1].button == 1 && m->buttons[1].type == ButtonActionType::RightClick);
    assert(m->overrides.size() == 2 && m->overrides[0].first == "SCROLL_H_SENS" && m->overrides[0].second == "4");
    assert(m->overrides[1].first == "ZOOM_SENS" && m->overrides[1].second == "7");
    assert(!m->allDefaultChannels());
    assert(ms.modes.size() == 4);

    // label falls back to the id; unlisted names are ignored
    clearDir();
    writeFile("plain.conf", "V_ACTION = wheel_v\n");
    writeFile("Bad Name.conf", "V_ACTION = none\n");
    writeFile("UPPER.conf", "V_ACTION = none\n");
    writeFile("readme.txt", "V_ACTION = none\n");
    writeFile(".conf", "V_ACTION = none\n");
    ms = loadModes(g_dir, &w);
    assert(ms.modes.size() == 4 && ms.find("plain") && ms.find("plain")->label == "plain");

    // a file named like a built-in replaces it (normal stays at index 0)
    clearDir();
    writeFile("normal.conf", "NAME = My normal\nBUTTON_2 = nav\n");
    writeFile("timeline.conf", "H_ACTION = zoom\n");
    ms = loadModes(g_dir, &w);
    assert(ms.modes.size() == 3 && ms.normal().id == "normal" && !ms.normal().builtin);
    assert(ms.normal().label == "My normal" && ms.normal().buttons.size() == 1);
    assert(ms.find("timeline")->h.type == ChannelActionType::Zoom && ms.find("timeline")->v.type == ChannelActionType::Default);
}

static void testGrammarErrors() {
    clearDir();
    writeFile("bad.conf",
              "V_ACTION = flying\n"           // 1: invalid action
              "H_ACTION = keys:left\n"        // 2
              "BUTTON_0 = key:nonsense\n"     // 3
              "BUTTON_32 = click\n"           // 4: index out of range
              "BUTTON_x = click\n"            // 5
              "BUTTON_ = click\n"             // 6
              "NO_SUCH_KEY = 1\n"             // 7
              "SCROLL_V_SIGN = 5\n"           // 8: invalid value
              "MODE = timeline\n"             // 9: forbidden in mode files
              "[section]\n"                   // 10
              "no equals sign\n"              // 11
              " = value\n"                    // 12
              "NAME = ok\x01\x02name\n"       // control characters are dropped
              "TWIST_ACTION = zoom\n");
    std::vector<std::string> w;
    ModeSet ms = loadModes(g_dir, &w);
    const ModeDef *m = ms.find("bad");
    assert(m);  // the file survives, bad lines are skipped
    assert(w.size() == 12);
    assert(hasWarning(w, "bad.conf:1:") && hasWarning(w, "bad.conf:9:") && hasWarning(w, "MODE"));
    assert(m->v.type == ChannelActionType::Default && m->h.type == ChannelActionType::Default);
    assert(m->twist.type == ChannelActionType::Zoom);
    assert(m->buttons.empty() && m->overrides.empty());
    assert(m->label == "okname");

    // LF-less last line and CRLF line endings
    clearDir();
    writeFile("crlf.conf", "V_ACTION = none\r\nH_ACTION = wheel_h");
    ms = loadModes(g_dir, &w);
    assert(ms.find("crlf")->v.type == ChannelActionType::None && ms.find("crlf")->h.type == ChannelActionType::WheelH);
}

static void testLimits() {
    // > 64 files: only the first 64 (sorted by name), with a warning
    clearDir();
    for (int i = 0; i < 70; i++) {
        char name[32];
        std::snprintf(name, sizeof name, "m%03d.conf", i);
        writeFile(name, "V_ACTION = none\n");
    }
    std::vector<std::string> w;
    ModeSet ms = loadModes(g_dir, &w);
    assert(ms.modes.size() == 3 + 64);
    assert(ms.find("m000") && ms.find("m063") && !ms.find("m064"));
    assert(hasWarning(w, "too many mode files"));

    // 64 KiB limit: exactly at the limit ok, one more byte rejected
    clearDir();
    std::string ok = "NAME = x\n";
    ok += std::string(64 * 1024 - ok.size(), '#');
    assert(ok.size() == 64 * 1024);
    writeFile("fits.conf", ok);
    writeFile("big.conf", ok + "#");
    w.clear();
    ms = loadModes(g_dir, &w);
    assert(ms.find("fits") && !ms.find("big") && hasWarning(w, "big.conf") && hasWarning(w, "too large"));

    // 128 overrides ok, 129 -> file rejected
    clearDir();
    std::string o128, o129;
    for (int i = 0; i < 128; i++) o128 += "SCROLL_V_SENS = " + std::to_string(i % 200) + "\n";
    o129 = o128 + "SCROLL_V_SENS = 1\n";
    writeFile("o128.conf", o128);
    writeFile("o129.conf", o129);
    w.clear();
    ms = loadModes(g_dir, &w);
    assert(ms.find("o128") && ms.find("o128")->overrides.size() == 128);
    assert(!ms.find("o129") && hasWarning(w, "too many settings overrides"));

    // very long line is skipped, the rest still parsed
    clearDir();
    writeFile("long.conf", "NAME = " + std::string(5000, 'a') + "\nV_ACTION = none\n");
    w.clear();
    ms = loadModes(g_dir, &w);
    assert(ms.find("long") && ms.find("long")->v.type == ChannelActionType::None && hasWarning(w, "too long"));
}

static void testFileTypes() {
    clearDir();
    writeFile("real.conf", "V_ACTION = none\n");
    writeFile("target.txt", "V_ACTION = wheel_v\n");
    assert(symlink((g_dir + "/target.txt").c_str(), (g_dir + "/link.conf").c_str()) == 0);
    assert(symlink("does-not-exist", (g_dir + "/dangling.conf").c_str()) == 0);
    assert(mkdir((g_dir + "/dir.conf").c_str(), 0700) == 0);
    assert(mkfifo((g_dir + "/fifo.conf").c_str(), 0600) == 0);  // must not block
    std::vector<std::string> w;
    ModeSet ms = loadModes(g_dir, &w);
    assert(ms.find("real"));
    assert(!ms.find("link") && !ms.find("dangling") && !ms.find("dir") && !ms.find("fifo"));
    assert(hasWarning(w, "link.conf") && hasWarning(w, "fifo.conf") && hasWarning(w, "dir.conf"));

    // foreign owner: only testable as root (chown needs privilege)
    if (geteuid() == 0) {
        clearDir();
        writeFile("mine.conf", "V_ACTION = none\n");
        writeFile("theirs.conf", "V_ACTION = none\n");
        assert(chown((g_dir + "/theirs.conf").c_str(), 65534, 65534) == 0);
        w.clear();
        ms = loadModes(g_dir, &w);
        assert(ms.find("mine") && !ms.find("theirs") && hasWarning(w, "not owned"));
    } else {
        std::cout << "test_modes: foreign-owner check skipped (not root)\n";
    }
}

static void testStamp() {
    clearDir();
    const std::string s0 = modesStamp(g_dir);
    assert(!s0.empty());  // directory exists but is empty -> non-empty stamp, differs from "missing"
    writeFile("a.conf", "V_ACTION = none\n");
    const std::string s1 = modesStamp(g_dir);
    assert(s1 != s0 && modesStamp(g_dir) == s1);
    writeFile("a.conf", "V_ACTION = none\nH_ACTION = none\n");  // size changes
    const std::string s2 = modesStamp(g_dir);
    assert(s2 != s1);
    writeFile("b.conf", "");
    assert(modesStamp(g_dir) != s2);
    writeFile("notes.txt", "x");  // non-.conf files do not count
    const std::string s3 = modesStamp(g_dir);
    writeFile("more.txt", "y");
    assert(modesStamp(g_dir) == s3);
}

// A mode's overrides feed ProfileSet::resolve: global < device < mode < app.
static void testResolveWithMode() {
    clearDir();
    writeFile("fast.conf", "SCROLL_V_SENS = 9\nZOOM_SENS = 9\n");
    ModeSet ms = loadModes(g_dir, nullptr);
    const ModeDef *m = ms.find("fast");
    assert(m);
    ProfileSet ps;
    ps.sections[0].kv = {{"SCROLL_V_SENS", "1"}, {"ZOOM_SENS", "1"}, {"MODE", "fast"}};
    size_t dev = ps.findOrAdd(Section::DEVICE, "pro");
    ps.sections[dev].kv = {{"SCROLL_V_SENS", "2"}, {"ZOOM_SENS", "2"}};
    size_t app = ps.findOrAdd(Section::APP, "app");
    ps.sections[app].kv = {{"ZOOM_SENS", "4"}};
    Settings s = ps.resolve("pro", "app");
    assert(s.mode == "fast" && ms.find(s.mode) == m);
    s = ps.resolve("pro", "app", &m->overrides);
    assert(s.scroll_v.sensitivity == 9);  // mode beats device
    assert(s.zoom.sensitivity == 4);      // app beats mode
}

int main() {
    g_dir = "/tmp/test_modes." + std::to_string(::getpid());
    testIds();
    testGrammar();
    testBuiltins();
    testParser();
    testGrammarErrors();
    testLimits();
    testFileTypes();
    testStamp();
    testResolveWithMode();
    assert(system(("rm -rf '" + g_dir + "'").c_str()) == 0);
    std::cout << "test_modes: all tests passed\n";
    return 0;
}
