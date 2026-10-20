// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "New from example": the shipped examples that are not installed yet, installing one, and the
// origin texts of the list entries.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <fstream>
#include <string>

#include "testutil.hpp"
#include "ui/model/examples_model.hpp"

using namespace spn;
using namespace spn::ui;

static void put(const std::string &p, const std::string &c) { std::ofstream(p, std::ios::binary) << c; }
static bool has(const std::vector<Example> &v, const std::string &id) {
    for (const Example &e : v)
        if (e.id == id) return true;
    return false;
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-examples");
    const std::string modesDir = root + "/modes", buttonsDir = root + "/buttons", ex = root + "/ex";
    assert(fs::mkdirsPrivate(modesDir, nullptr) && fs::mkdirsPrivate(buttonsDir, nullptr));
    assert(fs::mkdirsPrivate(ex + "/modes", nullptr) && fs::mkdirsPrivate(ex + "/buttons", nullptr));
    put(modesDir + "/mine.conf", "NAME = Mine\n");
    put(modesDir + "/timeline.conf", "NAME = My timeline\nV_ACTION = none\n");  // replaces the built-in joystick mode
    put(buttonsDir + "/latch.conf", "NAME = Latch\nACTION = toggle:nav\n");
    put(ex + "/modes/mine.conf", "NAME = Example mine\n");        // already installed (same id)
    put(ex + "/modes/slides.conf", "NAME = Slides\nDESCRIPTION = Pages\nV_ACTION = keys:pageup,pagedown\n");
    put(ex + "/buttons/latch.conf", "ACTION = toggle:nav\n");     // already installed
    put(ex + "/buttons/next.conf", "NAME = Next\nACTION = mode:next\n");

    ModeCatalog mc(modesDir);
    ButtonModeCatalog bc(buttonsDir);

    // only what is not installed is offered
    std::vector<Example> me = availableModeExamples(mc, ex), be = availableButtonModeExamples(bc, ex);
    assert(me.size() == 1 && me[0].id == "slides" && me[0].label == "Slides" && me[0].description == "Pages");
    assert(!has(me, "mine") && !has(me, "normal") && !has(me, "timeline"));
    assert(be.size() == 1 && be[0].id == "next" && be[0].label == "Next" && !has(be, "latch"));
    assert(availableModeExamples(mc, "").empty() && availableButtonModeExamples(bc, "").empty());
    assert(availableModeExamples(mc, root + "/nothing").empty());

    // installing copies the file; it is not offered again; installing twice fails
    ExampleResult r = installModeExample(mc, me[0]);
    assert(r.ok && r.id == "slides" && fs::pathExists(modesDir + "/slides.conf") && mc.find("slides"));
    assert(availableModeExamples(mc, ex).empty());
    assert(!installModeExample(mc, me[0]).ok);
    r = installButtonModeExample(bc, be[0]);
    assert(r.ok && r.id == "next" && fs::pathExists(buttonsDir + "/next.conf") && bc.find("next"));
    assert(availableButtonModeExamples(bc, ex).empty());
    assert(!installButtonModeExample(bc, be[0]).ok);

    // deleting an installed example offers it again; a file that replaces a built-in mode brings the built-in one back
    assert(mc.remove("slides").ok && availableModeExamples(mc, ex).size() == 1);
    assert(mc.remove("mine").ok && has(availableModeExamples(mc, ex), "mine"));
    assert(mc.remove("timeline").ok && mc.find("timeline")->origin == ModeCatalog::Origin::BuiltIn);

    // origin texts
    const IdentityStrings id;
    assert(modeOriginText(ModeCatalog::Origin::BuiltIn, id) == "Built in");
    assert(modeOriginText(ModeCatalog::Origin::File, id) == "Your file");
    assert(modeOriginText(ModeCatalog::Origin::BuiltInOverridden, id) == "Your file, replaces the built-in joystick mode");
    assert(buttonModeOriginText(id) == "Your file" && exampleOriginText(id) == "Example, not installed yet");

    tu::removeAll(root);
    std::puts("test_ui_examples: ok");
    return 0;
}
