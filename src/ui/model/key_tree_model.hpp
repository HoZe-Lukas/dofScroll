// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tree for the key picker (toolkit-free): category -> optional subcategory -> leaf, shown by the view as columns
// (like the Finder column view). The tree is data: a leaf holds the stored value (a key name of keys.hpp, or an
// action text such as "click") and an English label; later stages can add branches (Mouse > Scroll, Functions)
// by adding nodes here, the view needs no change. Titles and labels are English source strings; the view
// translates them.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "key_list.hpp"

namespace spn::ui {

struct KeyTreeNode {
    std::string id;     // stable id of a branch; empty for a leaf
    std::string title;  // English label of the branch or leaf
    std::string value;  // leaf only: the stored text
    std::vector<KeyTreeNode> children;
    bool isLeaf() const { return children.empty(); }
};

using KeyTreePath = std::vector<size_t>;  // child indices from the root, one per level

struct KeyTree {
    std::vector<KeyTreeNode> roots;

    // The node at a path, nullptr if the path does not exist.
    const KeyTreeNode *at(const KeyTreePath &path) const {
        const std::vector<KeyTreeNode> *level = &roots;
        const KeyTreeNode *n = nullptr;
        for (size_t i : path) {
            if (i >= level->size()) return nullptr;
            n = &(*level)[i];
            level = &n->children;
        }
        return n;
    }
    // Path of the leaf with this stored value, empty if there is none.
    KeyTreePath pathOf(const std::string &value) const {
        KeyTreePath p;
        return value.empty() || !find(roots, value, &p) ? KeyTreePath() : p;
    }
    // The titles along a path ("Keyboard", "Navigation", "Page up"; English, the view translates the parts).
    std::vector<std::string> titlesOf(const KeyTreePath &path) const {
        std::vector<std::string> out;
        const std::vector<KeyTreeNode> *level = &roots;
        for (size_t i : path) {
            if (i >= level->size()) break;
            out.push_back((*level)[i].title);
            level = &(*level)[i].children;
        }
        return out;
    }
    std::vector<const KeyTreeNode *> leaves() const {
        std::vector<const KeyTreeNode *> out;
        collect(roots, &out);
        return out;
    }

private:
    static bool find(const std::vector<KeyTreeNode> &level, const std::string &value, KeyTreePath *p) {
        for (size_t i = 0; i < level.size(); ++i) {
            p->push_back(i);
            if (level[i].isLeaf() ? level[i].value == value : find(level[i].children, value, p)) return true;
            p->pop_back();
        }
        return false;
    }
    static void collect(const std::vector<KeyTreeNode> &level, std::vector<const KeyTreeNode *> *out) {
        for (const KeyTreeNode &n : level) {
            if (n.isLeaf()) out->push_back(&n);
            else collect(n.children, out);
        }
    }
};

// English label of a key name: "pageup" -> "Page up", "a" -> "A", "kp_enter" -> "Enter" (under Keypad the branch says it).
inline std::string keyTreeLabel(const std::string &name) {
    static const struct {
        const char *name, *label;
    } kNamed[] = {
        {"ctrl", "Ctrl"}, {"shift", "Shift"}, {"alt", "Alt"}, {"super", "Super"},
        {"up", "Up arrow"}, {"down", "Down arrow"}, {"left", "Left arrow"}, {"right", "Right arrow"},
        {"home", "Home"}, {"end", "End"}, {"pageup", "Page up"}, {"pagedown", "Page down"},
        {"enter", "Enter"}, {"tab", "Tab"}, {"space", "Space"}, {"delete", "Delete"}, {"backspace", "Backspace"},
        {"esc", "Escape"}, {"insert", "Insert"},
        {"kp_plus", "Plus"}, {"kp_minus", "Minus"}, {"kp_enter", "Enter"}, {"kp_multiply", "Multiply"},
        {"kp_divide", "Divide"}, {"kp_decimal", "Decimal point"},
        {"minus", "Minus"}, {"plus", "Plus"}, {"comma", "Comma"}, {"period", "Period"}, {"slash", "Slash"},
        {"backslash", "Backslash"}, {"semicolon", "Semicolon"}, {"apostrophe", "Apostrophe"},
        {"bracket_left", "Left bracket"}, {"bracket_right", "Right bracket"}, {"grave", "Grave accent"},
        {"equal", "Equal sign"},
        {"caps_lock", "Caps Lock"}, {"print", "Print Screen"}, {"scroll_lock", "Scroll Lock"}, {"pause", "Pause"},
        {"menu", "Menu"}, {"num_lock", "Num Lock"},
        {"play_pause", "Play / Pause"}, {"media_next", "Next track"}, {"media_prev", "Previous track"},
        {"media_stop", "Stop"}, {"volume_up", "Volume up"}, {"volume_down", "Volume down"}, {"volume_mute", "Mute"},
    };
    for (const auto &e : kNamed)
        if (name == e.name) return e.label;
    if (name.compare(0, 3, "kp_") == 0 && name.size() == 4) return name.substr(3);  // keypad digits
    if (name.size() == 1 && name[0] >= 'a' && name[0] <= 'z') return std::string(1, char(name[0] - 'a' + 'A'));
    if (name.size() >= 2 && name[0] == 'f') return "F" + name.substr(1);
    return name;  // digits, and any future key until it has a label
}

// The category and subcategory id of a key name.
inline std::pair<std::string, std::string> keyTreeCategoryOf(const std::string &name) {
    const std::string g = keyGroupIdOf(name);
    if (g == "media") return {"media", name.compare(0, 7, "volume_") == 0 ? "volume" : "playback"};
    if (g == "navigation" && name == "tab") return {"keyboard", "editing"};
    if (g == "symbols") return {"keyboard", "punctuation"};
    return {"keyboard", g};
}

struct KeyTreeOptions {
    bool mouse = false;  // add Mouse > Buttons with the click actions of the button grammar (click, rightclick, middleclick)
};

// The tree of keys.hpp (every name exactly once). Order of categories and subcategories is fixed, keys keep keys.hpp order.
inline KeyTree buildKeyTree(const KeyTreeOptions &opt = {}) {
    auto branch = [](const char *id, const char *title) {
        KeyTreeNode n;
        n.id = id;
        n.title = title;
        return n;
    };
    auto leaf = [](const std::string &title, const std::string &value) {
        KeyTreeNode n;
        n.title = title;
        n.value = value;
        return n;
    };
    KeyTreeNode keyboard = branch("keyboard", "Keyboard"), media = branch("media", "Media");
    struct SubCategory {
        const char *id, *title;
    };
    static const SubCategory kKeyboardSubs[] = {
        {"letters", "Letters"},       {"digits", "Digits"},         {"function", "Function keys"},
        {"navigation", "Navigation"}, {"editing", "Editing"},       {"keypad", "Keypad"},
        {"punctuation", "Punctuation"}, {"modifiers", "Modifiers"}, {"system", "System keys"}};
    for (const SubCategory &s : kKeyboardSubs) keyboard.children.push_back(branch(s.id, s.title));
    media.children.push_back(branch("volume", "Volume"));
    media.children.push_back(branch("playback", "Playback"));
    for (const std::string &name : allKeyNames()) {
        const auto cat = keyTreeCategoryOf(name);
        KeyTreeNode &top = cat.first == "media" ? media : keyboard;
        for (KeyTreeNode &sub : top.children)
            if (sub.id == cat.second) sub.children.push_back(leaf(keyTreeLabel(name), name));
    }
    KeyTree t;
    t.roots.push_back(std::move(keyboard));
    t.roots.push_back(std::move(media));
    if (opt.mouse) {
        KeyTreeNode mouse = branch("mouse", "Mouse"), buttons = branch("buttons", "Buttons");
        buttons.children.push_back(leaf("Click", "click"));
        buttons.children.push_back(leaf("Right click", "rightclick"));
        buttons.children.push_back(leaf("Middle click", "middleclick"));
        mouse.children.push_back(std::move(buttons));
        t.roots.insert(t.roots.begin(), std::move(mouse));
    }
    return t;
}

}  // namespace spn::ui
