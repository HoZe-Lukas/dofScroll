// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Button mode files: parser, limits, loader safety, expansion of buttonmode:<id>.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "button_modes.hpp"
#include "builtin_counts.hpp"
#include "testutil.hpp"

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace spn;

static std::string g_dir;

static void writeFile(const std::string &name, const std::string &text) {
    std::filesystem::create_directories(g_dir);
    std::ofstream(g_dir + "/" + name, std::ios::binary) << text;
}
static void clearDir() {
    tu::removeAll(g_dir);
    std::filesystem::create_directories(g_dir);
}
static bool hasWarning(const std::vector<std::string> &w, const std::string &part) {
    for (const std::string &x : w)
        if (x.find(part) != std::string::npos) return true;
    return false;
}

int main() {
    g_dir = tu::tmpRoot() + "/test_button_modes." + std::to_string(tu::pid());
    clearDir();
    std::vector<std::string> w;
    // every file of examples/buttons, embedded: the count comes from the library, never a literal
    const size_t kBuiltin = tu::builtinButtonModeCount();
    assert(kBuiltin >= 1);

    // the built-in button modes: present with no directory, no warnings, no file, same as the examples
    {
        std::vector<std::string> bw;
        const ButtonModeSet lib = loadButtonModes("", &bw);
        assert(bw.empty() && lib.modes.size() == kBuiltin);
        for (const BuiltinFile *f = builtinButtonModeFiles(); f && f->id; ++f) {  // each embedded file is listed
            const ButtonMode *e = lib.find(f->id);
            assert(e && e->builtin && e->path.empty());
        }
        for (const char *id : {"latch-nav", "next-mode", "prev-mode"}) {
            const ButtonMode *b = lib.find(id);
            assert(b && b->builtin && b->path.empty() && !b->label.empty());
        }
        assert(lib.find("latch-nav")->type == ButtonActionType::ToggleNav);
        assert(lib.find("prev-mode")->type == ButtonActionType::ModeSwitch && lib.find("prev-mode")->target == "prev");
        // every built-in is expandable like an installed file
        Settings st;
        std::string err;
        assert(applyKey(st, "BUTTON_1", "buttonmode:latch-nav", &err));
        std::set<std::string> missing;
        expandSettings(st, lib, &missing);
        assert(missing.empty() && st.buttons[1] == "toggle:nav");
    }

    // grammar of buttonmode:<id> in the shared button action parser
    {
        ButtonActionType t;
        Key k;
        std::string err, id;
        assert(parseButtonAction("buttonmode:latch-nav", &t, &k, &err, &id) && t == ButtonActionType::ButtonMode &&
               id == "latch-nav");
        assert(parseButtonAction(" ButtonMode: Next_1 ", &t, &k, &err, &id) && id == "next_1");
        for (const char *bad : {"buttonmode:", "buttonmode:Bad Id", "buttonmode:a/b", "buttonmode:con",
                                "buttonmode:0123456789012345678901234567890123"}) {
            err.clear();
            assert(!parseButtonAction(bad, &t, &k, &err, &id) && !err.empty());
        }
    }

    // the shipped examples load without warnings and use dofscroll-state actions only
    {
        const std::string src =
            (std::filesystem::path(__FILE__).parent_path() / ".." / "examples" / "buttons").string();
        std::vector<std::string> ew;
        const ButtonModeSet ex = loadButtonModes(src, &ew);
        assert(ew.empty());
        const ButtonMode *latch = ex.find("latch-nav"), *next = ex.find("next-mode");
        assert(latch && latch->label == "Navigation latch" && latch->type == ButtonActionType::ToggleNav);
        assert(next && next->label == "Next mode" && next->type == ButtonActionType::ModeSwitch && next->target == "next");
    }

    // a file identical to a shipped example is the built-in, not the user's own file
    clearDir();
    {
        std::ifstream in(std::filesystem::path(__FILE__).parent_path() / ".." / "examples" / "buttons" / "next-mode.conf",
                         std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        assert(!text.empty());
        writeFile("next-mode.conf", text);
        const ButtonModeSet same = loadButtonModes(g_dir, nullptr);
        assert(same.modes.size() == kBuiltin && same.find("next-mode")->builtin);
        writeFile("next-mode.conf", text + "# edited\n");
        const ButtonModeSet edited = loadButtonModes(g_dir, nullptr);
        assert(!edited.find("next-mode")->builtin);
    }

    // a file with the id of a built-in replaces it (in place); the others stay
    clearDir();
    writeFile("latch-nav.conf", "NAME = My latch\nACTION = click\n");
    {
        const ButtonModeSet ov = loadButtonModes(g_dir, nullptr);
        const ButtonMode *b = ov.find("latch-nav");
        assert(ov.modes.size() == kBuiltin && b && !b->builtin && b->label == "My latch" &&
               b->type == ButtonActionType::Click && !b->path.empty());
        assert(ov.find("next-mode") && ov.find("next-mode")->builtin);
        // a broken override file is skipped with a warning and the built-in stays
        clearDir();
        writeFile("latch-nav.conf", "NAME = broken\n");
        std::vector<std::string> bw;
        const ButtonModeSet br = loadButtonModes(g_dir, &bw);
        assert(br.find("latch-nav")->builtin && hasWarning(bw, "no valid ACTION"));
    }

    // a valid file
    clearDir();
    writeFile("latch.conf", "# comment\nNAME = Navigation latch\nDESCRIPTION = Toggles navigation\nACTION = toggle:nav  # x\n");
    writeFile("nextm.conf", "ACTION = mode:next\n");
    writeFile("key.conf", "NAME = Space\nACTION = key:Space\n");
    ButtonModeSet set = loadButtonModes(g_dir, &w);
    assert(w.empty());
    assert(set.modes.size() == kBuiltin + 3);
    const ButtonMode *b = set.find("latch");
    assert(b && b->label == "Navigation latch" && b->description == "Toggles navigation" &&
           b->type == ButtonActionType::ToggleNav && b->action == "toggle:nav");
    b = set.find("nextm");
    assert(b && b->label == "nextm" && b->type == ButtonActionType::ModeSwitch && b->target == "next");
    b = set.find("key");
    assert(b && b->type == ButtonActionType::Key && b->key == Key::Space && b->action == "key:space");
    assert(!set.find("missing"));

    // missing or invalid ACTION: file skipped; nested reference rejected; other keys warned
    clearDir();
    writeFile("noaction.conf", "NAME = x\n");
    writeFile("badaction.conf", "ACTION = key:nonsense\n");
    writeFile("nested.conf", "ACTION = buttonmode:latch\n");
    writeFile("extra.conf", "ACTION = click\nV_ACTION = none\nSCROLL_V_SENS = 3\n[sec]\nnoequals\n");
    w.clear();
    set = loadButtonModes(g_dir, &w);
    assert(set.modes.size() == kBuiltin + 1 && set.find("extra"));
    assert(hasWarning(w, "noaction.conf: no valid ACTION"));
    assert(hasWarning(w, "badaction.conf") && hasWarning(w, "unknown key"));
    assert(hasWarning(w, "nested.conf") && hasWarning(w, "cannot refer to another button mode"));
    assert(hasWarning(w, "V_ACTION: not allowed") && hasWarning(w, "SCROLL_V_SENS: not allowed"));
    assert(hasWarning(w, "sections are not supported") && hasWarning(w, "line without '='"));

    // ids: only valid ids are loaded; texts are cleaned; NUL, BOM, long lines
    clearDir();
    writeFile("Upper.conf", "ACTION = click\n");
    writeFile("has space.conf", "ACTION = click\n");
    writeFile("bom.conf", std::string("\xEF\xBB\xBF") + "ACTION = click\nNAME = a\x01" "b\n");
    writeFile("nulbyte.conf", std::string("ACTION = click\n") + std::string("X\0Y = 1\n", 8));
    writeFile("long.conf", "ACTION = click\n" + std::string(5000, 'x') + " = 1\n");
    writeFile("notconf.txt", "ACTION = click\n");
    w.clear();
    set = loadButtonModes(g_dir, &w);
    assert(!set.find("Upper") && !set.find("has space") && set.find("bom") && set.find("long"));
    assert(set.find("bom")->label == "ab");
    assert(hasWarning(w, "NUL byte") && hasWarning(w, "line too long"));

    // limits: file size and file count
    clearDir();
    writeFile("a-big.conf", "ACTION = click\n" + std::string(70 * 1024, '#'));
    for (int i = 0; i < 70; ++i) writeFile("b" + std::to_string(100 + i) + ".conf", "ACTION = click\n");
    w.clear();
    set = loadButtonModes(g_dir, &w);
    assert(!set.find("a-big") && hasWarning(w, "file too large"));
    assert(set.modes.size() <= 64 + kBuiltin && hasWarning(w, "too many button mode files"));

#ifndef _WIN32
    // links are not followed
    clearDir();
    writeFile("real.conf", "ACTION = click\n");
    assert(::symlink((g_dir + "/real.conf").c_str(), (g_dir + "/link.conf").c_str()) == 0);
    w.clear();
    set = loadButtonModes(g_dir, &w);
    assert(set.find("real") && !set.find("link"));
#endif

    // missing directory is no error; stamp changes with the content
    {
        std::vector<std::string> w2;
        assert(loadButtonModes(g_dir + "/nonexistent", &w2).modes.size() == kBuiltin && w2.empty());
        clearDir();
        writeFile("a.conf", "ACTION = click\n");
        const std::string s1 = buttonModesStamp(g_dir);
        assert(!s1.empty() && s1 == buttonModesStamp(g_dir));
        writeFile("a.conf", "ACTION = rightclick and longer\n");
        assert(buttonModesStamp(g_dir) != s1);
        writeFile("c.conf", "ACTION = click\n");
        assert(buttonModesStamp(g_dir) != s1);
    }
    assert(buttonModesDir().size() > 8 && buttonModesDir().rfind("/dofscroll/buttons") == buttonModesDir().size() - 18);

    // expansion: profile settings and mode bindings
    {
        clearDir();
        writeFile("rc.conf", "ACTION = rightclick\n");
        writeFile("tn.conf", "ACTION = toggle:nav\n");
        writeFile("sw.conf", "ACTION = mode:next\n");
        set = loadButtonModes(g_dir, nullptr);
        Settings s;
        std::string err;
        assert(applyKey(s, "BUTTON_1", "buttonmode:rc", &err) && s.buttons[1] == "buttonmode:rc");
        assert(applyKey(s, "BUTTON_2", "buttonmode:rc", &err));
        assert(applyKey(s, "BUTTON_3", "buttonmode:tn", &err));
        assert(applyKey(s, "BUTTON_4", "buttonmode:gone", &err));
        assert(applyKey(s, "BUTTON_5", "key:space", &err));
        assert(!applyKey(s, "BUTTON_6", "buttonmode:", &err));
        std::set<std::string> missing;
        Settings e = s;
        expandSettings(e, set, &missing);
        assert(e.buttons[1] == "rightclick" && e.buttons[2] == "rightclick" && e.buttons[3] == "toggle:nav");
        assert(e.buttons[4] == "none" && e.buttons[5] == "key:space");
        assert(missing.size() == 1 && *missing.begin() == "gone");
        assert(s.buttons[1] == "buttonmode:rc");  // the source stays as written

        ModeDef m;
        m.id = "m";
        ButtonBinding b1, b2, b3;
        b1.button = 1; b1.type = ButtonActionType::ButtonMode; b1.mode = "sw";
        b2.button = 2; b2.type = ButtonActionType::ButtonMode; b2.mode = "gone2";
        b3.button = 3; b3.type = ButtonActionType::Key; b3.key = Key::Space;
        m.buttons = {b1, b2, b3};
        missing.clear();
        const ModeDef x = expandMode(m, set, &missing);
        assert(x.buttons[0].type == ButtonActionType::ModeSwitch && x.buttons[0].mode == "next");
        assert(x.buttons[1].type == ButtonActionType::Unbound && x.buttons[1].button == 2);
        assert(x.buttons[2].type == ButtonActionType::Key);
        assert(missing.size() == 1 && *missing.begin() == "gone2");
        assert(m.buttons[0].type == ButtonActionType::ButtonMode);  // the original is unchanged
    }

    // CYCLE: list parsing, canonical action text, ignored with other actions
    {
        std::vector<std::string> ids, dups;
        std::string err;
        assert(parseModeCycleList(" Slides, media  map,slides ", &ids, &err, &dups));
        assert((ids == std::vector<std::string>{"slides", "media", "map"}) && dups.size() == 1 && dups[0] == "slides");
        assert(!parseModeCycleList("", &ids, &err) && !parseModeCycleList(" , ", &ids, &err));
        assert(!parseModeCycleList("a, Bad.Id", &ids, &err) && !parseModeCycleList("a,b/c", &ids, &err));
        std::string many;
        for (size_t i = 0; i < kMaxModeCycle; i++) many += "m" + std::to_string(i) + ",";
        assert(parseModeCycleList(many, &ids, &err) && ids.size() == kMaxModeCycle);
        assert(!parseModeCycleList(many + "extra", &ids, &err) && err.find("too many") != std::string::npos);

        ButtonActionType t;
        Key k;
        std::string id;
        assert(parseButtonAction("mode:next:A, b ,c", &t, &k, &err, &id) && t == ButtonActionType::ModeSwitch &&
               id == "next:a,b,c");
        assert(parseButtonAction("mode:prev:x", &t, &k, &err, &id) && id == "prev:x");
        assert(!parseButtonAction("mode:next:", &t, &k, &err) && !parseButtonAction("mode:next:Bad.Id", &t, &k, &err));
        bool fwd = false;
        std::vector<std::string> l;
        assert(splitCycleTarget("next", &fwd, &l) && fwd && l.empty());
        assert(splitCycleTarget("prev:a,b", &fwd, &l) && !fwd && l.size() == 2);
        assert(!splitCycleTarget("nextx", &fwd, &l) && !splitCycleTarget("timeline", &fwd, &l) && !splitCycleTarget("next:", &fwd, &l));

        clearDir();
        writeFile("cyc.conf", "CYCLE = b, a, b, ghost\nACTION = mode:next\n");
        writeFile("back.conf", "ACTION = mode:prev\nCYCLE = a b\n");
        writeFile("wrong.conf", "ACTION = click\nCYCLE = a\n");
        writeFile("bad.conf", "ACTION = mode:next\nCYCLE = Bad.Id\n");
        w.clear();
        set = loadButtonModes(g_dir, &w);
        assert(set.modes.size() == kBuiltin + 4);
        const ButtonMode *c = set.find("cyc");
        assert(c && c->target == "next:b,a,ghost" && c->action == "mode:next:b,a,ghost" && c->cycle.size() == 3);
        assert(hasWarning(w, "duplicate 'b'"));
        assert(set.find("back")->target == "prev:a,b");
        assert(set.find("wrong")->cycle.empty() && set.find("wrong")->type == ButtonActionType::Click);
        assert(hasWarning(w, "wrong.conf:2: CYCLE needs ACTION"));
        assert(set.find("bad")->target == "next" && hasWarning(w, "CYCLE: mode id expected"));
        Settings s;
        s.buttons[1] = "buttonmode:cyc";
        std::set<std::string> missing;
        expandSettings(s, set, &missing);
        assert(s.buttons[1] == "mode:next:b,a,ghost");
    }

    tu::removeAll(g_dir);
    std::puts("test_button_modes OK");
    return 0;
}
