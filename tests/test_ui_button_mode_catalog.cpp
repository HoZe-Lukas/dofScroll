// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ButtonModeCatalog: create, duplicate, save, import, remove, references and change detection.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "testutil.hpp"
#include "ui/model/button_mode_catalog.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace spn;
using namespace spn::ui;

static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static void put(const std::string &p, const std::string &c) { std::ofstream(p, std::ios::binary) << c; }

static bool hasWarning(const std::vector<std::string> &w, const std::string &part) {
    for (const std::string &x : w)
        if (x.find(part) != std::string::npos) return true;
    return false;
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-bmcat");
    const std::string dir = root + "/buttons";
    ButtonModeCatalog c(dir);
    // the built-in button modes are there without any file
    assert(c.buttonModes().size() == 4 && c.generalWarnings().empty());
    {
        const ButtonModeCatalog::Entry *ln = c.find("latch-nav");
        assert(ln && ln->builtin && !ln->overridesBuiltin && ln->path.empty() && ln->action == "toggle:nav");
        assert(!c.save("latch-nav", "x", "", "click").ok);  // no file to rewrite
        assert(!c.remove("latch-nav").ok);
        assert(!c.createNew("latch-nav").ok);  // the id is taken by a built-in
        // editable copy: a file with the same id replaces the built-in one; removing it restores it
        ButtonModeCatalog::Result cp = c.makeEditableCopy("latch-nav");
        assert(cp.ok && cp.path == dir + "/latch-nav.conf" && !c.makeEditableCopy("latch-nav").ok);
        ln = c.find("latch-nav");
        assert(ln && !ln->builtin && ln->overridesBuiltin && ln->path == cp.path && ln->action == "toggle:nav");
        assert(c.save("latch-nav", "Mine", "", "click").ok && c.find("latch-nav")->action == "click");
        assert(!c.makeEditableCopy("nope").ok);
        assert(c.remove("latch-nav").ok);
        ln = c.find("latch-nav");
        assert(ln && ln->builtin && ln->action == "toggle:nav" && c.buttonModes().size() == 4);
        // duplicate a built-in under a new name
        ButtonModeCatalog::Result d = c.duplicate("next-mode", "My next");
        assert(d.ok && c.find("my-next") && !c.find("my-next")->builtin && c.find("my-next")->action == "mode:next");
        assert(c.remove("my-next").ok);
    }

    // create: unique id from the name, template, 0600, no overwrite
    ButtonModeCatalog::Result r = c.createNew("My Latch!");
    assert(r.ok && r.id == "my-latch" && r.path == dir + "/my-latch.conf");
    const std::string tpl = get(r.path);
    assert(tpl.find("NAME = My Latch!") != std::string::npos && tpl.find("ACTION = none") != std::string::npos);
    assert(tpl.find("Documentation: docs/button-modes.md") != std::string::npos);
#ifndef _WIN32
    struct stat st;
    assert(::stat(r.path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(::stat(dir.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
#endif
    assert(c.buttonModes().size() == 5 && c.find("my-latch")->label == "My Latch!" && c.find("my-latch")->action == "none");
    assert(c.generalWarnings().empty() && c.find("my-latch")->warnings.empty());  // the template loads cleanly
    assert(!c.createNew("my latch").ok);  // same id
    assert(!c.createNew("!!!").ok && !c.createNew("").ok);
    assert(c.uniqueId("My Latch") == "my-latch-2" && c.uniqueId("Other") == "other");
    const ButtonModeCatalog::Result no = c.createNew("My Latch");
    assert(!no.ok && get(dir + "/my-latch.conf") == tpl);  // nothing overwritten

    // save: id unchanged, leading comment block kept, atomic replace
    r = c.save("my-latch", "Latch nav", "Press to latch", " Toggle:NAV ");
    assert(r.ok && r.id == "my-latch");
    const std::string saved = get(dir + "/my-latch.conf");
    assert(saved.find("# Button mode \"my-latch\"") == 0);
    assert(saved.find("NAME = Latch nav\n") != std::string::npos && saved.find("DESCRIPTION = Press to latch\n") != std::string::npos &&
           saved.find("ACTION = toggle:nav\n") != std::string::npos);
    assert(saved.find("NAME = My Latch!") == std::string::npos);
    assert(c.find("my-latch")->action == "toggle:nav" && c.find("my-latch")->label == "Latch nav");
    assert(c.generalWarnings().empty() && c.find("my-latch")->warnings.empty());
    // invalid and nested actions are refused and change nothing
    assert(!c.save("my-latch", "x", "", "key:nonsense").ok);
    r = c.save("my-latch", "x", "", "buttonmode:other");
    assert(!r.ok && r.error.find("another button mode") != std::string::npos);
    assert(!c.save("my-latch", "x", "", "").ok && !c.save("nope", "x", "", "click").ok);
    assert(get(dir + "/my-latch.conf") == saved);
    // an empty description is left out; an empty name falls back to the id
    assert(c.save("my-latch", "", "", "key:Space").ok);
    assert(get(dir + "/my-latch.conf").find("DESCRIPTION") == std::string::npos);
    assert(c.find("my-latch")->label == "my-latch" && c.find("my-latch")->action == "key:space");

    // duplicate: new id, same action and description
    assert(c.save("my-latch", "Latch nav", "Press to latch", "toggle:nav").ok);
    r = c.duplicate("my-latch", "Copy of it");
    assert(r.ok && r.id == "copy-of-it" && c.find("copy-of-it")->action == "toggle:nav" &&
           c.find("copy-of-it")->description == "Press to latch");
    assert(!c.duplicate("my-latch", "Copy of it").ok && !c.duplicate("missing", "X").ok);

    // import: checks, warnings, no overwrite
    const std::string src = root + "/incoming.conf";
    put(src, "NAME = Imported\nACTION = mode:next\nV_ACTION = none\n");
    ButtonModeCatalog::Result ins = c.inspectImport(src);
    assert(ins.ok && ins.id == "incoming" && hasWarning(ins.warnings, "V_ACTION: not allowed"));
    assert(!c.find("incoming"));  // inspect keeps nothing
    ins = c.importFile(src, "Imported mode");
    assert(ins.ok && ins.id == "imported-mode" && c.find("imported-mode")->action == "mode:next");
    assert(!c.importFile(src, "Imported mode").ok);  // id taken, nothing overwritten
    put(src, "NAME = no action\n");
    assert(!c.inspectImport(src).ok);
    put(src, std::string("ACTION = click\n") + std::string("x\0y\n", 4));
    assert(!c.inspectImport(src).ok);
    put(src, std::string(70 * 1024, '#'));
    assert(!c.inspectImport(src).ok);
    put(src, std::string(5000, 'a'));
    assert(!c.inspectImport(src).ok);
    put(src, "ACTION = click\n");
    assert(!c.inspectImport(root + "/missing.conf").ok && !c.inspectImport(root).ok);
    assert(c.inspectImport(src, "Brand new").ok);
    assert(!ButtonModeCatalog::importSafetyNote(IdentityStrings()).empty());
    assert(c.importFile(src, "ok one").ok && c.find("ok-one")->action == "click");
    // nothing left behind in the staging area
    std::vector<std::string> names;
    bool capped = false;
    assert(fs::listDir(root, 0, &names, &capped));
    for (const std::string &n : names) assert(n.find(".dofscroll-import-") == std::string::npos);

    // references: profile sections and mode files
    {
        put(root + "/dofscroll.conf",
            "[global]\nBUTTON_1 = buttonmode:my-latch\n\n[app:blender]\nBUTTON_3 = buttonmode:my-latch\nBUTTON_4 = nav\n\n"
            "[device:Pad]\nBUTTON_2 = buttonmode:copy-of-it\n");
        SettingsModel sm;
        std::vector<std::string> warn;
        std::string err;
        assert(sm.load(root + "/dofscroll.conf", &warn, &err));
        sm.setDeviceInfo("", 6);
        ModeSet ms;
        ModeDef md;
        md.id = "timeline";
        ButtonBinding b;
        b.button = 5;
        b.type = ButtonActionType::ButtonMode;
        b.mode = "my-latch";
        md.buttons.push_back(b);
        ms.modes.push_back(md);
        const std::vector<std::string> refs = c.referencedBy("my-latch", sm, ms);
        assert(refs.size() == 3);
        assert(refs[0] == "[global] BUTTON_1" && refs[1] == "[app:blender] BUTTON_3" && refs[2] == "mode timeline: BUTTON_5");
        assert(c.referencedBy("copy-of-it", sm, ms).size() == 1);
        assert(c.referencedBy("unused", sm, ms).empty());
    }

    // change detection
    assert(!c.refreshIfChanged());
    put(dir + "/external.conf", "ACTION = click\n");
    assert(c.refreshIfChanged() && c.find("external"));
    assert(!c.refreshIfChanged());
    put(dir + "/external.conf", "ACTION = rightclick\n");
    assert(c.refreshIfChanged() && c.find("external")->action == "rightclick");

    // a broken file shows its warnings on the entry, a file without ACTION is a general warning
    put(dir + "/odd.conf", "ACTION = click\nSCROLL_V_SENS = 3\n");
    put(dir + "/noact.conf", "NAME = x\n");
    c.refresh();
    assert(hasWarning(c.find("odd")->warnings, "SCROLL_V_SENS"));
    assert(!c.find("noact") && hasWarning(c.generalWarnings(), "noact.conf"));

    // CYCLE list: save, reload, duplicate, model helpers
    put(dir + "/cyc.conf", "# head\nNAME = Favorites\nACTION = mode:next\n");
    c.refresh();
    assert(c.find("cyc")->cycle.empty());
    ModeCycleList l;
    assert(l.add("slides") && l.add("media") && l.add("map") && !l.add("slides") && !l.add("Bad Id"));
    assert(l.moveUp(2) && l.entries()[1] == "map" && !l.moveUp(0) && l.moveDown(0) && l.entries()[0] == "map");
    assert(!l.moveDown(2) && l.remove(1) && l.size() == 2 && !l.remove(5));
    assert(l.toText() == "map, media");
    r = c.save("cyc", "Favorites", "", "mode:next", l.entries());
    assert(r.ok);
    assert(get(dir + "/cyc.conf").find("CYCLE = map, media\n") != std::string::npos);
    assert(c.find("cyc")->cycle == l.entries() && c.find("cyc")->action == "mode:next");
    r = c.duplicate("cyc", "Favorites 2");
    assert(r.ok && c.find(r.id)->cycle == l.entries());
    assert(!c.save("cyc", "Favorites", "", "click", l.entries()).ok);  // a list needs mode:next / mode:prev
    assert(c.save("cyc", "Favorites", "", "mode:prev").ok && c.find("cyc")->cycle.empty());  // empty list: no CYCLE
    assert(get(dir + "/cyc.conf").find("CYCLE") == std::string::npos);
    {
        ModeSet ms;
        ModeDef a, b;
        a.id = "normal"; b.id = "slides";
        ms.modes = {a, b};
        ModeCycleList m(std::vector<std::string>{"slides", "ghost"});
        assert((m.candidates(ms) == std::vector<std::string>{"normal"}));
        assert((m.unknown(ms) == std::vector<std::string>{"ghost"}));
    }

    // remove
    assert(c.remove("external").ok && !c.find("external") && !fs::pathExists(dir + "/external.conf"));
    assert(!c.remove("external").ok);

    tu::removeAll(root);
    std::puts("test_ui_button_mode_catalog OK");
    return 0;
}
