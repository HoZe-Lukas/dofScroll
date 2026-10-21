// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/ui/model/extensions_model.hpp: listing the joystick mode and button mode files, switching
// a file off and on by renaming it, what the daemon's loaders do with a switched-off file, removing.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "button_modes.hpp"
#include "modes.hpp"
#include "testutil.hpp"
#include "ui/model/extensions_model.hpp"

using namespace spn;
using namespace spn::ui;

static void put(const std::string &path, const std::string &text) {
    std::string err;
    if (!fs::writeFileReplace(path, text, &err)) {
        std::fprintf(stderr, "writeFileReplace(%s) failed: %s\n", path.c_str(), err.c_str());
        std::abort();
    }
}

static const ExtensionEntry *find(const std::vector<ExtensionEntry> &v, ExtensionKind k, const std::string &id) {
    for (const ExtensionEntry &e : v)
        if (e.kind == k && e.id == id) return &e;
    return nullptr;
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-ext");
    const std::string modes = root + "/modes", buttons = root + "/buttons";
    std::string err;
    assert(fs::mkdirsPrivate(modes, &err) && fs::mkdirsPrivate(buttons, &err));

    put(modes + "/mine.conf", "# my mode\nNAME = Mine\nDESCRIPTION = For testing\nBUTTON_1 = key:space\n");
    put(modes + "/plain.conf", "BUTTON_1 = key:space\n");
    put(modes + "/old.conf.disabled", "name = Old one\n");
    put(modes + "/notes.txt", "not an extension\n");
    put(modes + "/Bad Id.conf", "NAME = x\n");  // not a valid id
    put(buttons + "/latch.conf", "NAME = Latch\nACTION = toggle:nav\n");

    // listing: both kinds, on and off, labels from the files, other names left out
    auto used = [](ExtensionKind k, const std::string &id) {
        return std::vector<std::string>{k == ExtensionKind::JoystickMode && id == "mine" ? "[app:blender]" : ""};
    };
    std::vector<ExtensionEntry> list = listExtensions(modes, buttons, used);
    assert(list.size() == 4);
    const ExtensionEntry *mine = find(list, ExtensionKind::JoystickMode, "mine");
    assert(mine && mine->enabled && mine->label == "Mine" && mine->description == "For testing" && mine->path == modes + "/mine.conf");
    assert(mine->usedBy.size() == 1 && mine->usedBy[0] == "[app:blender]");
    const ExtensionEntry *plain = find(list, ExtensionKind::JoystickMode, "plain");
    assert(plain && plain->label == "plain" && plain->description.empty());
    const ExtensionEntry *old = find(list, ExtensionKind::JoystickMode, "old");
    assert(old && !old->enabled && old->label == "Old one");  // keys are case-insensitive
    const ExtensionEntry *latch = find(list, ExtensionKind::ButtonMode, "latch");
    assert(latch && latch->enabled && latch->label == "Latch");
    assert(!find(list, ExtensionKind::JoystickMode, "notes") && !find(list, ExtensionKind::JoystickMode, "Bad Id"));
    assert(listExtensions("", "").empty() && listExtensions(root + "/missing", root + "/missing").empty());

    // the loaders read "<id>.conf" and ignore "<id>.conf.disabled"
    ModeSet ms = loadModes(modes, nullptr);
    assert(ms.find("mine") && !ms.find("old"));

    // switching off: renamed, no longer loaded, listed as off
    ExtensionResult r = setExtensionEnabled(*mine, false);
    assert(r.ok && r.path == modes + "/mine.conf.disabled");
    assert(!fs::pathExists(modes + "/mine.conf") && fs::pathExists(modes + "/mine.conf.disabled"));
    ms = loadModes(modes, nullptr);
    assert(!ms.find("mine"));
    list = listExtensions(modes, buttons);
    mine = find(list, ExtensionKind::JoystickMode, "mine");
    assert(mine && !mine->enabled && mine->label == "Mine");

    // the same state again changes nothing
    assert(setExtensionEnabled(*mine, false).ok && fs::pathExists(modes + "/mine.conf.disabled"));

    // switching on: back to the original name, loaded again, content unchanged
    r = setExtensionEnabled(*mine, true);
    assert(r.ok && r.path == modes + "/mine.conf");
    ms = loadModes(modes, nullptr);
    assert(ms.find("mine") && ms.find("mine")->label == "Mine");

    // never overwrites: a file for the same id is already there
    put(modes + "/mine.conf.disabled", "NAME = Another mine\n");
    list = listExtensions(modes, buttons);
    const ExtensionEntry *offAgain = nullptr;
    for (const ExtensionEntry &e : list)
        if (e.id == "mine" && !e.enabled) offAgain = &e;
    assert(offAgain);
    r = setExtensionEnabled(*offAgain, true);
    assert(!r.ok && !r.error.empty());
    std::string text;
    assert(fs::readFileBounded(modes + "/mine.conf", 4096, fs::ReadOptions(), &text).ok() && text.find("Mine") != std::string::npos);
    list = listExtensions(modes, buttons);
    const ExtensionEntry *onNow = find(list, ExtensionKind::JoystickMode, "mine");
    assert(onNow);
    r = setExtensionEnabled(*onNow, false);
    assert(!r.ok && !r.error.empty());  // the switched-off name is taken too

    // a file with the id of a built-in mode: switching it off lets the built-in mode apply again
    put(modes + "/timeline.conf", "NAME = My timeline\nV_ACTION = none\n");
    ms = loadModes(modes, nullptr);
    assert(ms.find("timeline") && ms.find("timeline")->label == "My timeline");
    list = listExtensions(modes, buttons);
    const ExtensionEntry *tl = find(list, ExtensionKind::JoystickMode, "timeline");
    assert(tl && setExtensionEnabled(*tl, false).ok);
    ms = loadModes(modes, nullptr);
    assert(ms.find("timeline") && ms.find("timeline")->label != "My timeline" && ms.find("timeline")->builtin);

    // button modes work the same way
    ButtonModeSet bs = loadButtonModes(buttons, nullptr);
    assert(bs.find("latch") && bs.find("latch")->label == "Latch" && !bs.find("latch")->builtin);
    list = listExtensions(modes, buttons);
    latch = find(list, ExtensionKind::ButtonMode, "latch");
    assert(latch && setExtensionEnabled(*latch, false).ok);
    bs = loadButtonModes(buttons, nullptr);
    assert(!bs.find("latch") || bs.find("latch")->builtin);
    assert(fs::pathExists(buttons + "/latch.conf.disabled"));

    // removing, switched on or off
    list = listExtensions(modes, buttons);
    const ExtensionEntry *gone = find(list, ExtensionKind::JoystickMode, "plain");
    assert(gone && removeExtension(*gone).ok && !fs::pathExists(modes + "/plain.conf"));
    gone = find(list, ExtensionKind::ButtonMode, "latch");
    assert(gone && !gone->enabled && removeExtension(*gone).ok && !fs::pathExists(buttons + "/latch.conf.disabled"));
    assert(!removeExtension(*gone).ok);  // already gone

    // copyOfShipped: a file identical to a built-in text, or to any legacy text, is flagged (the loaders ignore it);
    // an edited copy, a switched-off copy and a file of the user are not
    {
        const std::string m2 = root + "/modes2", b2 = root + "/buttons2";
        assert(fs::mkdirsPrivate(m2, &err) && fs::mkdirsPrivate(b2, &err));
        const BuiltinFile *bi = builtinModeFiles();
        assert(bi && bi->id);
        put(m2 + "/" + bi->id + ".conf", bi->text);
        std::string legacyId;
        for (const BuiltinFile *l = legacyModeFiles(); l && l->id; ++l) {
            // every legacy text of the first id is recognised, the first and the later ones alike
            if (legacyId.empty() || legacyId == l->id) {
                legacyId = l->id;
                put(m2 + "/" + legacyId + ".conf", l->text);
                std::vector<ExtensionEntry> one = listExtensions(m2, b2);
                const ExtensionEntry *e = find(one, ExtensionKind::JoystickMode, legacyId);
                assert(e && e->copyOfShipped);
                const ModeDef *loaded = loadModes(m2, nullptr).find(legacyId);
                assert(!loaded || loaded->builtin);  // ignored: never the user's own mode
            }
        }
        put(m2 + "/edited.conf", std::string(bi->text) + "# mine\n");
        put(m2 + "/mine.conf", "NAME = Mine\n");
        const BuiltinFile *bb = builtinButtonModeFiles();
        assert(bb && bb->id);
        put(b2 + "/" + bb->id + ".conf", bb->text);
        put(b2 + "/" + bb->id + "-x.conf", bb->text);  // another id: a user file
        std::vector<ExtensionEntry> l2 = listExtensions(m2, b2);
        const ExtensionEntry *c1 = find(l2, ExtensionKind::JoystickMode, bi->id);
        assert(c1 && c1->copyOfShipped && c1->enabled);
        assert(find(l2, ExtensionKind::JoystickMode, legacyId)->copyOfShipped);
        assert(!find(l2, ExtensionKind::JoystickMode, "edited")->copyOfShipped);
        assert(!find(l2, ExtensionKind::JoystickMode, "mine")->copyOfShipped);
        assert(find(l2, ExtensionKind::ButtonMode, bb->id)->copyOfShipped);
        assert(!find(l2, ExtensionKind::ButtonMode, std::string(bb->id) + "-x")->copyOfShipped);
        // switched off: it is then nothing the loader sees, and not flagged
        assert(setExtensionEnabled(*c1, false).ok);
        l2 = listExtensions(m2, b2);
        const ExtensionEntry *off2 = find(l2, ExtensionKind::JoystickMode, bi->id);
        assert(off2 && !off2->enabled && !off2->copyOfShipped);
    }

    std::puts("test_ui_extensions: ok");
    return 0;
}
