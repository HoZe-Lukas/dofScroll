// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include <cassert>
#include <cstdio>
#include <cstdlib>

#include "mode_list.hpp"
#include "testutil.hpp"

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

    // Quick mode menu.
    {
        ProfileSet g0;
        assert(globalModeId(g0) == "normal");
        assert(setGlobalMode(g0, "timeline") && globalModeId(g0) == "timeline");
        g0.sections[0].kv.emplace_back("MODE", "off");  // duplicate key
        assert(setGlobalMode(g0, "off") && globalModeId(g0) == "off");
        assert(setGlobalMode(g0, "normal") && globalModeId(g0) == "normal");
        for (auto &kv : g0.sections[0].kv) assert(kv.first != "MODE");
        assert(!setGlobalMode(g0, "Bad Id!") && !setGlobalMode(g0, ""));

        auto me = buildMenuEntries(ms, "timeline");
        assert(me.size() == 3 && me[0].id == "normal" && !me[0].active);
        assert(me[2].active && me[2].text == "Timeline (timeline)" && me[1].text == "off");
        int act = 0;
        for (auto &e : me) act += e.active;
        assert(act == 1);
        auto mu = buildMenuEntries(ms, "gone");
        assert(mu.size() == 4 && mu[3].active && !mu[3].known && mu[3].text == "gone");
        for (size_t i = 0; i < 3; ++i) assert(!mu[i].active);
    }

    {
        // Editing from the tray: entries, template round trip, never overwrite, invalid ids.
        const std::string tmpdir = tu::makeTempDir("dofscroll-ml");
        const std::string dir = tmpdir + "/modes";
        ModeSet fms = loadModes(dir, nullptr);
        auto ee = buildEditEntries(fms);
        assert(ee.size() == 3);
        for (auto &e : ee) assert(!e.has_file);
        assert(modeFilePath(dir, "../x").empty() && modeFilePath("", "a").empty());
        assert(modeFilePath(dir, "timeline") == dir + "/timeline.conf");

        for (const char *id : {"normal", "off", "timeline"}) {
            const ModeDef *b = fms.find(id);
            assert(b && b->builtin);
            std::string path, err;
            assert(createModeFile(dir, id, modeTemplate(*b), &path, &err));
#ifndef _WIN32
            struct stat st;
            assert(::stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
#endif
            assert(tu::exists(path));
            // Second attempt must refuse and leave the file alone.
            assert(!createModeFile(dir, id, "junk", &path, &err) && err == "file already exists");
        }
        std::vector<std::string> warn;
        ModeSet ms2 = loadModes(dir, &warn);
        assert(warn.empty() && ms2.modes.size() == 3);
        for (const ModeDef &b : fms.modes) {
            const ModeDef *f = ms2.find(b.id);
            assert(f && !f->builtin);
            assert(f->label == b.label && f->description == b.description);
            assert(f->v == b.v && f->h == b.h && f->twist == b.twist);
            assert(f->buttons.size() == b.buttons.size() && f->overrides == b.overrides);
        }
        for (auto &e : buildEditEntries(ms2)) assert(e.has_file);

        std::string err;
        assert(!createModeFile(dir, "Bad Id", "x", nullptr, &err));
        assert(!createModeFile(dir, "", "x", nullptr, &err));
        assert(!createModeFile(dir, "../escape", "x", nullptr, &err));

        // Dangling symlink at the target: O_EXCL|O_NOFOLLOW refuses.
#ifndef _WIN32
        assert(::symlink("/nonexistent/target", (dir + "/link.conf").c_str()) == 0);
        assert(!createModeFile(dir, "link", "x", nullptr, &err));
#endif

        // Template with buttons and overrides survives the round trip.
        ModeDef m;
        m.id = "custom";
        m.label = "Custom";
        m.description = "d";
        m.h = {ChannelActionType::Keys, Key::Left, Key::Right};
        m.twist = {ChannelActionType::Zoom};
        m.buttons.push_back({1, ButtonActionType::Key, Key::Space, ""});
        m.buttons.push_back({2, ButtonActionType::ModeSwitch, Key::Space, "next"});
        m.overrides.emplace_back("SCROLL_H_SENS", "4");
        assert(createModeFile(dir, "custom", modeTemplate(m), nullptr, &err));
        const ModeSet ms3 = loadModes(dir, nullptr);
        const ModeDef *c = ms3.find("custom");
        assert(c && c->h == m.h && c->twist == m.twist && c->buttons.size() == 2);
        assert(c->buttons[1].mode == "next" && c->overrides == m.overrides);

        tu::removeAll(tmpdir);
    }

    puts("test_mode_list: ok");
    return 0;
}
