// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/page_registry.hpp"

#include <QWidget>

#include "pages/axes_page.hpp"
#include "pages/button_modes_page.hpp"
#include "pages/device_page.hpp"
#include "pages/extensions_page.hpp"
#include "pages/general_page.hpp"
#include "pages/modes_page.hpp"
#include "pages/overview_page.hpp"
#include "pages/profiles_page.hpp"
#include "ui/model/ui_terms.hpp"
#include "ui_i18n.hpp"

// One line per page, in tab order; each page declares its factory in its own header. The Channels
// settings are not a page: they are the Channels tab of the profile editor (Profiles).
const std::vector<PageFactory> &pageFactories() {
    namespace terms = spn::ui::terms;
    static const std::vector<PageFactory> factories = {
        {"overview", "Overview", "overview", createOverviewPage},
        {"profiles", "Profiles", "profiles", createProfilesPage},
        {"modes", terms::kJoystickModes, "modes", makeModesPage},
        {"buttonmodes", terms::kButtonModes, "modes", createButtonModesPage},
        {"extensions", "Extensions", "modes", createExtensionsPage},
        {"device", "Device", "device", createDevicePage},
        {"axes", "Axes", "device", createAxesPage},
        {"settings", "Settings", "settings", createGeneralPage},
    };
    return factories;
}
