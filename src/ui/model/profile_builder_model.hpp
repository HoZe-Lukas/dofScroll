// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The profile builder behind "New profile" (toolkit-free): makes a normal profile (a program or device profile) from
// existing parts: a base profile to start from, a joystick mode, a button mode per button, and single motions or
// buttons taken over from other profiles (borrow_model.hpp). The result is a profile like any other; it stays editable
// on the Motions and buttons tab and nothing remembers how it was built.
//
//   ProfileBuild b;
//   b.kind = Section::APP;  b.name = "firefox";
//   b.base = ProfileSet::npos;                       // or the section of a profile whose own settings are copied
//   b.modeId = "timeline";  b.modeOwnCopy = false;   // MODE = timeline (follows the mode) or a copy of its file
//   b.buttons.push_back({3, "next-mode", false});    // BUTTON_3 = buttonmode:next-mode (follows) or a copy
//   b.motions.push_back({"v_tilt", source, ""});     // take the tilt's action over from another profile
//   const ProfileBuildResult r = buildProfile(model, b, &modeCatalog, &buttonCatalog);
//
// "Follows the mode": the profile only names the joystick mode or button mode (MODE = <id>, BUTTON_<n> =
// buttonmode:<id>), so a later change of that mode changes the profile too. "Make my own copy": the mode is
// duplicated into the user's own folder under a new name ("<mode> (<profile>)") and the profile names the copy; the
// copy does not change with the original. Everything is checked before anything is written; a failure at any step
// removes the new profile and the copies made so far, so a refused build leaves nothing behind. A name that already
// exists for the kind is refused (matching is ASCII case-insensitive, as for every profile name).
#pragma once
#include <cstdlib>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include "ui/model/app_picker_model.hpp"
#include "ui/model/borrow_model.hpp"
#include "ui/model/button_mode_catalog.hpp"
#include "ui/model/mode_catalog.hpp"
#include "ui/model/settings_model.hpp"
#include "utf8.hpp"

namespace spn::ui {

struct ProfileBuild {
    struct ButtonChoice {
        int number = 0;
        std::string buttonModeId;  // an existing button mode
        bool ownCopy = false;      // copy it into the own folder instead of following it
    };
    struct MotionTake {
        std::string motion;  // "v_push" ... "twist"
        BorrowSource from;
        std::string fromMotion;  // "" = the same motion
    };
    struct ButtonTake {
        int number = 0;
        BorrowSource from;
    };
    Section::Kind kind = Section::APP;
    std::string name;                // the match name, as typed
    size_t base = ProfileSet::npos;  // a profile whose own settings are copied first (npos = start empty)
    std::string modeId;              // "" = no MODE of its own (inherits)
    bool modeOwnCopy = false;
    std::vector<ButtonChoice> buttons;
    std::vector<MotionTake> motions;
    std::vector<ButtonTake> buttonTakes;
};

struct ProfileBuildResult {
    bool ok = false;
    size_t section = ProfileSet::npos;
    std::string error;   // English source text
    std::string modeId;  // the joystick mode the profile names ("" = none); the copy's id for an own copy
    std::vector<std::string> createdModes, createdButtonModes;  // the copies made
};

namespace profile_builder_detail {
inline std::string uniqueCopyName(const std::string &label, const std::string &profile, const std::function<bool(const std::string &)> &isFree) {
    const std::string base = label + " (" + profile + ")";
    if (isFree(base)) return base;
    for (int n = 2; n < 100; ++n)
        if (isFree(base + " " + std::to_string(n))) return base + " " + std::to_string(n);
    return std::string();
}
}  // namespace profile_builder_detail

// Does a profile of this kind already use the name? Compared ASCII case-insensitively.
inline bool profileNameTaken(const SettingsModel &m, Section::Kind kind, const std::string &normalized) {
    const std::string low = utf8::lowerAscii(normalized);
    for (const SettingsModel::SectionInfo &i : m.sections())
        if (i.index != 0 && i.kind == kind && utf8::lowerAscii(i.pattern) == low) return true;
    return false;
}

inline ProfileBuildResult buildProfile(SettingsModel &m, const ProfileBuild &b, ModeCatalog *modes, ButtonModeCatalog *buttonModes) {
    ProfileBuildResult r;
    auto fail = [&](const std::string &e) {
        r.ok = false;
        r.error = e;
        return r;
    };
    if (b.kind == Section::GLOBAL) return fail("choose a program or a device");
    std::string norm, e;
    if (!validateProfilePattern(b.name, &norm, &e)) return fail(e);
    if (profileNameTaken(m, b.kind, norm)) return fail("a profile with this name already exists");
    if (b.base != ProfileSet::npos && b.base >= m.sectionCount()) return fail("no such profile to start from");
    // checks before anything is written
    if (!b.modeId.empty()) {
        if (!validModeId(b.modeId)) return fail("invalid joystick mode id");
        if (!m.modeDef(b.modeId)) return fail("no such joystick mode");
        if (b.modeOwnCopy && !modes) return fail("copying a joystick mode is not possible here");
    }
    std::set<int> seenButtons;
    for (const ProfileBuild::ButtonChoice &c : b.buttons) {
        if (c.number < 1 || c.number > int(kMaxButtons)) return fail("no such button");
        if (!seenButtons.insert(c.number).second) return fail("a button is chosen twice");
        if (!buttonModes || !buttonModes->find(c.buttonModeId)) return fail("no such button mode");
    }
    for (const ProfileBuild::ButtonTake &t : b.buttonTakes) {
        if (t.number < 1 || t.number > int(kMaxButtons)) return fail("no such button");
        if (!seenButtons.insert(t.number).second) return fail("a button is chosen twice");
    }
    std::set<std::string> seenMotions;
    for (const ProfileBuild::MotionTake &t : b.motions)
        if (!seenMotions.insert(t.motion).second) return fail("a motion is chosen twice");

    // the profile, the base, the mode, the buttons, the taken-over motions and buttons
    const size_t sec = m.addSection(b.kind, norm, &e);
    if (sec == ProfileSet::npos) return fail(e);
    std::vector<std::string> madeModes, madeButtonModes;
    auto rollback = [&](const std::string &why) {
        m.removeSection(sec, nullptr);
        for (const std::string &id : madeModes) modes->remove(id);
        for (const std::string &id : madeButtonModes) buttonModes->remove(id);
        if (!madeModes.empty()) m.setModes(modes->modeSet(), modes->dir());
        return fail(why);
    };
    std::string err;
    if (b.base != ProfileSet::npos) {
        const Section base = m.profiles().sections[b.base];  // a copy: the model changes below
        for (const KV &kv : base.kv) {
            const std::string key = utf8::upperAscii(kv.first);
            if (key.rfind("BUTTON_", 0) == 0) {
                const int n = std::atoi(key.c_str() + 7);
                if (n > m.sectionButtonLimit(sec)) continue;  // the device has no such button
                if (!m.setButton(sec, n, kv.second, &err)) return rollback(err);
            } else if (!m.setValue(sec, key, kv.second, &err)) {
                return rollback(err);
            }
        }
    }
    std::string modeId = b.modeId;
    if (!modeId.empty()) {
        if (b.modeOwnCopy) {
            const ModeDef *src = m.modeDef(modeId);
            const std::string label = src && !src->label.empty() ? src->label : modeId;
            const std::string name = profile_builder_detail::uniqueCopyName(label, norm, [&](const std::string &n) {
                const std::string id = ModeCatalog::deriveId(n);
                return !id.empty() && !modes->taken(id);
            });
            if (name.empty()) return rollback("could not find a free name for the copy of the joystick mode");
            const ModeCatalog::Result c = modes->duplicate(modeId, name);
            if (!c.ok) return rollback(c.error);
            madeModes.push_back(c.id);
            modeId = c.id;
            m.setModes(modes->modeSet(), modes->dir());
        }
        if (!m.setValue(sec, "MODE", modeId, &err)) return rollback(err);
    }
    r.modeId = modeId;
    for (const ProfileBuild::ButtonChoice &c : b.buttons) {
        if (c.number > m.sectionButtonLimit(sec)) return rollback("this device has fewer buttons: button " + std::to_string(c.number));
        std::string id = c.buttonModeId;
        if (c.ownCopy) {
            const ButtonModeCatalog::Entry *src = buttonModes->find(id);
            const std::string label = src && !src->label.empty() ? src->label : id;
            const std::string name = profile_builder_detail::uniqueCopyName(label, norm, [&](const std::string &n) {
                const std::string nid = ModeCatalog::deriveId(n);
                return !nid.empty() && !buttonModes->taken(nid);
            });
            if (name.empty()) return rollback("could not find a free name for the copy of the button mode");
            const ButtonModeCatalog::Result d = buttonModes->duplicate(id, name);
            if (!d.ok) return rollback(d.error);
            madeButtonModes.push_back(d.id);
            id = d.id;
        }
        if (!m.setButton(sec, c.number, "buttonmode:" + id, &err)) return rollback(err);
    }
    for (const ProfileBuild::MotionTake &t : b.motions)
        if (!borrowMotion(m, sec, t.motion, t.from, &err, t.fromMotion)) return rollback(err);
    for (const ProfileBuild::ButtonTake &t : b.buttonTakes)
        if (!borrowButton(m, sec, t.number, t.from, &err, buttonModes ? &buttonModes->set() : nullptr)) return rollback(err);
    r.ok = true;
    r.section = sec;
    r.createdModes = madeModes;
    r.createdButtonModes = madeButtonModes;
    return r;
}

}  // namespace spn::ui
