// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// CycleEditorModel: the state behind the "Cycle through" section of the Button modes page,
// toolkit-free. A button mode with ACTION = mode:next / mode:prev either walks through all joystick
// modes alphabetically or through a CYCLE list (button_modes.hpp, mode_cycle_model.hpp). The view
// shows rows() as a check list (the checked rows first, in cycle order, then the other loaded
// joystick modes alphabetically), calls setChecked / moveUp / moveDown, and writes cycle() with
// ButtonModeCatalog::save (an empty result = no CYCLE key = all modes).
//
//   CycleEditorModel m(modes, entry->cycle);        // only-selected when the file has a CYCLE list
//   m.setChecked("slides", true); m.moveUp("slides");
//   m.summary(strings);                              // "Order: 1 slides, 2 normal"
//   catalog.save(id, name, desc, "mode:next", m.cycle());
//
// An id of the file that matches no loaded joystick mode stays in the list ("missing: <id>"; the
// daemon only warns and skips it); unticking it removes it.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "mode_cycle_model.hpp"
#include "modes.hpp"
#include "strings.hpp"

namespace spn::ui {

class CycleEditorModel {
public:
    struct Row {
        std::string id;
        std::string label;     // display name of a loaded mode; the id for a missing one
        bool checked = false;
        int position = 0;      // 1-based place in the cycle; 0 when not checked
        bool missing = false;  // in the file, but no loaded joystick mode has this id
    };

    CycleEditorModel() = default;
    CycleEditorModel(const ModeSet &modes, const std::vector<std::string> &cycle) { reset(modes, cycle); }

    void reset(const ModeSet &modes, const std::vector<std::string> &cycle) {
        known_.clear();
        for (const ModeDef &m : modes.modes)
            if (m.id != "next" && m.id != "prev") known_.push_back({m.id, m.label.empty() ? m.id : m.label});
        std::sort(known_.begin(), known_.end(), [](const Known &a, const Known &b) { return a.id < b.id; });
        list_ = ModeCycleList(cycle);
        only_ = !list_.entries().empty();
    }

    bool onlySelected() const { return only_; }
    // "All joystick modes" forgets the ticks (an empty list); "only these" starts from what is ticked.
    void setOnlySelected(bool only) {
        only_ = only;
        if (!only) list_ = ModeCycleList();
    }
    size_t selectedCount() const { return list_.size(); }
    bool isChecked(const std::string &id) const { return list_.contains(id); }

    // The value for CYCLE: empty (no key, all modes) unless "only these" holds at least one entry.
    std::vector<std::string> cycle() const { return only_ ? list_.entries() : std::vector<std::string>(); }

    std::vector<Row> rows() const {
        std::vector<Row> out;
        int n = 0;
        for (const std::string &id : list_.entries()) {
            Row r;
            r.id = id;
            r.label = labelOf(id);
            r.checked = true;
            r.position = ++n;
            r.missing = !isKnown(id);
            out.push_back(r);
        }
        for (const Known &k : known_)
            if (!list_.contains(k.id)) {
                Row r;
                r.id = k.id;
                r.label = k.label;
                out.push_back(r);
            }
        return out;
    }

    // Ticks or unticks a row. Ticking is refused (false) when the list is full (kMaxModeCycle).
    bool setChecked(const std::string &id, bool on) {
        if (on) return list_.add(id);
        const long i = indexOf(id);
        return i >= 0 && list_.remove(size_t(i));
    }
    bool moveUp(const std::string &id) { return move(id, -1); }
    bool moveDown(const std::string &id) { return move(id, +1); }
    bool canMoveUp(const std::string &id) const { return indexOf(id) > 0; }
    bool canMoveDown(const std::string &id) const {
        const long i = indexOf(id);
        return i >= 0 && size_t(i) + 1 < list_.size();
    }

    // Ids of the file that match no loaded joystick mode.
    std::vector<std::string> missing() const {
        std::vector<std::string> out;
        for (const std::string &id : list_.entries())
            if (!isKnown(id)) out.push_back(id);
        return out;
    }

    // The result in words: "Order: 1 timeline, 2 normal", or what happens without a selection.
    std::string summary(const StringProvider &s) const {
        if (!only_) return s.tr("The button goes through all joystick modes, alphabetically.");
        if (list_.entries().empty()) return s.tr("Nothing ticked yet: the button goes through all joystick modes, alphabetically.");
        std::string t;
        int n = 0;
        for (const std::string &id : list_.entries()) {
            if (n) t += ", ";
            t += std::to_string(++n) + " " + id;
        }
        return format(s.tr("Order: %1"), {t});
    }
    // A hint next to the section ("" when there is nothing to say).
    std::string hint(const StringProvider &s) const {
        if (list_.size() >= kMaxModeCycle) return s.tr("The list is full (at most 32 joystick modes).");
        if (!missing().empty()) return s.tr("A joystick mode that is missing stays in the file and is skipped; untick it to remove it.");
        return std::string();
    }
    static std::string rowText(const Row &r, const StringProvider &s) {
        std::string t = r.missing ? format(s.tr("missing: %1"), {r.id}) : (r.label == r.id ? r.id : r.label + " (" + r.id + ")");
        if (r.position) t = std::to_string(r.position) + ". " + t;
        return t;
    }
    // Built-in button modes have no file; this says what they do and how to change it.
    static std::string builtinNote(const StringProvider &s) {
        return s.tr("The built-in button modes for the next and the previous joystick mode go through all joystick modes; the "
                    "built-in cycle-favorites goes through timeline, then normal. Create an editable copy to choose the "
                    "joystick modes yourself.");
    }
    // "Cycles: timeline, normal" for tooltips and lists; "" without a CYCLE list.
    static std::string cyclesText(const std::vector<std::string> &cycle, const StringProvider &s) {
        if (cycle.empty()) return std::string();
        return format(s.tr("Cycles: %1"), {ModeCycleList(cycle).toText()});
    }

private:
    struct Known {
        std::string id, label;
    };
    bool isKnown(const std::string &id) const {
        return std::any_of(known_.begin(), known_.end(), [&](const Known &k) { return k.id == id; });
    }
    std::string labelOf(const std::string &id) const {
        for (const Known &k : known_)
            if (k.id == id) return k.label;
        return id;
    }
    long indexOf(const std::string &id) const {
        const auto &e = list_.entries();
        const auto it = std::find(e.begin(), e.end(), id);
        return it == e.end() ? -1 : long(it - e.begin());
    }
    bool move(const std::string &id, int d) {
        const long i = indexOf(id);
        if (i < 0) return false;
        return d < 0 ? list_.moveUp(size_t(i)) : list_.moveDown(size_t(i));
    }

    std::vector<Known> known_;
    ModeCycleList list_;
    bool only_ = false;
};

}  // namespace spn::ui
