// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// CycleEditorModel: rows, ticking, order, missing ids, summary text, and saving through the catalog.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>

#include "testutil.hpp"
#include "ui/model/button_mode_catalog.hpp"
#include "ui/model/cycle_editor_model.hpp"

using namespace spn;
using namespace spn::ui;

static ModeSet makeModes() {
    ModeSet s;
    for (const char *id : {"normal", "video", "timeline", "next"}) {
        ModeDef m;
        m.id = id;
        m.label = id;
        s.modes.push_back(m);
    }
    return s;
}
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

int main() {
    IdentityStrings s;
    const ModeSet modes = makeModes();
    {
        CycleEditorModel m(modes, {});
        assert(!m.onlySelected() && m.cycle().empty());
        const auto rows = m.rows();  // alphabetical, the reserved ids are not offered
        assert(rows.size() == 3 && rows[0].id == "normal" && rows[1].id == "timeline" && rows[2].id == "video");
        assert(m.summary(s).find("all joystick modes") != std::string::npos);
        m.setOnlySelected(true);  // nothing ticked: still no CYCLE
        assert(m.cycle().empty() && m.summary(s).find("Nothing ticked") != std::string::npos);
        assert(m.setChecked("timeline", true) && m.setChecked("normal", true));
        assert(!m.setChecked("normal", true));  // already ticked
        assert((m.cycle() == std::vector<std::string>{"timeline", "normal"}));
        assert(m.summary(s) == "Order: 1 timeline, 2 normal");
        const auto r2 = m.rows();  // checked first in cycle order, then the others
        assert(r2[0].id == "timeline" && r2[0].position == 1 && r2[1].id == "normal" && r2[1].position == 2 &&
               r2[2].id == "video" && r2[2].position == 0 && !r2[2].checked);
        assert(CycleEditorModel::rowText(r2[0], s) == "1. timeline");
        assert(!m.canMoveUp("timeline") && m.canMoveDown("timeline") && m.canMoveUp("normal") && !m.canMoveDown("normal"));
        assert(m.moveDown("timeline") && (m.cycle() == std::vector<std::string>{"normal", "timeline"}));
        assert(!m.moveDown("timeline") && !m.moveUp("video"));
        m.setOnlySelected(false);  // back to all: no CYCLE, the ticks are gone
        assert(m.cycle().empty() && m.selectedCount() == 0 && m.rows()[0].position == 0);
        m.setOnlySelected(true);
        assert(m.cycle().empty() && m.setChecked("normal", true) && m.setChecked("normal", false) && !m.setChecked("normal", false));
        assert(m.cycle().empty());
    }
    {  // an id of the file that no loaded mode has: shown as missing, kept, removable
        CycleEditorModel m(modes, {"gone", "normal"});
        assert(m.onlySelected() && m.missing().size() == 1 && m.missing()[0] == "gone");
        const auto rows = m.rows();
        assert(rows[0].missing && CycleEditorModel::rowText(rows[0], s) == "1. missing: gone");
        assert((m.cycle() == std::vector<std::string>{"gone", "normal"}));
        assert(!m.hint(s).empty());
        assert(m.setChecked("gone", false) && m.missing().empty() && m.rows().size() == 3);
    }
    {  // the list is limited to kMaxModeCycle
        ModeSet many;
        for (size_t i = 0; i < kMaxModeCycle + 3; ++i) {
            ModeDef d;
            d.id = "m" + std::to_string(100 + i);
            many.modes.push_back(d);
        }
        CycleEditorModel m(many, {});
        m.setOnlySelected(true);
        for (size_t i = 0; i < kMaxModeCycle; ++i) assert(m.setChecked(many.modes[i].id, true));
        assert(!m.setChecked(many.modes[kMaxModeCycle].id, true) && m.hint(s).find("full") != std::string::npos);
    }
    assert(CycleEditorModel::cyclesText({"timeline", "normal"}, s) == "Cycles: timeline, normal");
    assert(CycleEditorModel::cyclesText({}, s).empty());
    assert(CycleEditorModel::builtinNote(s).find("cycle-favorites") != std::string::npos);
    {  // the result is written by the catalog; empty = no CYCLE line and the file stays valid
        const std::string root = tu::makeTempDir("dofscroll-cycedit");
        ButtonModeCatalog c(root + "/buttons");
        const auto made = c.createNew("Next one");
        assert(made.ok);
        CycleEditorModel m(modes, {});
        m.setOnlySelected(true);
        m.setChecked("video", true);
        m.setChecked("normal", true);
        m.moveUp("normal");
        assert(c.save(made.id, "Next one", "", "mode:next", m.cycle()).ok);
        assert(get(made.path).find("CYCLE = normal, video\n") != std::string::npos);
        assert(c.find(made.id)->cycle.size() == 2 && c.find(made.id)->warnings.empty());
        m.setOnlySelected(false);
        assert(c.save(made.id, "Next one", "", "mode:next", m.cycle()).ok);
        assert(get(made.path).find("\nCYCLE") == std::string::npos && c.find(made.id)->cycle.empty());
    }
    {  // the editable copy of cycle-favorites keeps its CYCLE list
        const std::string root = tu::makeTempDir("dofscroll-cycedit2");
        ButtonModeCatalog c(root + "/buttons");
        const auto *b = c.find("cycle-favorites");
        assert(b && b->builtin && b->cycle.size() == 2);
        const std::vector<std::string> want = b->cycle;
        const auto cp = c.makeEditableCopy("cycle-favorites");
        assert(cp.ok && c.find("cycle-favorites")->cycle == want && !c.find("cycle-favorites")->builtin);
    }
    return 0;
}
