// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Logic of the "Channels" page on top of SettingsModel: the groups shown on the page, reset of one
// group and "restore defaults" (both remove the keys from [global], i.e. the built-in defaults
// apply again), and the zoom lateral axes as a list of checkboxes. No toolkit.
#pragma once
#include <string>
#include <vector>

#include "common.hpp"
#include "ui/model/settings_model.hpp"

namespace spn::ui {

// Groups of the page, in display order: vertical scroll, horizontal scroll, zoom (channel keys
// plus the zoom detection keys) and "daemon" (backend, tick rate). ENABLED is the tray's toggle
// and not part of the page.
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
    out.push_back({"daemon", "Daemon", {"BACKEND", "TICK_HZ"}, false});
    return out;
}

// Removes the keys of the group from [global]. true if anything changed.
inline bool resetGroup(SettingsModel &m, const SettingsModel::Group &g) {
    bool changed = false;
    for (const std::string &k : g.keys) {
        if (m.isOverridden(0, k)) {
            m.clearOverride(0, k);
            changed = true;
        }
    }
    return changed;
}

// "Restore defaults": all groups of the page.
inline bool restoreAllDefaults(SettingsModel &m) {
    bool changed = false;
    for (const SettingsModel::Group &g : channelPageGroups()) changed = resetGroup(m, g) || changed;
    return changed;
}

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

}  // namespace spn::ui
