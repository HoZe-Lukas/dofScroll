// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Extension list: origin of modes and button modes, shipped examples, install and remove.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <string>

#include "testutil.hpp"
#include "ui/model/extensions_model.hpp"

using namespace spn;
using namespace spn::ui;

static void put(const std::string &p, const std::string &c) { std::ofstream(p, std::ios::binary) << c; }
static const ExtensionEntry *find(const std::vector<ExtensionEntry> &v, ExtensionEntry::Kind k, const std::string &id) {
    for (const ExtensionEntry &e : v)
        if (e.kind == k && e.id == id) return &e;
    return nullptr;
}

int main() {
    using K = ExtensionEntry::Kind;
    using O = ExtensionEntry::Origin;
    const std::string root = tu::makeTempDir("dofscroll-ext");
    const std::string modesDir = root + "/modes", buttonsDir = root + "/buttons", ex = root + "/ex";
    assert(fs::mkdirsPrivate(modesDir, nullptr) && fs::mkdirsPrivate(buttonsDir, nullptr));
    assert(fs::mkdirsPrivate(ex + "/modes", nullptr) && fs::mkdirsPrivate(ex + "/buttons", nullptr));
    put(modesDir + "/mine.conf", "NAME = Mine\n");
    put(modesDir + "/timeline.conf", "NAME = My timeline\nV_ACTION = none\n");  // replaces the built-in mode
    put(buttonsDir + "/latch.conf", "NAME = Latch\nACTION = toggle:nav\n");
    put(ex + "/modes/mine.conf", "NAME = Example mine\n");        // already installed (same id)
    put(ex + "/modes/slides.conf", "NAME = Slides\nV_ACTION = keys:pageup,pagedown\n");
    put(ex + "/buttons/latch.conf", "ACTION = toggle:nav\n");     // already installed
    put(ex + "/buttons/next.conf", "NAME = Next\nACTION = mode:next\n");

    ModeCatalog mc(modesDir);
    ButtonModeCatalog bc(buttonsDir);
    std::vector<ExtensionEntry> v = buildExtensions(mc, bc, ex);

    // origins
    const ExtensionEntry *normal = find(v, K::JoystickMode, "normal");
    assert(normal && normal->origin == O::BuiltIn && !normal->removable() && !normal->installable());
    const ExtensionEntry *mine = find(v, K::JoystickMode, "mine");
    assert(mine && mine->origin == O::UserFile && mine->removable() && mine->label == "Mine" && !mine->replacesBuiltIn);
    const ExtensionEntry *tl = find(v, K::JoystickMode, "timeline");
    assert(tl && tl->origin == O::UserFile && tl->replacesBuiltIn);
    const ExtensionEntry *latch = find(v, K::ButtonMode, "latch");
    assert(latch && latch->origin == O::UserFile && latch->label == "Latch");
    // examples that are not installed are offered, installed ones are not listed twice
    const ExtensionEntry *slides = find(v, K::JoystickMode, "slides");
    assert(slides && slides->origin == O::ExampleAvailable && slides->installable() && !slides->removable());
    const ExtensionEntry *next = find(v, K::ButtonMode, "next");
    assert(next && next->origin == O::ExampleAvailable);
    int mineCount = 0, latchCount = 0;
    for (const ExtensionEntry &e : v) {
        mineCount += e.kind == K::JoystickMode && e.id == "mine";
        latchCount += e.kind == K::ButtonMode && e.id == "latch";
    }
    assert(mineCount == 1 && latchCount == 1);
    // order: joystick modes, button modes, examples
    size_t firstButton = v.size(), lastMode = 0, firstExample = v.size();
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].kind == K::ButtonMode && v[i].origin != O::ExampleAvailable && firstButton == v.size()) firstButton = i;
        if (v[i].kind == K::JoystickMode && v[i].origin != O::ExampleAvailable) lastMode = i;
        if (v[i].origin == O::ExampleAvailable && firstExample == v.size()) firstExample = i;
    }
    assert(lastMode < firstButton && firstButton < firstExample);
    // no examples directory: no examples
    for (const ExtensionEntry &e : buildExtensions(mc, bc, "")) assert(e.origin != O::ExampleAvailable);

    // texts for the view
    IdentityStrings id;
    assert(extensionKindText(K::ButtonMode, id) == "Button mode" && extensionKindText(K::JoystickMode, id) == "Joystick mode");
    assert(extensionOriginText(*normal, id) == "built in" && extensionOriginText(*mine, id) == "your file" &&
           extensionOriginText(*tl, id) == "your file, replaces the built-in mode" &&
           extensionOriginText(*slides, id) == "example, not installed");

    const ExtensionEntry normalE = *normal, mineE = *mine, tlE = *tl, latchE = *latch, slidesE = *slides, nextE = *next;

    // install an example: the file appears, the entry becomes a user file; installing twice fails
    ExtensionResult r = installExtension(mc, bc, slidesE);
    assert(r.ok && fs::pathExists(modesDir + "/slides.conf"));
    r = installExtension(mc, bc, slidesE);
    assert(!r.ok);
    r = installExtension(mc, bc, nextE);
    assert(r.ok && fs::pathExists(buttonsDir + "/next.conf"));
    v = buildExtensions(mc, bc, ex);
    assert(find(v, K::JoystickMode, "slides")->origin == O::UserFile && find(v, K::ButtonMode, "next")->origin == O::UserFile);
    assert(!installExtension(mc, bc, mineE).ok);  // not an example

    // remove: user files only; a replacing file brings the built-in mode back
    assert(!removeExtension(mc, bc, normalE).ok);
    assert(removeExtension(mc, bc, mineE).ok && !fs::pathExists(modesDir + "/mine.conf"));
    assert(removeExtension(mc, bc, tlE).ok);
    v = buildExtensions(mc, bc, ex);
    assert(find(v, K::JoystickMode, "timeline")->origin == O::BuiltIn);
    assert(find(v, K::JoystickMode, "mine")->origin == O::ExampleAvailable);  // the example is offered again
    assert(removeExtension(mc, bc, latchE).ok && !fs::pathExists(buttonsDir + "/latch.conf"));

    tu::removeAll(root);
    std::puts("test_ui_extensions OK");
    return 0;
}
