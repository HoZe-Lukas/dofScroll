// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/ui/model/mode_catalog.hpp.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "testutil.hpp"
#include "ui/model/mode_catalog.hpp"

using namespace spn;
using namespace spn::ui;
using Origin = ModeCatalog::Origin;

static std::string put(const std::string &path, const std::string &text) {
    std::string err;
    if (!fs::writeFileReplace(path, text, &err)) {
        std::fprintf(stderr, "writeFileReplace(%s) failed: %s\n", path.c_str(), err.c_str());
        std::abort();
    }
    return path;
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-cat");
    const std::string dir = root + "/modes";
    std::string stamp0;

    // ---- ids ----
    assert(ModeCatalog::deriveId("My Mode!") == "my-mode");
    assert(ModeCatalog::deriveId("  A  B__C ") == "a-b__c");
    assert(ModeCatalog::deriveId("???").empty() && ModeCatalog::deriveId("").empty());
    assert(ModeCatalog::deriveId("CON") == "con-mode" && validModeId("con-mode"));
    assert(ModeCatalog::deriveId("nul") == "nul-mode");
    assert(ModeCatalog::deriveId(std::string(60, 'a')).size() <= 32);
    assert(validModeId(ModeCatalog::deriveId(std::string(60, 'a'))));
    assert(ModeCatalog::deriveId("Zoom \xC3\xA4\xC3\xB6") == "zoom");
    assert(ModeCatalog::cleanLabel("  a\nb\t ") == "a b" && ModeCatalog::cleanLabel("\n").empty());
    assert(ModeCatalog::cleanLabel(std::string(100, 'x')).size() == 64);

    ModeCatalog c(dir);  // directory missing: built-ins only (normal, off, timeline + six embedded example modes)
    assert(c.modes().size() == 9 && c.stamp().empty());
    for (const auto &e : c.modes()) assert(e.origin == Origin::BuiltIn && e.path.empty() && e.warnings.empty());
    assert(c.find("timeline") && !c.find("nope"));
    for (const char *id : {"slides", "media", "map", "precision", "tabs", "browse"}) {
        const auto *e = c.find(id);
        assert(e && e->origin == Origin::BuiltIn && e->path.empty() && !e->label.empty());
    }
    // an embedded mode gets an editable copy like a code-defined one and keeps its channels
    {
        auto cp = c.makeEditableCopy("tabs");
        assert(cp.ok && c.find("tabs")->origin == Origin::BuiltInOverridden && c.find("tabs")->path == dir + "/tabs.conf");
        const spn::ModeDef *t = c.modeSet().find("tabs");
        assert(t && !t->builtin && t->h.type == spn::ChannelActionType::Keys && t->h.neg == spn::Key::PageUp);
        assert(c.remove("tabs").ok && c.find("tabs")->origin == Origin::BuiltIn);  // the built-in comes back
        assert(!c.remove("browse").ok);  // a built-in without a file cannot be removed
    }
    assert(c.uniqueId("Timeline") == "timeline-2");
    assert(c.uniqueId("Fresh One") == "fresh-one");

    // ---- createNew ----
    auto r = c.createNew("My Mode");
    assert(r.ok && r.id == "my-mode" && r.path == dir + "/my-mode.conf" && tu::exists(r.path));
#ifndef _WIN32
    struct stat st;
    assert(::stat(r.path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
#endif
    const ModeCatalog::ModeEntry *e = c.find("my-mode");
    assert(e && e->origin == Origin::File && e->label == "My Mode" && e->path == r.path && e->warnings.empty());
    assert(c.generalWarnings().empty());
    {  // the template documents the syntax
        std::string txt;
        assert(fs::readFileBounded(r.path, 65536, fs::ReadOptions(), &txt).ok());
        for (const char *k : {"V_ACTION", "H_ACTION", "TWIST_ACTION", "BUTTON_1", "from 1", "NAME = My Mode"})
            assert(txt.find(k) != std::string::npos);
    }
    assert(!c.createNew("my mode").ok && !c.createNew("Normal").ok && !c.createNew("???").ok);
    assert(!c.createNew("").ok);
    const std::string before = [&] { std::string t; fs::readFileBounded(r.path, 65536, fs::ReadOptions(), &t); return t; }();
    assert(!c.createNew("My_Mode ").ok || true);  // different id (my_mode) is allowed
    std::string after;
    fs::readFileBounded(r.path, 65536, fs::ReadOptions(), &after);
    assert(before == after);  // never overwritten
    assert(!c.stamp().empty());

    // ---- duplicate / editable copy ----
    r = c.duplicate("timeline", "Timeline Copy");
    assert(r.ok && r.id == "timeline-copy");
    e = c.find("timeline-copy");
    assert(e && e->origin == Origin::File);
    const ModeDef *a = c.modeSet().find("timeline"), *b = c.modeSet().find("timeline-copy");
    assert(a && b && a->h == b->h && a->twist == b->twist && a->v == b->v && b->label == "Timeline Copy");
    assert(!c.duplicate("nope", "X").ok && !c.duplicate("timeline", "Timeline Copy").ok);
    assert(!c.duplicate("timeline", "").ok);
    r = c.duplicate("normal", "Plain");
    assert(r.ok && c.find("plain"));

    r = c.makeEditableCopy("timeline");
    assert(r.ok && r.id == "timeline" && c.find("timeline")->origin == Origin::BuiltInOverridden);
    assert(c.find("timeline")->path == dir + "/timeline.conf");
    assert(!c.makeEditableCopy("timeline").ok && !c.makeEditableCopy("my-mode").ok && !c.makeEditableCopy("zz").ok);
    assert(c.modes().size() == 9 + 4);  // my-mode, timeline-copy, plain + timeline replaced in place

    // ---- remove ----
    assert(!c.remove("normal").ok && !c.remove("off").ok && !c.remove("nope").ok);
    assert(c.remove("timeline").ok && c.find("timeline")->origin == Origin::BuiltIn);  // restored
    assert(!tu::exists(dir + "/timeline.conf"));
    assert(c.remove("plain").ok && !c.find("plain") && !tu::exists(dir + "/plain.conf"));
    assert(c.remove("timeline-copy").ok);

    // ---- warnings per mode (chord warning, ignored line) ----
    put(dir + "/risky.conf", "NAME = Risky\nBUTTON_1 = key:ctrl\nBUTTON_0 = click\n");
    assert(c.refreshIfChanged() && !c.refreshIfChanged());
    e = c.find("risky");
    assert(e && e->warnings.size() >= 2);
    bool chord = false, ignored = false;
    for (auto &w : e->warnings) {
        chord = chord || w.find("shortcut chords") != std::string::npos;
        ignored = ignored || w.find("numbered from 1") != std::string::npos;
    }
    assert(chord && ignored);
    assert(c.find("my-mode")->warnings.empty());
    assert(c.remove("risky").ok);

    // ---- import ----
    const std::string src = root + "/Cool Mode.conf";
    put(src, "NAME = Cool\nH_ACTION = keys:left,right\nBUTTON_1 = key:alt\nBOGUS LINE\n");
    auto pv = c.inspectImport(src);
    assert(pv.ok && pv.id == "cool-mode" && !pv.warnings.empty());
    assert(!tu::exists(dir + "/cool-mode.conf"));  // inspect copies nothing
    {
        bool chordW = false, lineW = false;
        for (auto &w : pv.warnings) {
            chordW = chordW || w.find("shortcut chords") != std::string::npos;
            lineW = lineW || w.find("without '='") != std::string::npos;
            assert(w.find(".dofscroll-import") == std::string::npos);  // staging path hidden
        }
        assert(chordW && lineW);
    }
    // No staging directory left behind.
    {
        std::vector<std::string> names;
        bool capped = false;
        assert(fs::listDir(root, 0, &names, &capped));
        for (auto &n : names) assert(n.find("import") == std::string::npos);
    }
    r = c.importFile(src, "Imported!");
    assert(r.ok && r.id == "imported" && r.path == dir + "/imported.conf" && !r.warnings.empty());
    assert(c.find("imported") && c.find("imported")->label == "Cool" && c.find("imported")->origin == Origin::File);
    assert(!c.importFile(src, "Imported").ok);  // exists, never overwritten
    assert(!c.importFile(src, "normal").ok);    // built-in id
    r = c.importFile(src);                       // id from the file name
    assert(r.ok && r.id == "cool-mode");
    assert(!c.importFile(src).ok);

    // Rejections.
    assert(!c.inspectImport(root + "/missing.conf").ok);
    assert(!c.inspectImport(root).ok);  // directory
    put(root + "/nulbyte.conf", std::string("NAME = x\n") + std::string(1, '\0') + "V_ACTION = none\n");
    assert(!c.inspectImport(root + "/nulbyte.conf", "nulfile").ok);
    put(root + "/big.conf", std::string(70 * 1024, '#'));
    assert(!c.inspectImport(root + "/big.conf", "big").ok);
    std::string many;
    for (int i = 0; i < 1100; ++i) many += "#\n";
    put(root + "/many.conf", many);
    assert(!c.inspectImport(root + "/many.conf", "many").ok);
    put(root + "/long.conf", "NAME = " + std::string(5000, 'x') + "\n");
    assert(!c.inspectImport(root + "/long.conf", "long").ok);
    put(root + "/ok.conf", "NAME = ok\n");
    assert(!c.inspectImport(root + "/ok.conf", "???").ok);  // no usable id
    assert(c.inspectImport(root + "/ok.conf", "CON").id == "con-mode");
    assert(!c.inspectImport(root + "/ok.conf", "Timeline").ok);

    // ---- stamp / refresh ----
    const std::string s1 = c.stamp();
    assert(!s1.empty() && !c.refreshIfChanged());
    put(dir + "/late.conf", "NAME = Late\n");
    assert(c.stamp() != s1 && c.refreshIfChanged() && c.find("late"));
    (void)stamp0;

    // ---- used by profiles ----
    {
        ProfileSet ps;
        ps.sections[0].kv = {{"BUTTON_4", "hold:mode:late"}, {"MODE", "late"}};  // MODE in [global] is not a use
        const size_t appIdx = ps.findOrAdd(Section::APP, "firefox");
        ps.sections[appIdx].kv = {{"mode", "late"}, {"BUTTON_1", "toggle:mode:late"}, {"BUTTON_2", "mode:late"},
                             {"BUTTON_3", "mode:other"}, {"BUTTON_5", "buttonmode:late"}, {"BUTTON_6", "key:space"}};
        const size_t d = ps.findOrAdd(Section::DEVICE, "SpaceMouse");
        ps.sections[d].kv = {{"MODE", "other"}};
        const std::vector<ModeCatalog::ModeUse> u = ModeCatalog::usedBy(ps, "late");
        assert(u.size() == 2);
        assert(u[0].section == 0 && !u[0].selects && u[0].buttons == std::vector<int>({4}));
        assert(u[1].kind == Section::APP && u[1].selects && u[1].buttons == std::vector<int>({1, 2}));
        assert(ModeCatalog::useTitle(u[0]) == "[global]" && ModeCatalog::useTitle(u[1]) == "[app:firefox]");
        assert(ModeCatalog::usedBy(ps, "other").size() == 2);   // the device selects it, the app has a button
        assert(ModeCatalog::usedBy(ps, "nosuch").empty() && ModeCatalog::usedBy(ps, "").empty());
    }

    // ---- export ----
    {
        put(dir + "/exp.conf", "NAME = Exp\nBUTTON_1 = key:space\n");
        c.refresh();
        const std::string out = root + "/exported.conf";
        assert(c.exportTo("exp", out).ok && tu::exists(out));
        assert(!c.exportTo("exp", out).ok);  // never overwrites
        assert(!c.exportTo("nope", root + "/x.conf").ok && !c.exportTo("exp", "").ok);
        // the export is a valid mode file for the importer
        assert(c.inspectImport(out, "exp-again").ok);
        // a built-in is exported as its template
        const std::string bo = root + "/builtin.conf";
        assert(c.exportTo("timeline", bo).ok && c.inspectImport(bo, "tl-copy").ok);
    }

    // ---- rename ----
    {
        put(dir + "/old-one.conf", "# keep me\nNAME = Old one\nname = twice\nBUTTON_1 = key:space\n");
        put(dir + "/taken.conf", "NAME = Taken\n");
        c.refresh();
        assert(ModeCatalog::withName("A = 1\n", "X") == "NAME = X\nA = 1\n");
        assert(!c.rename("normal", "Normal two").ok);                 // built-in
        assert(!c.rename("timeline", "Timeline two").ok);             // built-in
        assert(!c.rename("old-one", "Taken").ok && tu::exists(dir + "/old-one.conf"));  // id taken
        assert(!c.rename("old-one", "???").ok && !c.rename("nope", "X").ok);
        const ModeCatalog::Result renamed = c.rename("old-one", "Brand New");
        assert(renamed.ok && renamed.id == "brand-new" && !tu::exists(dir + "/old-one.conf") && tu::exists(dir + "/brand-new.conf"));
        assert(!c.find("old-one") && c.find("brand-new") && c.find("brand-new")->label == "Brand New");
        std::string text;
        assert(fs::readFileBounded(dir + "/brand-new.conf", 4096, fs::ReadOptions(), &text).ok());
        assert(text == "# keep me\nNAME = Brand New\nBUTTON_1 = key:space\n");
        // same id, new display name: rewritten in place
        const ModeCatalog::Result r2 = c.rename("brand-new", "Brand-New!");
        assert(r2.ok && r2.id == "brand-new" && c.find("brand-new")->label == "Brand-New!");
    }

    IdentityStrings id;
    assert(ModeCatalog::importSafetyNote(id).find("any key") != std::string::npos);

    tu::removeAll(root);
    puts("test_ui_mode_catalog: ok");
    return 0;
}
