// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Own functions of this profile" (stage 7a of docs/plan-motion-functions.md), toolkit-free. An own function is the action text a
// profile itself holds for a motion or a device button when it is not one of the plain existing choices: a key function
// (`keys:`, `step:`), a hold, a macro, the pointer, or a key or macro on a button. The same text on several motions and buttons
// is one function that is "used by" all of them. There is no separate table of named functions yet (`FUNC_<name>` is stage 7e):
// the function is the text, written inline in the profile, exactly as the "New function..." dialog writes it.
//
//   ownFunctions(model, section, strings)            // the list: text, what it does in words, used by
//   deleteOwnFunction(&model, section, fn, strings)  // removes it from every user; they inherit again, said in words
//   joystickModeLines(fn, own, model, section)       // the lines of a joystick mode file that is a copy of the function
//
// Deleting sets the users back to inherited, never to "none": what Global, the device profile or the joystick mode say applies
// again, and the words name that for every user.
#pragma once
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

#include "config.hpp"
#include "platform/fs.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/button_mode_catalog.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/mode_catalog.hpp"
#include "ui/model/new_function_model.hpp"
#include "ui/model/profile_assignment_model.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"
#include "utf8.hpp"

namespace spn::ui {

struct OwnUse {
    std::string motionId;  // "h_tilt" ..., empty for a button
    int button = 0;
    std::string key;       // the key of the profile that holds the text: "H_TILT_ACTION", "V_ACTION", "BUTTON_3"
    std::string name;      // "Horizontal tilt", "Button 3" (translated)
    FunctionTarget target() const { return motionId.empty() ? FunctionTarget::buttonNumber(button) : FunctionTarget::motion(motionId); }
};

struct OwnFunction {
    std::string text;               // the action text as the profile holds it
    std::string words;              // what it does, in a few words (translated)
    std::vector<OwnUse> uses;
    bool onMotion() const {
        for (const OwnUse &u : uses)
            if (!u.motionId.empty()) return true;
        return false;
    }
    bool onButton() const {
        for (const OwnUse &u : uses)
            if (u.motionId.empty()) return true;
        return false;
    }
};

namespace own_functions_detail {
inline bool startsWith(const std::string &s, const char *prefix) { return s.rfind(prefix, 0) == 0; }

// A text that is a function of the dialog and not one of the plain choices of the lists.
inline bool ownMotionText(const std::string &t) {
    return startsWith(t, "keys:") || startsWith(t, "step:") || startsWith(t, "hold:") || startsWith(t, "macro:") || startsWith(t, "pointer_");
}
inline bool ownButtonText(const std::string &t) { return startsWith(t, "key:") || startsWith(t, "macro:"); }

struct OwnSlot {
    const char *key;
    const char *motions[2];  // the motions the key sets (two for V_ACTION and H_ACTION)
};
inline const OwnSlot *ownSlots() {
    static const OwnSlot s[] = {{"V_PUSH_ACTION", {"v_push", nullptr}}, {"V_TILT_ACTION", {"v_tilt", nullptr}}, {"H_PUSH_ACTION", {"h_push", nullptr}},
                             {"H_TILT_ACTION", {"h_tilt", nullptr}}, {"V_ACTION", {"v_push", "v_tilt"}},     {"H_ACTION", {"h_push", "h_tilt"}},
                             {"TWIST_ACTION", {"twist", nullptr}},   {"LIFT_ACTION", {"lift", nullptr}},     {nullptr, {nullptr, nullptr}}};
    return s;
}
}  // namespace own_functions_detail

inline std::vector<OwnFunction> ownFunctions(const SettingsModel &m, size_t section, const StringProvider &s) {
    using namespace own_functions_detail;
    std::vector<OwnFunction> out;
    if (section >= m.sectionCount()) return out;
    const ProfileAssignment a = profileAssignment(m, section, s);
    auto nameOf = [&](const std::string &id) {
        for (const MotionRow &r : a.motions)
            if (r.id == id) return r.name;
        return id;
    };
    auto add = [&](const std::string &text, const OwnUse &use, const std::string &words) {
        for (OwnFunction &f : out)
            if (f.text == text) {
                f.uses.push_back(use);
                return;
            }
        OwnFunction f;
        f.text = text;
        f.words = words;
        f.uses.push_back(use);
        out.push_back(f);
    };
    for (const KV &kv : m.profiles().sections[section].kv) {
        const std::string key = utf8::upperAscii(kv.first);
        const std::string text = utf8::lowerAscii(kv.second);
        for (const OwnSlot *sl = ownSlots(); sl->key; ++sl) {
            if (key != sl->key || !ownMotionText(text)) continue;
            for (const char *id : sl->motions) {
                if (!id) continue;
                OwnUse u;
                u.motionId = id;
                u.key = key;
                u.name = nameOf(id);
                std::string words = text;
                for (const MotionRow &r : a.motions)
                    if (r.id == id) words = r.action;
                add(text, u, words);
            }
        }
        if (key.rfind("BUTTON_", 0) == 0 && ownButtonText(text)) {
            OwnUse u;
            // BUTTON_<n> with n = 1..32 and nothing else (atoi on a long digit string is undefined; "3x" is no button)
            const std::string digits = key.substr(7);
            if (digits.empty() || digits.size() > 2 || digits.find_first_not_of("0123456789") != std::string::npos) continue;
            u.button = std::atoi(digits.c_str());
            if (u.button < 1 || u.button > 32) continue;
            u.key = key;
            u.name = format(s.tr("Button %1"), {std::to_string(u.button)});
            add(text, u, buttonActionWords(text, s));
        }
    }
    return out;
}

// What a user does after its function was removed, in words (the profile inherits again).
inline std::string usesWords(const std::vector<OwnUse> &uses) {
    std::string t;
    for (size_t i = 0; i < uses.size(); ++i) t += (i ? (i + 1 == uses.size() ? " and " : ", ") : "") + uses[i].name;
    return t;
}

struct OwnDeleteResult {
    bool ok = false;
    std::string words;  // "Deleted. Horizontal tilt and Button 3 are back to the inherited setting: ..." (translated)
};

// Removes the function from every motion and button that uses it: the key goes (the user inherits again; never "none"). The
// tuning keys of the motions (rate and trigger) go with a key function. The words say for every user what applies now.
inline OwnDeleteResult deleteOwnFunction(SettingsModel *m, size_t section, const OwnFunction &fn, const StringProvider &s) {
    OwnDeleteResult r;
    if (!m || section >= m->sectionCount() || fn.uses.empty()) return r;
    for (const OwnUse &u : fn.uses) {
        m->clearOverride(section, u.key);
        if (!u.motionId.empty()) {
            const std::string tm = motion_edit_detail::keyTuningMotion(*m, section, u.motionId);
            for (KeyField f : {KeyField::RateMin, KeyField::RateMax, KeyField::Trigger}) m->clearOverride(section, keyEngineKey(tm, f));
        }
    }
    std::string now;
    for (const OwnUse &u : fn.uses) {
        std::string what;
        if (!u.motionId.empty()) {
            for (const MotionRow &row : profileAssignment(*m, section, s).motions)
                if (row.id == u.motionId) what = row.action;
        } else {
            what = originBadgeText(buttonOrigin(*m, section, u.button), s);
        }
        now += (now.empty() ? "" : " ") + u.name + ": " + what + ".";
    }
    r.ok = true;
    r.words = format(s.tr("Deleted. %1 inherit again. Now: %2"), {usesWords(fn.uses), now});
    return r;
}

// The lines a joystick mode file gets for a copy of the function: the action key of every motion that uses it and BUTTON_<n> for
// every button, plus the rate and trigger keys of the motions that the profile holds. `own` reads the profile's own text of a key.
inline std::string joystickModeLines(const OwnFunction &fn, const std::function<std::string(const std::string &)> &own, const SettingsModel &m, size_t section) {
    std::string t;
    for (const OwnUse &u : fn.uses) {
        if (u.motionId.empty()) {
            t += "BUTTON_" + std::to_string(u.button) + " = " + fn.text + "\n";
            continue;
        }
        t += motion_edit_detail::partKeyOf(u.motionId) + " = " + fn.text + "\n";
        const std::string tm = motion_edit_detail::keyTuningMotion(m, section, u.motionId);
        for (KeyField f : {KeyField::RateMin, KeyField::RateMax, KeyField::Trigger}) {
            const std::string key = keyEngineKey(tm, f), v = own(key);
            if (!key.empty() && !v.empty()) t += key + " = " + v + "\n";
        }
    }
    return t;
}

struct SaveAsResult {
    bool ok = false;
    std::string id, path, error;  // error: English source text
    std::vector<std::string> warnings;  // loader warnings of the new file
};

// "Save as joystick mode": a new file modes/<id>.conf that is a copy of the function (the action keys of its motions and buttons
// and the tuning the profile holds); the profile keeps its own function (copy, not replace: replacing could change the
// precedence program > mode). `lines` come from joystickModeLines.
inline SaveAsResult saveAsJoystickMode(ModeCatalog *catalog, const std::string &name, const std::string &lines) {
    SaveAsResult r;
    const ModeCatalog::Result created = catalog->createNew(name);
    if (!created.ok) {
        r.error = created.error;
        return r;
    }
    std::string text;
    if (!fs::readFileBounded(created.path, ModeCatalog::kMaxImportBytes, fs::ReadOptions(), &text).ok()) {
        r.error = "cannot read " + created.path;
        return r;
    }
    text += "\n# A copy of a function of a profile (made with New function...).\n" + lines;
    std::string err;
    if (!fs::writeFileReplace(created.path, text, &err)) {
        r.error = err.empty() ? "cannot write " + created.path : err;
        return r;
    }
    catalog->refresh();
    r.ok = true;
    r.id = created.id;
    r.path = created.path;
    if (const ModeCatalog::ModeEntry *e = catalog->find(created.id)) r.warnings = e->warnings;
    return r;
}

// "Save as button mode": buttons/<id>.conf with ACTION = the text. Only a function of a button is a button action.
inline SaveAsResult saveAsButtonMode(ButtonModeCatalog *catalog, const std::string &name, const std::string &actionText) {
    SaveAsResult r;
    const ButtonModeCatalog::Result created = catalog->createNew(name);
    if (!created.ok) {
        r.error = created.error;
        return r;
    }
    const ButtonModeCatalog::Result saved = catalog->save(created.id, name, std::string(), actionText);
    if (!saved.ok) {
        catalog->remove(created.id);  // nothing half made is left behind
        r.error = saved.error;
        return r;
    }
    r.ok = true;
    r.id = created.id;
    r.path = saved.path;
    if (const ButtonModeCatalog::Entry *e = catalog->find(created.id)) r.warnings = e->warnings;
    return r;
}

}  // namespace spn::ui
