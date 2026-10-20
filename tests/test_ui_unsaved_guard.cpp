// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unsaved-changes rule: ask only when there are edits; Save goes on only if it worked.
#undef NDEBUG
#include <cassert>
#include <string>

#include "ui/model/unsaved_guard.hpp"

using namespace spn::ui;

struct Counter {
    int asked = 0, saved = 0;
    std::string lastWhat;
    UnsavedChoice answer = UnsavedChoice::Cancel;
    bool saveOk = true;
    UnsavedAsk ask() {
        return [this](const std::string &w) {
            ++asked;
            lastWhat = w;
            return answer;
        };
    }
    std::function<bool()> save() {
        return [this] {
            ++saved;
            return saveOk;
        };
    }
};

int main() {
    // not dirty: no question, may go on
    {
        Counter c;
        assert(mayLeaveForm(false, "x", c.ask(), c.save()));
        assert(c.asked == 0 && c.saved == 0);
    }
    // Cancel: stay, nothing saved
    {
        Counter c;
        c.answer = UnsavedChoice::Cancel;
        assert(!mayLeaveForm(true, "Mine", c.ask(), c.save()));
        assert(c.asked == 1 && c.saved == 0 && c.lastWhat == "Mine");
    }
    // Discard: go on without saving
    {
        Counter c;
        c.answer = UnsavedChoice::Discard;
        assert(mayLeaveForm(true, "x", c.ask(), c.save()));
        assert(c.asked == 1 && c.saved == 0);
    }
    // Save: go on if it worked, stay if it failed
    {
        Counter c;
        c.answer = UnsavedChoice::Save;
        assert(mayLeaveForm(true, "x", c.ask(), c.save()));
        assert(c.saved == 1);
        c.saveOk = false;
        assert(!mayLeaveForm(true, "x", c.ask(), c.save()));
        assert(c.saved == 2 && c.asked == 2);
    }
    // no way to ask or to save: never discards silently
    {
        Counter c;
        assert(!mayLeaveForm(true, "x", nullptr, c.save()));
        c.answer = UnsavedChoice::Save;
        assert(!mayLeaveForm(true, "x", c.ask(), nullptr));
    }
    return 0;
}
