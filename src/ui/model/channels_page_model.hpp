// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Logic of the "Channels" tab of a profile on top of SettingsModel: the groups shown there, reset
// of one group and "Restore defaults" of the profile (both remove the keys from the profile, i.e.
// the inherited values apply again; in [global] the built-in defaults), and the zoom lateral axes
// as a list of checkboxes. No toolkit.
#pragma once
#include <string>
#include <vector>

#include "common.hpp"
#include "ui/model/settings_model.hpp"

namespace spn::ui {

// Groups of the tab, in display order: vertical scroll, horizontal scroll, zoom (channel keys
// plus the zoom detection keys) and the step trigger of "step:" channel actions. ENABLED, MODE
// and the buttons are on the other tabs; BACKEND and TICK_HZ belong to the daemon and are on the
// Settings page (daemonGroup()).
inline std::vector<SettingsModel::Group> channelPageGroups() {
    std::vector<SettingsModel::Group> out;
    const std::vector<SettingsModel::Group> all = SettingsModel::globalChannelGroups();
    for (const SettingsModel::Group &g : all)
        if (g.id == "scroll_v" || g.id == "scroll_h") out.push_back(g);
    for (const SettingsModel::Group &g : all) {
        if (g.id != "zoom") continue;
        SettingsModel::Group z = g;
        for (const SettingsModel::Group &d : all)
            if (d.id == "zoom_detect") z.keys.insert(z.keys.end(), d.keys.begin(), d.keys.end());
        out.push_back(z);
    }
    out.push_back({"axes", "Axis directions", {"INVERT_AXES"}, false});
    out.push_back({"steps", "Step actions", {"STEP_TRIGGER"}, false});
    return out;
}

// The daemon settings (Settings page): they apply to the whole program, not to a profile.
inline SettingsModel::Group daemonGroup() { return {"daemon", "Daemon", {"BACKEND", "TICK_HZ"}, false}; }

// Removes the keys of the group from the profile `section`. true if anything changed.
inline bool resetGroup(SettingsModel &m, size_t section, const SettingsModel::Group &g) {
    bool changed = false;
    for (const std::string &k : g.keys) {
        if (m.isOverridden(section, k)) {
            m.clearOverride(section, k);
            changed = true;
        }
    }
    return changed;
}

// "Restore defaults": all groups of the tab.
inline bool restoreAllDefaults(SettingsModel &m, size_t section) {
    bool changed = false;
    for (const SettingsModel::Group &g : channelPageGroups()) changed = resetGroup(m, section, g) || changed;
    return changed;
}

// INVERT_AXES as a bit set over the six axes (bit = Axis value), like ZOOM_LATERAL.
// ZOOM_LATERAL as a bit set over the six axes (bit = Axis value).
inline unsigned lateralMask(const Settings &s) {
    unsigned m = 0;
    for (Axis a : s.zoom_lateral) m |= 1u << unsigned(a);
    return m;
}
inline std::string lateralText(unsigned mask) {
    std::string t;
    for (int a = 0; a < AX_COUNT; ++a)
        if (mask & (1u << unsigned(a))) t += std::string(t.empty() ? "" : " ") + axisName(Axis(a));
    return t;
}

// True if an axis mix text (valid or not) puts a function on lift/press (axis y). That stays allowed
// but is prone to false triggers when the cap is grabbed or released (issue #94, docs/modes.md).
inline bool mixUsesLiftPress(const std::string &text) {
    Mix m;
    std::string err;
    if (!parseMix(text, &m, &err)) return false;
    for (const Term &t : m.terms)
        if (t.axis == AX_Y && t.weight != 0) return true;
    return false;
}

}  // namespace spn::ui
