// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include <cassert>
#include <cstdio>

#include "mode_list.hpp"

using namespace spn;
using namespace spn::modelist;

int main() {
    ProfileSet ps;
    assert(buildRows(ps).empty());

    size_t d = ps.findOrAdd(Section::DEVICE, "SpaceMouse Pro");
    size_t a1 = ps.findOrAdd(Section::APP, "firefox");
    size_t a2 = ps.findOrAdd(Section::APP, "resolve");
    assert(d != ProfileSet::npos && a1 != ProfileSet::npos && a2 != ProfileSet::npos);
    ps.sections[a2].kv.emplace_back("MODE", "timeline");

    auto rows = buildRows(ps);
    assert(rows.size() == 3);
    // Programs first (file order), then devices.
    assert(rows[0].kind == Section::APP && rows[0].pattern == "firefox" && rows[0].mode_id == "normal");
    assert(rows[1].kind == Section::APP && rows[1].pattern == "resolve" && rows[1].mode_id == "timeline");
    assert(rows[2].kind == Section::DEVICE && rows[2].pattern == "SpaceMouse Pro");

    // A global MODE is inherited by profiles that do not set one.
    ps.sections[0].kv.emplace_back("MODE", "off");
    rows = buildRows(ps);
    assert(rows[0].mode_id == "off" && rows[1].mode_id == "timeline");

    // Choices: built-ins present, unknown current id appended.
    ModeSet ms;
    for (const char *id : {"normal", "off", "timeline"}) {
        ModeDef m;
        m.id = id;
        m.label = id;
        ms.modes.push_back(m);
    }
    auto ch = buildChoices(ms, "normal");
    assert(ch.size() == ms.modes.size());
    assert(choiceIndex(ch, "normal") == 0 && choiceIndex(ch, "timeline") >= 0);
    auto ch2 = buildChoices(ms, "gone");
    assert(ch2.size() == ms.modes.size() + 1 && !ch2.back().known && ch2.back().id == "gone");
    assert(choiceIndex(ch2, "gone") == int(ch2.size()) - 1);
    assert(choiceIndex(ch, "gone") == -1);
    assert(modeLabel(ms, "timeline") == "timeline" && modeLabel(ms, "gone") == "gone");
    ms.modes[2].label = "Timeline";
    assert(modeLabel(ms, "timeline") == "Timeline");
    ModeSet unlabeled;
    ModeDef nl;
    nl.id = "x";
    unlabeled.modes.push_back(nl);
    assert(buildChoices(unlabeled, "x")[0].label == "x");

    // setRowMode: replace, append, reject.
    assert(setRowMode(ps, Section::APP, "resolve", "normal"));
    assert(ps.sectionSettings(a2).mode == "normal");
    assert(setRowMode(ps, Section::DEVICE, "SpaceMouse Pro", "timeline"));
    assert(ps.sectionSettings(d).mode == "timeline");
    assert(!setRowMode(ps, Section::APP, "unknown-app", "normal"));
    assert(!setRowMode(ps, Section::GLOBAL, "", "normal"));
    assert(!setRowMode(ps, Section::APP, "firefox", "Bad Id!"));
    // Only one MODE key per section after repeated sets.
    assert(setRowMode(ps, Section::APP, "firefox", "off"));
    assert(setRowMode(ps, Section::APP, "firefox", "timeline"));
    int n = 0;
    for (auto &kv : ps.sections[a1].kv) n += kv.first == "MODE";
    assert(n == 1 && ps.sectionSettings(a1).mode == "timeline");

    puts("test_mode_list: ok");
    return 0;
}
