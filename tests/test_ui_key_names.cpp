// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The names of keys in the German interface: every key of the picker has a translation in the context "key" of po/de.po or is
// listed here as the same in German; the display names are translated, the names that files hold are not (golden texts).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>

#include "button_modes.hpp"
#include "modes.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/key_tree_model.hpp"
#include "ui/model/macro_model.hpp"
#include "ui/model/motion_summary_model.hpp"
#include "ui/model/new_function_model.hpp"

using namespace spn;
using namespace spn::ui;

namespace {

// Key labels that read the same on a German keyboard (the English label is the German one).
const std::set<std::string> kSameInGerman = {"Alt", "Super", "Tab", "Plus", "Minus", "Pause"};

// The "key" context of po/de.po: msgid -> msgstr (single-line entries, as the catalog writes the key names).
std::map<std::string, std::string> keyCatalog() {
    const std::filesystem::path po = std::filesystem::path(__FILE__).parent_path() / ".." / "po" / "de.po";
    std::ifstream in(po);
    assert(in);
    std::map<std::string, std::string> out;
    std::string line, id;
    bool inKey = false;
    auto quoted = [](const std::string &l, size_t from) {
        const size_t a = l.find('"', from), b = l.rfind('"');
        return a == std::string::npos || b <= a ? std::string() : l.substr(a + 1, b - a - 1);
    };
    while (std::getline(in, line)) {
        if (line.rfind("msgctxt ", 0) == 0) inKey = quoted(line, 8) == "key";
        else if (line.rfind("msgid ", 0) == 0) id = quoted(line, 6);
        else if (line.rfind("msgstr ", 0) == 0) {
            if (inKey) out[id] = quoted(line, 7);
            inKey = false;
        }
    }
    return out;
}

struct GermanKeys : StringProvider {
    std::map<std::string, std::string> keys;
    std::string tr(const char *s) const override { return s; }
    std::string keyLabel(const char *s) const override {
        const auto it = keys.find(s);
        return it == keys.end() || it->second.empty() ? std::string(s) : it->second;
    }
};

bool generatedLabel(const std::string &name, const std::string &label) {
    // the face of a letter, a digit, a function key and a keypad digit is the same everywhere
    return label.size() == 1 || (name.size() >= 2 && name[0] == 'f' && label[0] == 'F') || label == name;
}

}  // namespace

int main() {
    GermanKeys de;
    de.keys = keyCatalog();
    assert(!de.keys.empty());

    // ---- every key of the picker: translated, or the same in German on purpose ----
    std::set<std::string> labels;
    for (const std::string &name : allKeyNames()) {
        const std::string label = keyTreeLabel(name);
        if (generatedLabel(name, label)) continue;
        labels.insert(label);
    }
    assert(labels.size() > 40);
    for (const std::string &label : labels) {
        const auto it = de.keys.find(label);
        const bool translated = it != de.keys.end() && !it->second.empty();
        const bool same = kSameInGerman.count(label) != 0;
        if (translated == same) {
            // both (a translation that is also listed as the same) or neither is a mistake in the catalog or in this list
            fprintf(stderr, "key label \"%s\": %s\n", label.c_str(), translated ? "translated and listed as the same in German" : "no translation in the context \"key\" of po/de.po");
            assert(false);
        }
    }
    // no entry for a key label that does not exist (a renamed label leaves a dead entry behind)
    for (const auto &e : de.keys) assert(labels.count(e.first) || e.first == "Ctrl" || e.first == "Shift");

    // ---- the names a German keyboard shows ----
    assert(keyDisplayName("left", de, "Left") == "Pfeil links" && keyDisplayName("space", de, "Space") == "Leertaste");
    assert(keyDisplayName("delete", de, "Delete") == "Entf" && keyDisplayName("enter", de, "Enter") == "Eingabe"); // check-english: allow
    assert(keyDisplayName("pageup", de, "Page up") == "Bild auf" && keyDisplayName("esc", de, "Esc") == "Esc");
    assert(keyDisplayName("backspace", de, "Backspace") == "Rücktaste" && keyDisplayName("ctrl", de, "Ctrl") == "Strg"); // check-english: allow
    assert(keyDisplayName("a", de, "A") == "A" && keyDisplayName("f5", de, "F5") == "F5");
    // the English interface keeps the text each place always showed
    IdentityStrings en;
    assert(keyDisplayName("left", en, "Left") == "Left" && keyDisplayName("delete", en, "Delete") == "Delete");
    assert(motion_detail::keyWords("ctrl+pageup", en) == "Ctrl+Page up" && motion_detail::keyWords("left", en) == "Left");
    assert(motion_detail::keyWords("ctrl+pageup", de) == "Strg+Bild auf" && motion_detail::keyWords("left", de) == "Pfeil links");
    assert(chordDisplay("ctrl+shift+delete", de) == "Strg+Umschalt+Entf" && chordDisplay("ctrl+shift+delete", en) == "ctrl+shift+delete");

    // ---- the tiles and the lists show the translated names ----
    {
        ChannelAction a;
        a.type = ChannelActionType::Keys;
        std::string err;
        assert(parseChannelAction("keys:left,right", &a, &err));
        const MotionRow row = motion_detail::rowFor("h_push", "Sideways", "push", a, false, false, 0, de);
        assert(row.action == "Keys: Pfeil links / Pfeil rechts" && row.detail == "Pfeil links / Pfeil rechts");
        assert(buttonActionWords("key:space", de) == "Leertaste" && buttonActionWords("key:space", en) == "Space");
        assert(buttonActionWords("key:ctrl+pageup", de) == "Strg+Bild auf");
    }

    // ---- golden: what is written is the config name, whatever the interface language ----
    {
        const FunctionTarget tilt = FunctionTarget::motion("h_tilt"), btn = FunctionTarget::buttonNumber(3);
        FunctionDraft d = newDraft(tilt, FunctionKind::Key);
        d.neg = "ctrl+pageup";
        d.pos = "ctrl+delete";
        std::string text, err;
        assert(composeFunction(tilt, d, &text, &err) && text == "step:ctrl+pageup,ctrl+delete");
        assert(setDraftAnalog(&d, true) && composeFunction(tilt, d, &text, &err) && text == "keys:ctrl+pageup,ctrl+delete");
        d = newDraft(btn, FunctionKind::Key);
        d.neg = "left";
        assert(composeFunction(btn, d, &text, &err));
        assert(text.find("left") != std::string::npos && text.find("Pfeil") == std::string::npos);
        assert(keyActionText(Key::Delete, ChordMods{}) == "key:delete" && keyActionText(Key::Left, ChordMods{}) == "key:left");
        // the macro text of a step stays the config text; only the list shows the translated names
        MacroStep step;
        assert(makeMacroKeyStep(true, "ctrl+delete", &step, &err) && macroStepText(step) == "+ctrl+delete");
        // the chord text that the display takes apart is not changed by it
        const std::string chord = "ctrl+shift+delete";
        const std::string shown = chordDisplay(chord, de);
        assert(chord == "ctrl+shift+delete" && shown != chord);
    }
    return 0;
}
