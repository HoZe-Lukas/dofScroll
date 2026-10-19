// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/page_registry.hpp"

#include "pages/modes_page.hpp"

// One line per page; each page declares its make...Page() in its own header.
const std::vector<PageFactory> &pageFactories() {
    static const std::vector<PageFactory> factories = {
        {"Modes", makeModesPage},
    };
    return factories;
}
