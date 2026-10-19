// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/page_registry.hpp"

#include <QWidget>

#include "pages/axes_page.hpp"
#include "pages/channels_page.hpp"
#include "pages/general_page.hpp"
#include "pages/modes_page.hpp"
#include "pages/overview_page.hpp"
#include "pages/profiles_page.hpp"
#include "ui_i18n.hpp"

// One line per page, in tab order; each page declares its factory in its own header.
const std::vector<PageFactory> &pageFactories() {
    static const std::vector<PageFactory> factories = {
        {"overview", "Overview", "overview", createOverviewPage},
        {"profiles", "Profiles", "profiles", createProfilesPage},
        {"channels", "Channels", "profiles", createChannelsPage},
        {"modes", "Modes", "modes", makeModesPage},
        {"axes", "Axes", "device", createAxesPage},
        {"general", "General", "settings", createGeneralPage},
    };
    return factories;
}
