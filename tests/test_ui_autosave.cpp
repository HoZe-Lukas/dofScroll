// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Autosave rule (src/ui/model/autosave.hpp) with a fake clock: immediate and debounced requests,
// burst coalescing, the maximum wait, flush, failure handling and the status text.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "testutil.hpp"
#include "ui/model/autosave.hpp"
#include "ui/model/settings_model.hpp"

using namespace spn::ui;

namespace {
struct Rig {
    bool dirty = false;
    int saves = 0;
    Autosave::Outcome next{true, true, ""};
    Autosave a{[this] { return dirty; },
               [this] {
                   ++saves;
                   if (next.saved) dirty = false;
                   return next;
               }};
};
}  // namespace

int main() {
    const IdentityStrings id;
    // ---- nothing pending: nothing happens ----
    {
        Rig r;
        assert(!r.a.pending() && r.a.msUntilDue(0) == -1);
        assert(!r.a.tick(10000) && !r.a.flush(10000) && r.saves == 0);
        assert(r.a.statusText(0, id).empty() && r.a.state() == Autosave::State::None);
    }
    // ---- immediate: due at once, written by the next tick, one write for a burst ----
    {
        Rig r;
        r.dirty = true;
        r.a.request(Autosave::Urgency::Immediate, 100);
        r.a.request(Autosave::Urgency::Immediate, 100);
        r.a.request(Autosave::Urgency::Immediate, 101);
        assert(r.a.pending() && r.a.msUntilDue(101) == 0);
        assert(r.saves == 0);  // not inside the request
        assert(r.a.tick(101) && r.saves == 1 && !r.a.pending());
        assert(r.a.statusText(101, id) == "Saved");
        assert(r.a.statusText(101 + 2999, id) == "Saved");
        assert(r.a.statusText(101 + 3000, id).empty());  // expires
        assert(!r.a.tick(500) && r.saves == 1);
    }
    // ---- debounced: waits for quiet, every keystroke pushes the deadline ----
    {
        Rig r;
        r.dirty = true;
        r.a.request(Autosave::Urgency::Debounced, 0);
        assert(r.a.msUntilDue(0) == 600);
        assert(!r.a.tick(599) && r.saves == 0);
        r.a.request(Autosave::Urgency::Debounced, 500);  // another key
        assert(r.a.msUntilDue(500) == 600);
        assert(!r.a.tick(1000) && r.saves == 0);
        assert(r.a.tick(1100) && r.saves == 1);
    }
    // ---- continuous typing cannot postpone the write forever ----
    {
        Rig r;
        r.dirty = true;
        for (int64_t t = 0; t < 2900; t += 100) r.a.request(Autosave::Urgency::Debounced, t);
        assert(!r.a.tick(2999));
        r.a.request(Autosave::Urgency::Debounced, 2900);  // deadline would be 3500, capped at first + 3000
        assert(r.a.msUntilDue(2900) == 100);
        assert(r.a.tick(3000) && r.saves == 1);
    }
    // ---- an immediate change pulls a pending debounced one forward ----
    {
        Rig r;
        r.dirty = true;
        r.a.request(Autosave::Urgency::Debounced, 0);
        r.a.request(Autosave::Urgency::Immediate, 50);
        assert(r.a.msUntilDue(50) == 0 && r.a.tick(50) && r.saves == 1);
        // ... and a debounced one after it does not push it back
        r.dirty = true;
        r.a.request(Autosave::Urgency::Immediate, 100);
        r.a.request(Autosave::Urgency::Debounced, 110);
        assert(r.a.msUntilDue(110) == 0);
    }
    // ---- flush: synchronous, also without a due deadline ----
    {
        Rig r;
        r.dirty = true;
        r.a.request(Autosave::Urgency::Debounced, 0);
        assert(r.a.flush(10) && r.saves == 1 && !r.a.pending());
        assert(!r.a.flush(11) && r.saves == 1);  // nothing left
        // edits that never reached request() (dirty model) are written by flush as well
        r.dirty = true;
        assert(r.a.flush(20) && r.saves == 2);
    }
    // ---- already saved by someone else: no second write ----
    {
        Rig r;
        r.dirty = true;
        r.a.request(Autosave::Urgency::Immediate, 0);
        r.dirty = false;
        assert(!r.a.tick(0) && r.saves == 0 && !r.a.pending());
    }
    // ---- failure: stays in the status line, no retry loop, next edit tries again ----
    {
        Rig r;
        r.dirty = true;
        r.next = {false, true, "cannot write /x"};
        r.a.request(Autosave::Urgency::Immediate, 0);
        assert(r.a.tick(0) && r.saves == 1 && r.a.state() == Autosave::State::Failed);
        assert(r.a.statusText(0, id) == "Could not save: cannot write /x");
        assert(r.a.statusText(60000, id) == "Could not save: cannot write /x");  // stays
        assert(!r.a.tick(1000) && r.saves == 1 && !r.a.pending());
        assert(r.a.flush(2000) && r.saves == 2);  // still dirty: flush tries again
        r.next = {true, true, ""};
        r.a.request(Autosave::Urgency::Debounced, 3000);
        assert(r.a.tick(3600) && r.a.state() == Autosave::State::Saved && r.a.lastError().empty());
        assert(r.a.statusText(3600, id) == "Saved");
    }
    // ---- saved, daemon not told ----
    {
        Rig r;
        r.dirty = true;
        r.next = {true, false, "systemctl failed"};
        r.a.request(Autosave::Urgency::Immediate, 0);
        assert(r.a.tick(0) && r.a.state() == Autosave::State::SavedNotNotified);
        assert(r.a.statusText(0, id) == "Saved, but the daemon was not notified: systemctl failed");
    }
    // ---- with the real SettingsModel: an invalid value never dirties the model, so nothing is written ----
    {
        const std::string dir = tu::makeTempDir("dofscroll-autosave");
        const std::string path = dir + "/dofscroll.conf";
        SettingsModel m;
        int reloads = 0;
        m.setReloadCallback([&reloads](std::string *) {
            ++reloads;
            return true;
        });
        std::string err;
        assert(m.load(path, nullptr, &err));
        Autosave a([&m] { return m.dirty(); }, [&m, &id] {
            const SettingsModel::SaveResult s = m.save(id);
            return Autosave::Outcome{s.saved, s.reloaded, s.error};
        });
        assert(!m.setValue(0, "SCROLL_V_SENS", "banana", &err) && !m.dirty());
        a.request(Autosave::Urgency::Debounced, 0);
        assert(!a.flush(1) && !tu::exists(path) && reloads == 0);
        // a valid edit is written once with one reload, however many requests came in
        assert(m.setValue(0, "SCROLL_V_SENS", "9", &err));
        a.request(Autosave::Urgency::Immediate, 10);
        assert(m.setValue(0, "SCROLL_V_SENS", "10", &err));
        a.request(Autosave::Urgency::Immediate, 10);
        assert(a.tick(10) && reloads == 1 && tu::exists(path));
        SettingsModel back;
        assert(back.load(path, nullptr, &err) && back.effective(0).scroll_v.sensitivity == 10);
        tu::removeAll(dir);
    }
    puts("test_ui_autosave: ok");
    return 0;
}
