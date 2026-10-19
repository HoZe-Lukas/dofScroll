// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Effective values of a profile with their origin: for every channel setting the value that
// applies, and where it comes from (built-in default, [global], the device profile, the mode, the
// app profile). The origin is always given as text, never by colour alone. Toolkit-free.
#pragma once
#include <string>
#include <vector>

#include "settings_model.hpp"
#include "strings.hpp"

namespace spn::ui {

struct EffectiveRow {
    std::string key;
    std::string value;
    SettingsModel::Source source = SettingsModel::Source::BuiltIn;
    std::string sourceText;   // translated, e.g. "from mode \"timeline\""
    bool canReset = false;    // the profile sets the key itself: one click removes it
};

inline std::string sourceText(SettingsModel::Source s, const std::string &modeId, const StringProvider &str) {
    using S = SettingsModel::Source;
    switch (s) {
        case S::BuiltIn: return str.tr("built-in default");
        case S::Global: return str.tr("from the global settings");
        case S::Device: return str.tr("from this device profile");
        case S::App: return str.tr("from this program profile");
        case S::Mode: return format(str.tr("from mode \"%1\""), {modeId});
    }
    return std::string();
}

// One row per channel key the profile can override (set ones and addable ones), in display order.
inline std::vector<EffectiveRow> effectiveRows(const SettingsModel &m, size_t section, const StringProvider &str) {
    std::vector<EffectiveRow> rows;
    if (section >= m.sectionCount()) return rows;
    const SettingsModel::Visible v = m.visibleSettings(section);
    std::vector<std::string> keys = v.overrides.keys;
    keys.insert(keys.end(), v.addableOverrides.begin(), v.addableOverrides.end());
    for (const std::string &key : keys) {
        SettingsModel::Value val;
        EffectiveRow r;
        if (!m.value(section, key, &val) || !m.sourceOf(section, key, &r.source)) continue;
        r.key = key;
        r.value = val.text;
        if (r.source == SettingsModel::Source::Mode) r.value = m.modeOverrideValue(section, key, val.text);
        r.sourceText = sourceText(r.source, v.modeId, str);
        r.canReset = val.overridden;
        rows.push_back(r);
    }
    return rows;
}

}  // namespace spn::ui
