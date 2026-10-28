// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The key tree of the key picker: every key once, no empty branch, lookup both ways, labels.
#undef NDEBUG
#include <cassert>
#include <map>
#include <set>
#include <string>

#include "modes.hpp"
#include "ui/model/key_tree_model.hpp"

using namespace spn;
using namespace spn::ui;

static void checkBranches(const std::vector<KeyTreeNode> &level, int depth) {
    assert(depth <= 3);
    std::set<std::string> ids;
    for (const KeyTreeNode &n : level) {
        assert(!n.title.empty());
        if (n.isLeaf()) {
            assert(!n.value.empty());
        } else {
            assert(!n.id.empty() && ids.insert(n.id).second);
            assert(n.value.empty());
            checkBranches(n.children, depth + 1);
        }
    }
}

int main() {
    for (bool mouse : {false, true}) {
        const KeyTree t = buildKeyTree(KeyTreeOptions{mouse});
        checkBranches(t.roots, 1);
        // every key of keys.hpp exactly once
        std::map<std::string, int> seen;
        for (const KeyTreeNode *l : t.leaves()) ++seen[l->value];
        for (const std::string &k : allKeyNames()) assert(seen[k] == 1);
        assert(seen.size() == allKeyNames().size() + (mouse ? 3 : 0));
        // lookup value -> path -> node -> title
        for (const KeyTreeNode *l : t.leaves()) {
            const KeyTreePath p = t.pathOf(l->value);
            assert(!p.empty() && t.at(p) == l);
            assert(t.titlesOf(p).back() == l->title);
        }
        assert(t.pathOf("").empty() && t.pathOf("nonsense").empty());
        assert(t.at({99}) == nullptr && t.at({0, 99}) == nullptr);
    }
    const KeyTree t = buildKeyTree();
    using V = std::vector<std::string>;
    assert(t.roots.size() == 2 && t.roots[0].title == "Keyboard" && t.roots[1].title == "Media");
    assert(t.titlesOf(t.pathOf("pageup")) == (V{"Keyboard", "Navigation", "Page up"}));
    assert(t.titlesOf(t.pathOf("tab")) == (V{"Keyboard", "Editing", "Tab"}));
    assert(t.titlesOf(t.pathOf("a")) == (V{"Keyboard", "Letters", "A"}));
    assert(t.titlesOf(t.pathOf("f12")) == (V{"Keyboard", "Function keys", "F12"}));
    assert(t.titlesOf(t.pathOf("kp_5")) == (V{"Keyboard", "Keypad", "5"}));
    assert(t.titlesOf(t.pathOf("minus")).at(1) == "Punctuation");
    assert(t.titlesOf(t.pathOf("ctrl")).at(1) == "Modifiers");
    assert(t.titlesOf(t.pathOf("volume_mute")) == (V{"Media", "Volume", "Mute"}));
    assert(t.titlesOf(t.pathOf("media_next")).at(1) == "Playback");
    const KeyTree m = buildKeyTree(KeyTreeOptions{true});
    assert(m.roots[0].title == "Mouse");
    assert(m.titlesOf(m.pathOf("rightclick")) == (V{"Mouse", "Buttons", "Right click"}));
    // the click names are the ones of the button grammar
    for (const char *c : {"click", "rightclick", "middleclick"}) {
        ButtonActionType ty;
        Key k;
        std::string err;
        assert(parseButtonAction(c, &ty, &k, &err));
    }
    return 0;
}
