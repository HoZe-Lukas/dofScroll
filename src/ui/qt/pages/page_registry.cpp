// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "page_registry.hpp"

#include "profiles_page.hpp"

const std::vector<PageEntry> &pageRegistry() {
    static const std::vector<PageEntry> pages = {
        {"profiles", "Profiles", &createProfilesPage},
    };
    return pages;
}
