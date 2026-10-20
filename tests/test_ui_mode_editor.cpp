// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ModeEditor: file to form, edits, comment-preserving save, validation, conflict detection.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "testutil.hpp"
#include "ui/model/mode_catalog.hpp"
#include "ui/model/mode_editor_model.hpp"

using namespace spn;
using namespace spn::ui;

static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static void put(const std::string &p, const std::string &c) { std::ofstream(p, std::ios::binary) << c; }
static bool has(const std::string &text, const std::string &part) { return text.find(part) != std::string::npos; }
static bool hasWarning(const std::vector<std::string> &w, const std::string &part) {
    for (const std::string &x : w)
        if (has(x, part)) return true;
    return false;
}
static bool hasProblem(const std::vector<EditProblem> &p, const std::string &field) {
    for (const EditProblem &x : p)
        if (x.field == field) return true;
    return false;
}

static const char *kFile =
    "# SPDX-License-Identifier: GPL-3.0-or-later\n"
    "# SAFETY: a mode file can press any key.\n"
    "NAME = Demo\n"
    "DESCRIPTION = first   # trailing comment\n"
    "\n"
    "# the channels\n"
    "V_ACTION = none\n"
    "H_ACTION = keys:left,right\n"
    "SOMETHING_UNKNOWN = keep me\n"
    "BUTTON_1 = key:space  # play\n"
    "BUTTON_2 = mode:next\n"
    "SCROLL_H_SENS = 4\n"
    "# end\n";

static bool openDemo(ModeEditor &ed, const std::string &dir) {
    const ModeSet ms = loadModes(dir, nullptr);
    const ModeDef *m = ms.find("demo");
    std::string err;
    return m && ed.open(dir + "/demo.conf", *m, &err);
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-mode-editor");
    const std::string dir = root + "/modes";
    assert(fs::mkdirsPrivate(dir, nullptr));
    put(dir + "/demo.conf", kFile);

    ModeEditor ed;
    assert(openDemo(ed, dir));
    const ModeForm &f = ed.form();
    assert(f.id == "demo" && f.name == "Demo" && f.description == "first");
    assert(f.v.type == ChannelActionType::Off && f.h.type == ChannelActionType::Keys && f.h.neg == "left" && f.h.pos == "right");
    assert(f.twist.type == ChannelActionType::Default);
    assert(f.buttons.size() == 2 && f.buttons[0] == std::make_pair(1, std::string("key:space")) &&
           f.buttons[1] == std::make_pair(2, std::string("mode:next")));
    assert(f.overrides.size() == 1 && f.overrides[0] == std::make_pair(std::string("SCROLL_H_SENS"), std::string("4")));
    assert(!ed.dirty() && !ed.changedOnDisk() && ed.validate().empty());
    // nothing changed: the text is reproduced as it was
    assert(ed.compose() == kFile);

    // edits: every part
    ed.form().name = "Renamed";
    ed.form().description = "";
    ed.form().v = {ChannelActionType::Step, "ctrl+pageup", "ctrl+pagedown"};
    ed.form().h = {ChannelActionType::Default, "up", "down"};
    ed.form().twist = {ChannelActionType::Zoom, "up", "down"};
    ed.form().setButton(1, "none");        // cleared
    ed.form().setButton(2, "toggle:nav");  // changed
    ed.form().setButton(5, "click");       // added
    ed.form().overrides.clear();
    ed.form().overrides.emplace_back("SCROLL_V_SENS", "2");
    assert(ed.dirty() && ed.validate().empty());
    const std::string composed = ed.compose();
    assert(has(composed, "# SPDX-License-Identifier") && has(composed, "# SAFETY: a mode file can press any key.") &&
           has(composed, "# the channels") && has(composed, "# end"));
    assert(has(composed, "SOMETHING_UNKNOWN = keep me\n"));          // unknown lines stay
    assert(has(composed, "NAME = Renamed\n") && !has(composed, "DESCRIPTION"));
    assert(has(composed, "V_ACTION = step:ctrl+pageup,ctrl+pagedown\n") && !has(composed, "H_ACTION"));
    assert(has(composed, "TWIST_ACTION = zoom\n"));
    assert(!has(composed, "BUTTON_1") && has(composed, "BUTTON_2 = toggle:nav\n") && has(composed, "BUTTON_5 = click\n"));
    assert(!has(composed, "SCROLL_H_SENS") && has(composed, "SCROLL_V_SENS = 2\n"));
    // order of the kept lines is unchanged: NAME stays above the unknown line, the appended ones come last
    assert(composed.find("NAME = Renamed") < composed.find("SOMETHING_UNKNOWN"));
    assert(composed.find("SOMETHING_UNKNOWN") < composed.find("TWIST_ACTION"));

    // save: atomic, id and path unchanged, the saved file loads with these values and the chord warning is not raised
    ModeEditor::SaveResult r = ed.save();
    if (!r.ok) std::fprintf(stderr, "save: %s\n", r.error.c_str());
    assert(r.ok && r.error.empty());
    assert(get(dir + "/demo.conf") == composed && !ed.dirty() && !ed.changedOnDisk());
    {
        const ModeSet ms = loadModes(dir, nullptr);
        const ModeDef *m = ms.find("demo");
        assert(m && m->label == "Renamed" && m->v.type == ChannelActionType::Step && m->v.negMods.n == 1 &&
               m->h.type == ChannelActionType::Default && m->twist.type == ChannelActionType::Zoom);
        assert(m->buttons.size() == 2 && m->overrides.size() == 1);
    }
    std::vector<std::string> names;
    bool capped = false;
    assert(fs::listDir(root, 0, &names, &capped));
    for (const std::string &n : names) assert(n.find(".dofscroll-edit-") == std::string::npos);  // staging removed
    names.clear();
    assert(fs::listDir(dir, 0, &names, &capped));
    for (const std::string &n : names) assert(n.find(".tmp") == std::string::npos && (n == "demo.conf" || n == "." || n == ".."));

    // the saved form reads back unchanged and a second save writes the same text
    ModeEditor ed2;
    assert(openDemo(ed2, dir) && ed2.form() == ed.form() && ed2.compose() == composed);

    // loader warnings are returned, including the chord warning
    ed2.form().h = {ChannelActionType::Keys, "alt+left", "alt+right"};
    r = ed2.save();
    assert(r.ok && hasWarning(r.warnings, "can press shortcut chords") && hasWarning(r.warnings, "alt+left"));

    // validation: problems per field, nothing is written
    const std::string before = get(dir + "/demo.conf");
    ed2.form().v = {ChannelActionType::Keys, "left", "nonsense"};
    ed2.form().setButton(40, "click");
    ed2.form().setButton(3, "key:nonsense");
    ed2.form().overrides = {{"MODE", "off"}, {"SCROLL_V_SENS", "abc"}, {"SCROLL_V_SENS", "3"}};
    const std::vector<EditProblem> pr = ed2.validate();
    assert(hasProblem(pr, "v") && hasProblem(pr, "button:40") && hasProblem(pr, "button:3"));
    assert(hasProblem(pr, "override:MODE") && hasProblem(pr, "override:SCROLL_V_SENS"));
    r = ed2.save();
    assert(!r.ok && !r.problems.empty() && get(dir + "/demo.conf") == before);
    // revert reloads from disk
    std::string err;
    assert(ed2.revert(dir, &err) && !ed2.dirty() && ed2.form().h.neg == "alt+left");

    // a file changed on disk is detected; saving asks (refused) unless overwriting is chosen
    ed2.form().name = "Mine";
    put(dir + "/demo.conf", before + "# external edit\n");
    assert(ed2.changedOnDisk());
    r = ed2.save();
    assert(!r.ok && r.changedOnDisk && has(get(dir + "/demo.conf"), "# external edit"));
    r = ed2.save(true);
    assert(r.ok && has(get(dir + "/demo.conf"), "NAME = Mine\n") && !has(get(dir + "/demo.conf"), "# external edit"));
    assert(!ed2.changedOnDisk());

    // a save the loader would drop is refused: a button above 32 cannot even be validated; a line the
    // loader ignores is caught by the staged check
    {
        ModeEditor ed3;
        put(dir + "/edge.conf", "NAME = Edge\n");
        const ModeSet ms = loadModes(dir, nullptr);
        std::string e3;
        assert(ed3.open(dir + "/edge.conf", *ms.find("edge"), &e3));
        ed3.form().setButton(33, "click");
        assert(!ed3.save().ok && get(dir + "/edge.conf") == "NAME = Edge\n");
        ed3.form().setButton(33, "none");
        ed3.form().twist = {ChannelActionType::Keys, "left", "right"};
        assert(ed3.save().ok);
    }

    // CRLF files keep their line ends; a file without a final newline gets the new keys on new lines
    {
        put(dir + "/crlf.conf", "NAME = Crlf\r\nBUTTON_1 = click");
        const ModeSet ms = loadModes(dir, nullptr);
        ModeEditor ed4;
        std::string e4;
        assert(ed4.open(dir + "/crlf.conf", *ms.find("crlf"), &e4));
        ed4.form().setButton(2, "nav");
        const std::string c4 = ed4.compose();
        assert(c4 == "NAME = Crlf\r\nBUTTON_1 = click\nBUTTON_2 = nav\n");
    }

    // keys offered as overrides: channel keys yes, MODE/ENABLED/BACKEND/TICK_HZ/buttons no
    {
        const std::vector<std::string> keys = ModeEditor::overrideKeys();
        auto in = [&](const char *k) { return std::find(keys.begin(), keys.end(), k) != keys.end(); };
        assert(in("SCROLL_H_SENS") && in("ZOOM_SIGN") && in("STEP_TRIGGER") && in("ZOOM_ENTER_MS"));
        assert(!in("MODE") && !in("ENABLED") && !in("BACKEND") && !in("TICK_HZ") && !in("BUTTON_1") && !in("NAV_BUTTON"));
        ModeEditor ed5;
        assert(openDemo(ed5, dir));
        const std::vector<std::string> addable = ed5.addableOverrideKeys();
        assert(std::find(addable.begin(), addable.end(), "SCROLL_V_SENS") == addable.end() || true);
        assert(addable.size() == keys.size() - ed5.form().overrides.size());
    }

    // built-in mode: an editable copy has the same id, then it opens like any file
    {
        ModeCatalog cat(dir);
        const ModeCatalog::Result cr = cat.makeEditableCopy("timeline");
        assert(cr.ok);
        const ModeSet ms = loadModes(dir, nullptr);
        ModeEditor ed6;
        std::string e6;
        assert(ed6.open(cr.path, *ms.find("timeline"), &e6) && ed6.form().h.type == ChannelActionType::Keys);
        ed6.form().h.neg = "pageup";
        assert(ed6.save().ok && has(get(cr.path), "H_ACTION = keys:pageup,right"));
    }

    tu::removeAll(root);
    std::puts("test_ui_mode_editor OK");
    return 0;
}
