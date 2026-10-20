// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Inheritance, made explicit (toolkit-free): where the value of a setting or a button in a profile
// comes from and what the profile editor says about it. Precedence (AGENTS.md): Global < device <
// joystick-mode overrides < program. Everything can be set in Global like in any other profile; a
// device or program profile only holds what it changes.
//
//   const KeyOrigin o = keyOrigin(settings, section, "SCROLL_V_SENS");
//   originBadgeText(o, strings)      // "Inherited from Global: 5" / "Set in this profile (inherited: 5)"
//   const std::string hint = overrideHint(settings, "SCROLL_V_SENS", strings);  // Global: "2 profiles override this"
//   headerExplanation(section, strings)  // the one-line text under the profile name
//
// A program profile cannot know which device is active at run time, so it shows the value of Global
// and names the device profiles that set the same key ("device X sets 8").
#pragma once
#include <string>
#include <vector>

#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct KeyOrigin {
    enum class Kind {
        Plain,           // [global] holds the value itself: no inheritance to show
        SetHere,         // the profile sets the key itself
        FromGlobal,      // inherited; [global] sets the key in the file
        FromGlobalDefault,  // inherited; [global] does not set it, so the built-in default applies
        FromMode,        // the joystick mode of the profile overrides the key
    };
    Kind kind = Kind::Plain;
    std::string key;                      // the config key ("SCROLL_V_SENS", "BUTTON_2")
    std::string value;                    // the effective value (text)
    std::string inherited;                // the value the profile would have without its own setting
    std::string modeId;                   // FromMode
    std::vector<std::string> deviceNames; // program profiles: device profiles that set the key too
    std::vector<std::string> deviceValues;  // their values, same order
    bool canReset = false;                // the profile sets the key itself: Reset removes it
};

// Origin of a settings key (not a button) in section `section`.
inline KeyOrigin keyOrigin(const SettingsModel &m, size_t section, const std::string &key) {
    KeyOrigin o;
    SettingsModel::Value v;
    o.key = key;
    if (section >= m.sectionCount() || !m.value(section, key, &v)) return o;
    SettingsModel::Source src;
    const bool haveSrc = m.sourceOf(section, key, &src);
    o.value = v.text;
    o.inherited = v.inherited;
    const SettingsModel::SectionInfo info = m.sections()[section];
    if (haveSrc && src == SettingsModel::Source::Mode) {
        o.kind = KeyOrigin::Kind::FromMode;
        o.modeId = m.effectiveMode(section);
        o.value = m.modeOverrideValue(section, key, v.text);
        o.canReset = section == 0 ? v.text != v.inherited : v.overridden;
    } else if (section == 0) {
        o.kind = KeyOrigin::Kind::Plain;
        o.canReset = v.text != v.inherited;  // differs from the built-in default
    } else if (v.overridden) {
        o.kind = KeyOrigin::Kind::SetHere;
        o.canReset = true;
    } else {
        o.kind = m.isOverridden(0, key) ? KeyOrigin::Kind::FromGlobal : KeyOrigin::Kind::FromGlobalDefault;
    }
    if (section > 0 && info.kind == Section::APP && !v.overridden) {
        // device profiles that set the key too (they apply while that device is used)
        for (size_t i : m.profilesOverriding(key)) {
            const SettingsModel::SectionInfo di = m.sections()[i];
            if (di.kind != Section::DEVICE) continue;
            SettingsModel::Value dv;
            if (!m.value(i, key, &dv)) continue;
            o.deviceNames.push_back(di.pattern);
            o.deviceValues.push_back(dv.text);
        }
    }
    return o;
}

// Origin of the binding of button n: its text is the action ("none", "key:space", ...).
inline KeyOrigin buttonOrigin(const SettingsModel &m, size_t section, int number) {
    KeyOrigin o;
    if (section >= m.sectionCount()) return o;
    const std::string key = "BUTTON_" + std::to_string(number);
    o.key = key;
    const std::vector<SettingsModel::ButtonRow> rows = m.buttonRows(section);
    for (const SettingsModel::ButtonRow &r : rows)
        if (r.number == number) {
            o.value = r.action;
            o.canReset = r.overridden;
        }
    const bool globalSets = m.isOverridden(0, key);
    if (section == 0) {
        o.kind = KeyOrigin::Kind::Plain;
        o.canReset = o.value != "none";
        return o;
    }
    // what the profile would have without its own binding
    const std::vector<SettingsModel::ButtonRow> grows = m.buttonRows(0);
    o.inherited = "none";
    for (const SettingsModel::ButtonRow &r : grows)
        if (r.number == number) o.inherited = r.action;
    if (o.canReset) o.kind = KeyOrigin::Kind::SetHere;
    else o.kind = globalSets ? KeyOrigin::Kind::FromGlobal : KeyOrigin::Kind::FromGlobalDefault;
    if (m.sections()[section].kind == Section::APP && !o.canReset) {
        for (size_t i : m.profilesOverriding(key)) {
            const SettingsModel::SectionInfo di = m.sections()[i];
            if (di.kind != Section::DEVICE) continue;
            for (const SettingsModel::ButtonRow &r : m.buttonRows(i))
                if (r.number == number) {
                    o.deviceNames.push_back(di.pattern);
                    o.deviceValues.push_back(r.action);
                }
        }
    }
    return o;
}

// A value the way a person reads it: the on/off switch and the direction are words, everything else as is.
inline std::string displayValue(const std::string &key, const std::string &text, const StringProvider &s) {
    if (key == "ENABLED") return text == "0" ? s.tr("off") : s.tr("on");
    if (key.size() > 5 && key.compare(key.size() - 5, 5, "_SIGN") == 0) return text == "-1" ? s.tr("inverted") : s.tr("normal");
    return text;
}

// The short text of a row: where the value comes from, with the inherited value as a hint. "" for
// the plain values of [global]. The state is always written out, never shown by colour alone.
inline std::string originBadgeText(const KeyOrigin &o0, const StringProvider &s) {
    KeyOrigin o = o0;
    o.value = displayValue(o.key, o.value, s);
    o.inherited = displayValue(o.key, o.inherited, s);
    for (std::string &v : o.deviceValues) v = displayValue(o.key, v, s);
    std::string t;
    switch (o.kind) {
        case KeyOrigin::Kind::Plain: return std::string();
        case KeyOrigin::Kind::SetHere:
            t = o.inherited.empty() ? s.tr("Set in this profile")
                                    : format(s.tr("Set in this profile (inherited: %1)"), {o.inherited});
            break;
        case KeyOrigin::Kind::FromGlobal:
            t = o.value.empty() ? s.tr("Inherited from Global") : format(s.tr("Inherited from Global: %1"), {o.value});
            break;
        case KeyOrigin::Kind::FromGlobalDefault:
            t = o.value.empty() ? s.tr("Inherited from Global (built-in default)")
                                : format(s.tr("Inherited from Global (built-in default): %1"), {o.value});
            break;
        case KeyOrigin::Kind::FromMode:
            t = format(s.tr("From joystick mode \"%1\": %2"), {o.modeId, o.value});
            break;
    }
    for (size_t i = 0; i < o.deviceNames.size(); ++i)
        t += "; " + format(s.tr("device %1 sets %2"), {o.deviceNames[i], o.deviceValues[i]});
    return t;
}

// Tooltip of the Reset button of a row.
inline std::string resetTooltip(size_t section, bool canReset, const StringProvider &s) {
    if (section == 0) return canReset ? s.tr("Back to the built-in default") : s.tr("Already the built-in default");
    return canReset ? s.tr("Reset to inherited: remove this profile's own value") : s.tr("Nothing to reset: this profile inherits it");
}

// "N profiles override this" (a value of [global] that device or program profiles set themselves,
// so that it has no effect there); "" when no profile overrides it. `names` (if given) gets the
// profile names for the tooltip.
inline std::string overrideHint(const SettingsModel &m, const std::string &key, const StringProvider &s,
                                std::vector<std::string> *names = nullptr) {
    const std::vector<size_t> ps = m.profilesOverriding(key);
    if (names) {
        names->clear();
        for (size_t i : ps) {
            const SettingsModel::SectionInfo info = m.sections()[i];
            names->push_back((info.kind == Section::DEVICE ? s.tr("device") : s.tr("program")) + " " + info.pattern);
        }
    }
    if (ps.empty()) return std::string();
    return ps.size() == 1 ? s.tr("1 profile overrides this")
                          : format(s.tr("%1 profiles override this"), {std::to_string(ps.size())});
}

// The tooltip text for an overrideHint: one profile per line.
inline std::string overrideHintTooltip(const std::vector<std::string> &names, const StringProvider &s) {
    std::string t = s.tr("These profiles set their own value, so the value of Global has no effect there:");
    for (const std::string &n : names) t += "\n - " + n;
    return t;
}

// The one-line explanation under the name of the edited profile.
inline std::string headerExplanation(size_t section, Section::Kind kind, const StringProvider &s) {
    if (section == 0) return s.tr("Applies to every device and program unless a device or program profile overrides it.");
    (void)kind;
    return s.tr("Everything you do not change here is inherited from Global (and the device).");
}

// "firefox (3)": the label of a profile in the list, with the number of settings it overrides.
inline std::string profileListLabel(const std::string &name, size_t overrides) {
    return overrides ? name + " (" + std::to_string(overrides) + ")" : name;
}
inline std::string profileOverrideCountText(size_t overrides, const StringProvider &s) {
    if (overrides == 0) return s.tr("Overrides nothing: everything is inherited");
    return overrides == 1 ? s.tr("Overrides 1 setting") : format(s.tr("Overrides %1 settings"), {std::to_string(overrides)});
}

}  // namespace spn::ui
