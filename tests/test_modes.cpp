// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tests for src/modes.cpp - plain assert tests, no framework.
#undef NDEBUG
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>

#include "config.hpp"
#include "modes.hpp"
#include "builtin_counts.hpp"
#include "testutil.hpp"

using namespace spn;

static std::string g_dir;

static void writeFile(const std::string &name, const std::string &content) {
    std::ofstream f(g_dir + "/" + name, std::ios::binary);
    f << content;
}

static void clearDir() {
    tu::removeAll(g_dir);
    assert(std::filesystem::create_directories(g_dir));
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
    // reserved Windows device names are not mode ids on any OS (con.conf would be the console)
    for (const char *r : {"con", "prn", "aux", "nul", "com1", "com9", "lpt1", "lpt9"}) assert(!validModeId(r));
    assert(validModeId("console") && validModeId("com0") && validModeId("com10") && validModeId("lpt") &&
           validModeId("con-1") && validModeId("nul_") && validModeId("aux2"));
}

static void testGrammar() {
    std::string err;
    ChannelAction a;
    assert(parseChannelAction("default", &a, &err) && a.type == ChannelActionType::Default);
    assert(parseChannelAction(" NONE ", &a, &err) && a.type == ChannelActionType::Off);
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

    // buttonmode:<id> in button actions: accepted syntactically (existence is checked at runtime)
    {
        ButtonActionType bt;
        Key bk;
        std::string bm;
        assert(parseButtonAction("buttonmode:latch-nav", &bt, &bk, &err, &bm) && bt == ButtonActionType::ButtonMode &&
               bm == "latch-nav");
        assert(!parseButtonAction("buttonmode:Bad Id", &bt, &bk, &err, &bm) && !err.empty());
    }

    // step: same grammar as keys:, own type, compared and warned like keys:
    assert(parseChannelAction("step:pageup,pagedown", &a, &err) && a.type == ChannelActionType::Step &&
           a.neg == Key::PageUp && a.pos == Key::PageDown);
    assert(parseChannelAction(" STEP: ctrl+pageup , ctrl+pagedown", &a, &err) && a.type == ChannelActionType::Step &&
           a.negMods.n == 1 && a.posMods.n == 1);
    {
        ChannelAction k, st;
        assert(parseChannelAction("keys:left,right", &k, nullptr) && parseChannelAction("step:left,right", &st, nullptr));
        assert(!(k == st) && st == st);
        ChannelAction st2;
        assert(parseChannelAction("step:left,up", &st2, nullptr) && !(st == st2));
        assert(st.usesKeys() && k.usesKeys() && !ChannelAction().usesKeys());
    }
    for (const char *bad : {"step:", "step:left", "step:left,right,up", "step:ctrl+ctrl+a,b", "step:ctrl+shift,b",
                            "step:left,nonsense"}) {
        err.clear();
        assert(!parseChannelAction(bad, &a, &err) && !err.empty());
    }

    // key chords: modifiers + one key, in any order, each modifier once, at most four
    assert(parseChannelAction("keys:ctrl+pageup,ctrl+pagedown", &a, &err) && a.type == ChannelActionType::Keys &&
           a.neg == Key::PageUp && a.pos == Key::PageDown && a.negMods.n == 1 && a.negMods.k[0] == Key::Ctrl &&
           a.posMods.n == 1);
    assert(parseChannelAction("Keys: Ctrl + Shift + Tab , alt+left", &a, &err) && a.negMods.n == 2 &&
           a.negMods.k[0] == Key::Ctrl && a.negMods.k[1] == Key::Shift && a.pos == Key::Left && a.posMods.n == 1);
    {
        ChannelAction x, y, z;
        assert(parseChannelAction("keys:ctrl+shift+tab,left", &x, nullptr));
        assert(parseChannelAction("keys:shift+ctrl+tab,left", &y, nullptr));  // order independent
        assert(x == y);
        assert(parseChannelAction("keys:ctrl+tab,left", &z, nullptr));
        assert(!(x == z));
        assert(parseChannelAction("keys:tab,left", &z, nullptr) && !(z == x) && z.negMods.n == 0);
        assert(parseChannelAction("keys:ctrl+shift+alt+super+a,b", &z, nullptr) && z.negMods.n == 4);
        // a lone modifier stays valid
        assert(parseChannelAction("keys:ctrl,shift", &z, nullptr) && z.neg == Key::Ctrl && z.negMods.n == 0);
    }
    for (const char *bad : {"keys:ctrl+ctrl+a,b", "keys:ctrl+,b", "keys:+a,b", "keys:ctrl+shift,b",
                            "keys:a+b,c", "keys:ctrl+nonsense,b", "keys:ctrl+shift+alt+super+ctrl+a,b",
                            "keys:ctrl+a,b,c", "keys:ctrl+a"}) {
        err.clear();
        assert(!parseChannelAction(bad, &a, &err) && !err.empty());
    }

    ButtonActionType t;
    Key k;
    assert(parseButtonAction("none", &t, &k, &err) && t == ButtonActionType::Unbound);
    assert(parseButtonAction("click", &t, &k, &err) && t == ButtonActionType::Click);
    assert(parseButtonAction("RightClick", &t, &k, &err) && t == ButtonActionType::RightClick);
    assert(parseButtonAction("middleclick", &t, &k, &err) && t == ButtonActionType::MiddleClick);
    assert(parseButtonAction("nav", &t, &k, &err) && t == ButtonActionType::Nav);
    assert(parseButtonAction("key:space", &t, &k, &err) && t == ButtonActionType::Key && k == Key::Space);
    assert(parseButtonAction(" key: F5 ", &t, &k, &err) && k == Key::F5);
    {   // key chords in a button action: key:<mod>+...+<key>, same grammar and limits as keys:
        ChordMods cm;
        assert(parseButtonAction("key:ctrl+0", &t, &k, &err, nullptr, &cm) && t == ButtonActionType::Key && k == Key::N0 &&
               cm.n == 1 && cm.k[0] == Key::Ctrl);
        assert(parseButtonAction(" KEY: Ctrl + Shift + N ", &t, &k, &err, nullptr, &cm) && k == Key::N && cm.n == 2 &&
               cm.k[0] == Key::Ctrl && cm.k[1] == Key::Shift);
        assert(parseButtonAction("key:super+alt+ctrl+shift+f5", &t, &k, &err, nullptr, &cm) && cm.n == 4 && cm.k[0] == Key::Super &&
               k == Key::F5);
        assert(parseButtonAction("key:space", &t, &k, &err, nullptr, &cm) && cm.n == 0 && k == Key::Space);  // plain key unchanged
        assert(parseButtonAction("key:ctrl", &t, &k, &err, nullptr, &cm) && cm.n == 0 && k == Key::Ctrl);    // a lone modifier is a plain key
        assert(parseButtonAction("key:ctrl+0", &t, &k, &err) && t == ButtonActionType::Key);                  // mods pointer is optional
        parseButtonAction("key:ctrl+shift+n", &t, &k, &err, nullptr, &cm);
        assert(keyActionText(k, cm) == "key:ctrl+shift+n");
        for (const char *bad : {"key:ctrl+shift+alt+super+ctrl+a", "key:ctrl+ctrl+a", "key:ctrl+nonsense", "key:ctrl+", "key:+a",
                                "key:ctrl+shift", "key:a+b", "key:ctrl+shift+alt+super+shift+a", "key:ctrl++0"}) {
            err.clear();
            assert(!parseButtonAction(bad, &t, &k, &err, nullptr, &cm) && !err.empty());
        }
        assert(!chordNeedsWarning(Key::Space, ChordMods()));
        ChordMods c1;
        parseButtonAction("key:ctrl+0", &t, &k, &err, nullptr, &c1);
        assert(!chordNeedsWarning(Key::N0, c1));  // the zoom reset: documented as harmless like ctrl+plus
        parseButtonAction("key:ctrl+w", &t, &k, &err, nullptr, &c1);
        assert(chordNeedsWarning(Key::W, c1));
        parseButtonAction("key:alt+left", &t, &k, &err, nullptr, &c1);
        assert(chordNeedsWarning(Key::Left, c1));
    }
    for (const char *bad : {"", "press", "key:", "key:nonsense", "keys:a,b", "click now", "key"}) {
        err.clear();
        assert(!parseButtonAction(bad, &t, &k, &err) && !err.empty());
    }
}

// normal, off, timeline + every embedded example mode: derived from the library, never a literal
static const size_t kBuiltinModes = tu::builtinModeCount();

static void testBuiltins() {
    std::vector<std::string> w;
    ModeSet none = loadModes(g_dir + "/does-not-exist", &w);
    assert(w.empty());  // a missing directory is not an error
    assert(modesStamp(g_dir + "/does-not-exist").empty());
    // code-defined: normal, off, timeline; embedded from examples/modes: whatever files exist there
    assert(none.modes.size() == kBuiltinModes && none.modes.size() == tu::codeDefinedModeCount() + tu::embeddedExampleIds().size() && none.normal().id == "normal" && none.normal().builtin);
    std::vector<std::string> allBuiltin = tu::embeddedExampleIds();
    for (const char *id : {"normal", "off", "timeline"}) allBuiltin.push_back(id);
    for (const std::string &id : allBuiltin) {
        const ModeDef *b = none.find(id);
        assert(b && b->builtin && !b->label.empty());
    }
    assert(!none.find("cycle-modes") && !none.find("browse-keys"));  // dropped / renamed
    {
        std::set<std::string> ids;
        for (const ModeDef &m : none.modes) assert(ids.insert(m.id).second);  // unique ids
        // the embedded files parse without a warning and without a chord warning
        std::vector<std::string> bw;
        assert(loadModes("", &bw).modes.size() == kBuiltinModes && bw.empty());
        // the embedded text and the example file give the same mode
        const std::string exDir =
            (std::filesystem::path(__FILE__).parent_path() / ".." / "examples" / "modes").string();
        // copies with an extra comment line (an identical file would be taken for the built-in itself)
        clearDir();
        for (const std::string &id : tu::embeddedExampleIds()) {
            std::ifstream in(exDir + "/" + id + ".conf", std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assert(!text.empty());
            writeFile(id + ".conf", text + "\n# copy\n");
        }
        const ModeSet files = loadModes(g_dir, nullptr);
        for (const std::string &id : tu::embeddedExampleIds()) {
            const ModeDef *e = none.find(id), *f = files.find(id);
            assert(e && f && e->builtin && !f->builtin);
            assert(e->label == f->label && e->description == f->description && e->v == f->v && e->h == f->h &&
                   e->twist == f->twist && e->overrides == f->overrides && e->buttons.size() == f->buttons.size());
        }
        assert(none.find("browse")->label == "Browse");
        const ModeDef *pr = none.find("precision");
        assert(pr->overrides.size() == 3 && pr->v.type == ChannelActionType::Default);
        const ModeDef *sl = none.find("slides");
        assert(sl->v.type == ChannelActionType::Keys && sl->h.type == ChannelActionType::Off);
    }
    // a user file byte-identical to a shipped example (built-in text, or the copy that earlier
    // versions made on first start: browse-keys, cycle-modes, timeline, ...) is the built-in: not listed again
    {
        const std::string exDir =
            (std::filesystem::path(__FILE__).parent_path() / ".." / "examples").string();
        clearDir();
        for (const char *id : {"slides", "browse"}) {
            std::ifstream in(exDir + "/modes/" + id + ".conf", std::ios::binary);
            writeFile(std::string(id) + ".conf", std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
        }
        for (const char *id : {"timeline", "browse-keys", "cycle-modes", "tabs"}) {
            std::ifstream in(exDir + "/legacy/modes/" + id + ".conf", std::ios::binary);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assert(!text.empty());
            writeFile(std::string(id) + ".conf", text);
        }
        std::vector<std::string> iw;
        const ModeSet ms0 = loadModes(g_dir, &iw);
        assert(iw.empty() && ms0.modes.size() == kBuiltinModes);
        for (const ModeDef &m : ms0.modes) assert(m.builtin);
        assert(!ms0.find("browse-keys") && !ms0.find("cycle-modes"));
        // one changed byte makes it the user's own file again
        writeFile("timeline.conf", "# mine\n" + std::string(std::istreambuf_iterator<char>(*std::make_unique<std::ifstream>(exDir + "/legacy/modes/timeline.conf", std::ios::binary)), {}));
        const ModeSet ms1 = loadModes(g_dir, nullptr);
        assert(ms1.find("timeline") && !ms1.find("timeline")->builtin);
    }

    // every recovered legacy text (examples/legacy/modes/<id>[.<n>].conf: all texts the first-start seeding ever
    // copied) is ignored by the loader, and a changed copy is the user's own file. The oldest timeline text
    // had button bindings: it must not replace the built-in timeline.
    {
        const std::filesystem::path legacyDir = std::filesystem::path(__FILE__).parent_path() / ".." / "examples" / "legacy" / "modes";
        int seen = 0;
        bool sawOldTimeline = false;
        for (const auto &de : std::filesystem::directory_iterator(legacyDir)) {
            const std::string name = de.path().filename().string();
            if (name.size() < 6 || name.compare(name.size() - 5, 5, ".conf") != 0) continue;
            const std::string id = name.substr(0, name.find('.'));
            std::ifstream in(de.path(), std::ios::binary);
            const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            assert(!text.empty());
            clearDir();
            writeFile(id + ".conf", text);
            std::vector<std::string> lw;
            const ModeSet ls = loadModes(g_dir, &lw);
            assert(lw.empty() && ls.modes.size() == kBuiltinModes);
            for (const ModeDef &m : ls.modes) assert(m.builtin);
            if (id == "timeline") {
                assert(ls.find("timeline") && ls.find("timeline")->buttons.empty());
                if (text.find("BUTTON_") != std::string::npos) sawOldTimeline = true;
            }
            // same id, changed content: the user's own file
            writeFile(id + ".conf", text + "# edited\n");
            const ModeSet ch = loadModes(g_dir, nullptr);
            assert(ch.find(id) && !ch.find(id)->builtin);
            ++seen;
        }
        assert(seen >= 16);
        assert(sawOldTimeline);
    }

    // a file with the id of an embedded mode replaces it, the other built-ins stay
    {
        clearDir();
        writeFile("slides.conf", "NAME = My slides\nV_ACTION = none\n");
        std::vector<std::string> ow;
        const ModeSet ov = loadModes(g_dir, &ow);
        const ModeDef *s = ov.find("slides");
        assert(ow.empty() && ov.modes.size() == kBuiltinModes && s && !s->builtin && s->label == "My slides" &&
               s->v.type == ChannelActionType::Off);
        assert(ov.find("media") && ov.find("media")->builtin);
    }
    assert(none.normal().allDefaultChannels() && none.normal().buttons.empty());
    const ModeDef *off = none.find("off");
    assert(off && off->v.type == ChannelActionType::Off && off->h.type == ChannelActionType::Off &&
           off->twist.type == ChannelActionType::Off);
    const ModeDef *tl = none.find("timeline");
    assert(tl && tl->v.type == ChannelActionType::Off);
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
              "BUTTON_1 = key:space\n"
              "BUTTON_2 = click\n"
              "BUTTON_2 = rightclick\n"  // later wins
              "SCROLL_H_SENS = 4\n"
              "zoom_sens = 7 # note\n");
    std::vector<std::string> w;
    ModeSet ms = loadModes(g_dir, &w);
    assert(w.empty());
    const ModeDef *m = ms.find("resolve");
    assert(m && !m->builtin && m->label == "DaVinci Resolve" && m->description == "Edit page");
    assert(m->v.type == ChannelActionType::Off && m->h.type == ChannelActionType::Keys);
    assert(m->twist.neg == Key::Left && m->twist.pos == Key::Right);
    assert(m->buttons.size() == 2);
    assert(m->buttons[0].button == 1 && m->buttons[0].type == ButtonActionType::Key && m->buttons[0].key == Key::Space);
    assert(m->buttons[1].button == 2 && m->buttons[1].type == ButtonActionType::RightClick);
    assert(m->overrides.size() == 2 && m->overrides[0].first == "SCROLL_H_SENS" && m->overrides[0].second == "4");
    assert(m->overrides[1].first == "ZOOM_SENS" && m->overrides[1].second == "7");
    assert(!m->allDefaultChannels());
    assert(ms.modes.size() == kBuiltinModes + 1);

    // label falls back to the id; unlisted names are ignored
    clearDir();
    writeFile("plain.conf", "V_ACTION = wheel_v\n");
    writeFile("Bad Name.conf", "V_ACTION = none\n");
    writeFile("UPPER.conf", "V_ACTION = none\n");
    writeFile("readme.txt", "V_ACTION = none\n");
    writeFile(".conf", "V_ACTION = none\n");
    ms = loadModes(g_dir, &w);
    assert(ms.modes.size() == kBuiltinModes + 1 && ms.find("plain") && ms.find("plain")->label == "plain");

    // a file named like a built-in replaces it (normal stays at index 0)
    clearDir();
    writeFile("normal.conf", "NAME = My normal\nBUTTON_3 = nav\n");
    writeFile("timeline.conf", "H_ACTION = zoom\n");
    ms = loadModes(g_dir, &w);
    assert(ms.modes.size() == kBuiltinModes && ms.normal().id == "normal" && !ms.normal().builtin);
    assert(ms.normal().label == "My normal" && ms.normal().buttons.size() == 1);
    assert(ms.find("timeline")->h.type == ChannelActionType::Zoom && ms.find("timeline")->v.type == ChannelActionType::Default);
}

static void testGrammarErrors() {
    clearDir();
    writeFile("bad.conf",
              "V_ACTION = flying\n"           // 1: invalid action
              "H_ACTION = keys:left\n"        // 2
              "BUTTON_1 = key:nonsense\n"     // 3
              "BUTTON_33 = click\n"           // 4: number out of range
              "BUTTON_x = click\n"            // 5
              "BUTTON_ = click\n"             // 6
              "NO_SUCH_KEY = 1\n"             // 7
              "SCROLL_V_SIGN = 5\n"           // 8: invalid value
              "MODE = timeline\n"             // 9: forbidden in mode files
              "[section]\n"                   // 10
              "no equals sign\n"              // 11
              " = value\n"                    // 12
              "NAME = ok\x01\x02name\n"       // control characters are dropped
              "TWIST_ACTION = zoom\n"
              "BUTTON_0 = click\n");         // 13: buttons are numbered from 1
    std::vector<std::string> w;
    ModeSet ms = loadModes(g_dir, &w);
    const ModeDef *m = ms.find("bad");
    assert(m);  // the file survives, bad lines are skipped
    assert(w.size() == 13);
    assert(hasWarning(w, "buttons are numbered from 1"));
    assert(hasWarning(w, "bad.conf:1:") && hasWarning(w, "bad.conf:9:") && hasWarning(w, "MODE"));
    assert(m->v.type == ChannelActionType::Default && m->h.type == ChannelActionType::Default);
    assert(m->twist.type == ChannelActionType::Zoom);
    assert(m->buttons.empty() && m->overrides.empty());
    assert(m->label == "okname");

    // LF-less last line and CRLF line endings
    clearDir();
    writeFile("crlf.conf", "V_ACTION = none\r\nH_ACTION = wheel_h");
    ms = loadModes(g_dir, &w);
    assert(ms.find("crlf")->v.type == ChannelActionType::Off && ms.find("crlf")->h.type == ChannelActionType::WheelH);
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
    assert(ms.modes.size() == kBuiltinModes + 64);
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
    assert(ms.find("long") && ms.find("long")->v.type == ChannelActionType::Off && hasWarning(w, "too long"));
}

static void testFileTypes() {
    clearDir();
    writeFile("real.conf", "V_ACTION = none\n");
    writeFile("target.txt", "V_ACTION = wheel_v\n");
#ifndef _WIN32
    assert(symlink((g_dir + "/target.txt").c_str(), (g_dir + "/link.conf").c_str()) == 0);
    assert(symlink("does-not-exist", (g_dir + "/dangling.conf").c_str()) == 0);
    assert(mkfifo((g_dir + "/fifo.conf").c_str(), 0600) == 0);  // must not block
#endif
    assert(std::filesystem::create_directory(g_dir + "/dir.conf"));
    std::vector<std::string> w;
    ModeSet ms = loadModes(g_dir, &w);
    assert(ms.find("real"));
    assert(!ms.find("dir"));
    assert(hasWarning(w, "dir.conf"));
#ifndef _WIN32
    assert(!ms.find("link") && !ms.find("dangling") && !ms.find("fifo"));
    assert(hasWarning(w, "link.conf") && hasWarning(w, "fifo.conf"));
#endif

    // foreign owner: only testable as root (chown needs privilege); POSIX only
#ifdef _WIN32
    std::cout << "test_modes: foreign-owner check skipped (Windows)\n";
#else
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
#endif
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

static void testModeSwitchBinding() {
    ButtonActionType t;
    Key k;
    std::string err, id;
    assert(parseButtonAction("mode:timeline", &t, &k, &err, &id) && t == ButtonActionType::ModeSwitch && id == "timeline");
    assert(parseButtonAction(" Mode:NEXT ", &t, &k, &err, &id) && id == "next");
    assert(!parseButtonAction("mode:", &t, &k, &err) && !err.empty());
    assert(validModeId("release") && parseButtonAction("mode:release", &t, &k, &err, &id) && id == "release");
    assert(!validModeId("release:hold"));  // the internal hold-release request is not a mode id
    assert(!parseButtonAction("mode:bad name", &t, &k, &err));
    assert(parseButtonAction("mode:x", &t, &k, &err));  // id pointer is optional

    clearDir();
    writeFile("a.conf", "BUTTON_1 = mode:timeline\nBUTTON_2 = mode:next\nBUTTON_3 = mode:nosuch\n");
    std::vector<std::string> w;
    ModeSet ms = loadModes(g_dir, &w);
    const ModeDef *m = ms.find("a");
    assert(m && m->buttons.size() == 2);
    assert(m->buttons[0].type == ButtonActionType::ModeSwitch && m->buttons[0].mode == "timeline");
    assert(m->buttons[1].mode == "next");
    assert(hasWarning(w, "unknown mode 'nosuch'"));
    // the main config accepts the syntax too
    Settings s;
    assert(applyKey(s, "BUTTON_2", "mode:next", &err) && s.buttons[2] == "mode:next");
    assert(applyKey(s, "BUTTON_3", "mode:prev", &err) && s.buttons[3] == "mode:prev");

    // mode:prev is a reserved target like mode:next: always valid in a mode file, no warning
    clearDir();
    writeFile("p.conf", "BUTTON_1 = mode:prev\nBUTTON_2 = mode:next\n");
    writeFile("prev.conf", "V_ACTION = none\n");  // a mode of that name still loads
    w.clear();
    ms = loadModes(g_dir, &w);
    m = ms.find("p");
    assert(m && m->buttons.size() == 2 && m->buttons[0].type == ButtonActionType::ModeSwitch && m->buttons[0].mode == "prev");
    assert(!hasWarning(w, "unknown mode") && ms.find("prev"));
    ButtonActionType pt;
    Key pk;
    std::string pid;
    assert(parseButtonAction("mode:prev", &pt, &pk, &err, &pid) && pt == ButtonActionType::ModeSwitch && pid == "prev");
}

static void testToggleBindings() {
    ButtonActionType t;
    Key k;
    std::string err, id;
    assert(parseButtonAction("toggle:nav", &t, &k, &err) && t == ButtonActionType::ToggleNav);
    assert(parseButtonAction(" Toggle:NAV ", &t, &k, &err) && t == ButtonActionType::ToggleNav);
    assert(parseButtonAction("toggle:mode:timeline", &t, &k, &err, &id) && t == ButtonActionType::ToggleMode && id == "timeline");
    assert(!parseButtonAction("toggle:", &t, &k, &err) && !err.empty());
    assert(!parseButtonAction("toggle:foo", &t, &k, &err));
    assert(!parseButtonAction("toggle:mode:", &t, &k, &err));
    assert(!parseButtonAction("toggle:mode:bad name", &t, &k, &err));
    assert(!parseButtonAction("toggle:mode:next", &t, &k, &err));
    assert(!parseButtonAction("toggle:mode:prev", &t, &k, &err) && err.find("prev") != std::string::npos);
    assert(!parseButtonAction("toggle:key:space", &t, &k, &err));  // plain keys are the driver's job

    // hold:mode:<id>: momentary mode while the button is held
    assert(parseButtonAction("hold:mode:timeline", &t, &k, &err, &id) && t == ButtonActionType::HoldMode && id == "timeline");
    assert(parseButtonAction(" Hold:Mode: Fine_1 ", &t, &k, &err, &id) && id == "fine_1");
    for (const char *bad : {"hold:mode:", "hold:mode:next", "hold:mode:prev", "hold:mode:Bad Id", "hold:", "hold:nav", "hold:key:space"}) {
        err.clear();
        assert(!parseButtonAction(bad, &t, &k, &err) && !err.empty());
    }
    {
        clearDir();
        writeFile("h.conf", "BUTTON_1 = hold:mode:timeline\nBUTTON_2 = hold:mode:nosuch\n");
        std::vector<std::string> hw;
        const ModeSet hs = loadModes(g_dir, &hw);
        const ModeDef *hm = hs.find("h");
        assert(hm && hm->buttons.size() == 1 && hm->buttons[0].type == ButtonActionType::HoldMode && hm->buttons[0].mode == "timeline");
        assert(hasWarning(hw, "unknown mode 'nosuch'"));
        Settings hset;
        assert(applyKey(hset, "BUTTON_4", "hold:mode:off", &err) && hset.buttons[4] == "hold:mode:off");
    }

    clearDir();
    writeFile("a.conf", "BUTTON_1 = toggle:nav\nBUTTON_2 = toggle:mode:timeline\nBUTTON_3 = toggle:mode:nosuch\n");
    std::vector<std::string> w;
    ModeSet ms = loadModes(g_dir, &w);
    const ModeDef *m = ms.find("a");
    assert(m && m->buttons.size() == 2);
    assert(m->buttons[0].type == ButtonActionType::ToggleNav);
    assert(m->buttons[1].type == ButtonActionType::ToggleMode && m->buttons[1].mode == "timeline");
    assert(hasWarning(w, "unknown mode 'nosuch'"));
    // the main config accepts the syntax too
    Settings s;
    assert(applyKey(s, "BUTTON_2", "toggle:nav", &err) && s.buttons[2] == "toggle:nav");
    assert(applyKey(s, "BUTTON_3", "toggle:mode:off", &err) && s.buttons[3] == "toggle:mode:off");
}

static void testSecurityHardening() {
    std::vector<std::string> w;
    std::string err;
    // F5: process-level keys are rejected as overrides
    clearDir();
    writeFile("evil.conf", "ENABLED = 0\nBACKEND = uinput\ntick_hz = 1000\nSCROLL_V_SENS = 3\n");
    ModeSet ms = loadModes(g_dir, &w);
    const ModeDef *m = ms.find("evil");
    assert(m && m->overrides.size() == 1 && m->overrides[0].first == "SCROLL_V_SENS");
    assert(hasWarning(w, "ENABLED is not allowed in a mode file"));
    assert(hasWarning(w, "BACKEND is not allowed"));
    assert(hasWarning(w, "TICK_HZ is not allowed"));
    assert(hasWarning(w, "evil.conf:1"));

    // F2: chord warnings, one per mode, mode still loads
    clearDir();
    writeFile("mod.conf", "BUTTON_1 = key:ctrl\nBUTTON_2 = key:alt\n");
    writeFile("kmod.conf", "TWIST_ACTION = keys:shift,f1\n");
    writeFile("zk.conf", "V_ACTION = zoom\nH_ACTION = keys:w,w\n");
    writeFile("cnav.conf", "H_ACTION = keys:ctrl+pageup,ctrl+shift+pagedown\nV_ACTION = keys:shift+left,shift+right\n");
    writeFile("calt.conf", "H_ACTION = keys:alt+left,alt+right\n");
    writeFile("csup.conf", "H_ACTION = keys:super+left,super+right\n");
    writeFile("cdel.conf", "H_ACTION = keys:ctrl+w,ctrl+q\n");
    writeFile("safe.conf", "BUTTON_1 = key:space\nH_ACTION = keys:left,right\nV_ACTION = wheel_v\n");
    w.clear();
    ms = loadModes(g_dir, &w);
    assert(ms.find("mod") && ms.find("kmod") && ms.find("zk") && ms.find("safe"));
    int n = 0;
    for (const std::string &x : w)
        if (x.find("mode mod can press shortcut chords") != std::string::npos) n++;
    assert(n == 1);
    assert(hasWarning(w, "mode kmod can press shortcut chords"));
    assert(hasWarning(w, "mode zk can press shortcut chords"));
    assert(!hasWarning(w, "mode safe"));
    writeFile("snav.conf", "H_ACTION = step:ctrl+pageup,ctrl+pagedown\n");
    writeFile("sdel.conf", "H_ACTION = step:ctrl+w,ctrl+q\nV_ACTION = step:left,right\n");
    w.clear();
    ms = loadModes(g_dir, &w);
    assert(ms.find("snav") && ms.find("sdel"));
    assert(!hasWarning(w, "mode snav"));
    assert(hasWarning(w, "mode sdel can press shortcut chords") && hasWarning(w, "step: channel presses ctrl+w"));
    // button chords in a mode file: parsed into the binding, warned about by the same rules as keys:
    writeFile("bz.conf", "BUTTON_1 = key:ctrl+0\nBUTTON_2 = key:ctrl+pageup\n");
    writeFile("bw.conf", "BUTTON_1 = key:ctrl+w\n");
    writeFile("ba.conf", "BUTTON_1 = key:alt+left\n");
    writeFile("bbad.conf", "BUTTON_1 = key:ctrl+shift+alt+super+ctrl+a\nBUTTON_2 = key:space\n");
    w.clear();
    ms = loadModes(g_dir, &w);
    {
        const ModeDef *bz = ms.find("bz");
        assert(bz && bz->buttons.size() == 2 && bz->buttons[0].type == ButtonActionType::Key && bz->buttons[0].key == Key::N0 &&
               bz->buttons[0].mods.n == 1 && bz->buttons[0].mods.k[0] == Key::Ctrl);
        assert(!hasWarning(w, "mode bz"));  // ctrl+0 and ctrl+pageup: no warning
        assert(hasWarning(w, "mode bw can press shortcut chords") && hasWarning(w, "BUTTON_1 presses ctrl+w"));
        assert(hasWarning(w, "mode ba can press shortcut chords"));
        const ModeDef *bb = ms.find("bbad");
        assert(bb && bb->buttons.size() == 1 && bb->buttons[0].button == 2);  // the invalid chord line is dropped
        assert(hasWarning(w, "at most 4 modifiers") || hasWarning(w, "appears twice"));
    }
    assert(ms.find("cnav") && ms.find("calt") && ms.find("csup") && ms.find("cdel"));
    assert(!hasWarning(w, "mode cnav"));  // Ctrl/Shift with navigation keys: no warning
    assert(hasWarning(w, "mode calt can press shortcut chords") && hasWarning(w, "alt+left"));
    assert(hasWarning(w, "mode csup can press shortcut chords"));
    assert(hasWarning(w, "mode cdel can press shortcut chords") && hasWarning(w, "ctrl+w"));

    // shipped example modes: no chord warning (loaded as files: not identical to the embedded text)
    const std::string src =
        (std::filesystem::path(__FILE__).parent_path() / ".." / "examples" / "modes").string();
    clearDir();
    for (const auto &de : std::filesystem::directory_iterator(src)) {
        std::ifstream in(de.path(), std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        writeFile(de.path().filename().string(), text + "\n# copy\n");
    }
    w.clear();
    ms = loadModes(g_dir, &w);
    for (const std::string &id : tu::embeddedExampleIds()) assert(ms.find(id) && !ms.find(id)->builtin);
    assert(ms.find("browse") && !ms.find("browse")->builtin);
    {
        const ModeDef *tabs = ms.find("tabs");
        assert(tabs && tabs->buttons.empty() && tabs->h.type == ChannelActionType::Keys &&
               tabs->h.negMods.n == 1 && tabs->h.neg == Key::PageUp && tabs->h.pos == Key::PageDown);
    }
    for (const char *id : {"slides", "media", "map", "precision"}) {
        const ModeDef *sm = ms.find(id);
        assert(sm && sm->buttons.empty());  // axis-only examples
    }
    assert(w.empty());

    // F9: NUL bytes
    clearDir();
    static const char nulText[] = "SCROLL_V_SENS = 3\0junk\nBUTTON_1 = key:space\0x\nSCROLL_H_SENS = 4\n";
    writeFile("nulbyte.conf", std::string(nulText, sizeof nulText - 1));  // not "nul": a reserved Windows device name
    w.clear();
    ms = loadModes(g_dir, &w);
    m = ms.find("nulbyte");
    assert(m && m->overrides.size() == 1 && m->overrides[0].second == "4" && m->buttons.empty());
    assert(hasWarning(w, "NUL byte"));
    Settings s;
    assert(!applyKey(s, "SCROLL_V_SENS", std::string("3\0junk", 6), &err) && err.find("NUL") != std::string::npos);
}

// LIFT_ACTION in a mode file: the grammar without the analog keys:, the lift keys as overrides (stage 2 of #224).
static void testLiftKeys() {
    clearDir();
    writeFile("l1.conf", "LIFT_ACTION = step:left,right\nLIFT_SENS = 7\nLIFT_DEADZONE = 120\nLIFT_MIX = z\nLIFT_SIGN = -1\n");
    writeFile("l2.conf", "LIFT_ACTION = keys:left,right\n");
    writeFile("l3.conf", "LIFT_ACTION = none\nLIFT_ACTION = zoom\n");
    writeFile("l4.conf", "LIFT_SENS = 300x\nLIFT_MIX = q\nLIFT_ACTION = bogus\n");
    std::vector<std::string> w;
    const ModeSet ms = loadModes(g_dir, &w);
    const ModeDef *l1 = ms.find("l1"), *l2 = ms.find("l2"), *l3 = ms.find("l3"), *l4 = ms.find("l4");
    assert(l1 && l2 && l3 && l4);
    assert(l1->lift.type == ChannelActionType::Step && l1->overrides.size() == 4 && !l1->allDefaultChannels());
    assert(l2->lift.type == ChannelActionType::Default && l2->allDefaultChannels());
    assert(hasWarning(w, "l2.conf:1") && hasWarning(w, "step:"));
    assert(l3->lift.type == ChannelActionType::Zoom);  // the last line wins
    assert(l4->lift.type == ChannelActionType::Default && l4->overrides.empty());
    assert(hasWarning(w, "l4.conf:1") && hasWarning(w, "l4.conf:2") && hasWarning(w, "l4.conf:3"));
    // a mode that says LIFT_ACTION = none stays on the normal path
    writeFile("l5.conf", "LIFT_ACTION = none\n");
    const ModeSet ms5 = loadModes(g_dir, nullptr);
    assert(ms5.find("l5")->allDefaultChannels());
    // the override keys are valid settings keys: the defaults and the ranges
    Settings s;
    std::string err;
    assert(s.lift.deadzone == 80 && s.lift.sensitivity == 5 && s.lift.mix.terms.size() == 1 && s.lift.mix.terms[0].axis == AX_Y);
    assert(applyKey(s, "LIFT_SENS", "300", &err) && s.lift.sensitivity == 255);
    assert(applyKey(s, "LIFT_DEADZONE", "100", &err) && s.lift.deadzone == 100);
    assert(applyKey(s, "LIFT_SIGN", "-1", &err) && s.lift.sign == -1 && !applyKey(s, "LIFT_SIGN", "2", &err));
    assert(applyKey(s, "LIFT_CURVE", "2", &err) && applyKey(s, "LIFT_SMOOTH_MS", "10", &err) && s.lift.smooth_ms == 10);
    assert(applyKey(s, "LIFT_MIX", "z", &err) && s.lift.mix.terms[0].axis == AX_Z && !applyKey(s, "LIFT_MIX", "q", &err));
    assert(!applyKey(s, "LIFT_NOPE", "1", &err));
    // allKeys round trip
    Settings back;
    for (const KV &kv : allKeys(s)) assert(applyKey(back, kv.first, kv.second, &err));
    assert(back.lift.mix.terms[0].axis == AX_Z && back.lift.sensitivity == 255 && back.lift.sign == -1 && back.lift.deadzone == 100);
}

int main() {
    g_dir = tu::tmpRoot() + "/test_modes." + std::to_string(tu::pid());
    testIds();
    testLiftKeys();
    testGrammar();
    testBuiltins();
    testParser();
    testGrammarErrors();
    testLimits();
    testFileTypes();
    testStamp();
    testResolveWithMode();
    testModeSwitchBinding();
    testToggleBindings();
    testSecurityHardening();
    tu::removeAll(g_dir);
    std::cout << "test_modes: all tests passed\n";
    return 0;
}
