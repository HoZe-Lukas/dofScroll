// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ModeCycleList: the ordered list of joystick mode ids behind the CYCLE setting of a button mode
// (button_modes.hpp), toolkit-free. A view shows entries() and calls add/remove/moveUp/moveDown;
// candidates() lists what the "Add" control may offer (all loaded modes not yet in the list).
//
//   ModeCycleList l(entry->cycle);
//   l.add("slides"); l.moveUp(1); l.remove(0);
//   buttonModes.save(id, name, desc, "mode:next", l.entries());   // an empty list removes CYCLE
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "modes.hpp"

namespace spn::ui {

class ModeCycleList {
public:
    ModeCycleList() = default;
    explicit ModeCycleList(const std::vector<std::string> &ids) {
        for (const std::string &id : ids) add(id);
    }

    const std::vector<std::string> &entries() const { return ids_; }
    size_t size() const { return ids_.size(); }
    bool contains(const std::string &id) const { return std::find(ids_.begin(), ids_.end(), id) != ids_.end(); }

    // Appends a valid id that is not in the list yet and while there is room (kMaxModeCycle).
    bool add(const std::string &id) {
        if (!validModeId(id) || contains(id) || ids_.size() >= kMaxModeCycle) return false;
        ids_.push_back(id);
        return true;
    }
    bool remove(size_t i) {
        if (i >= ids_.size()) return false;
        ids_.erase(ids_.begin() + static_cast<long>(i));
        return true;
    }
    bool moveUp(size_t i) {
        if (i == 0 || i >= ids_.size()) return false;
        std::swap(ids_[i], ids_[i - 1]);
        return true;
    }
    bool moveDown(size_t i) {
        if (i + 1 >= ids_.size()) return false;
        std::swap(ids_[i], ids_[i + 1]);
        return true;
    }

    // All loaded modes that are not in the list yet, in the order of `modes`. Empty when the list is full.
    std::vector<std::string> candidates(const ModeSet &modes) const {
        std::vector<std::string> out;
        if (ids_.size() >= kMaxModeCycle) return out;
        for (const ModeDef &m : modes.modes)
            if (!contains(m.id) && m.id != "next" && m.id != "prev") out.push_back(m.id);
        return out;
    }
    // Entries that match no loaded mode (shown as "missing"; skipped by the daemon).
    std::vector<std::string> unknown(const ModeSet &modes) const {
        std::vector<std::string> out;
        for (const std::string &id : ids_)
            if (!modes.find(id)) out.push_back(id);
        return out;
    }
    std::string toText() const {
        std::string t;
        for (size_t i = 0; i < ids_.size(); i++) t += (i ? ", " : "") + ids_[i];
        return t;
    }

private:
    std::vector<std::string> ids_;
};

}  // namespace spn::ui
