// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Navigation structure of the settings window, without any toolkit: which top-level categories
// (vertical tabs on the left) exist and which pages sit in which category (horizontal tabs on
// top of the content). The window builds its tabs from this; the page registry only says which
// category a page belongs to.
//
//   Categories, in this order: Profiles, Modes, Device, Settings.
//   buildNavigation(pages) groups the pages in the order they have in the registry. A category
//   without pages is left out. A page whose category is unknown is dropped and reported in
//   *problems (the self-test and the model test fail on that, so a typo cannot hide a page).
//   A category with a single page shows no top tab bar (showTabBar).
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace spn::ui {

struct NavInput {  // one registry entry
    std::string id, title, category;
};

struct NavPage {
    std::string id, title;
};

struct NavCategory {
    std::string id, title;  // title: English source string, translated by the view
    std::vector<NavPage> pages;
};

// Category id and English title, in display order.
inline const std::vector<std::pair<std::string, std::string>> &navCategoryDefs() {
    static const std::vector<std::pair<std::string, std::string>> d = {
        {"profiles", "Profiles"}, {"modes", "Modes"}, {"device", "Device"}, {"settings", "Settings"}};
    return d;
}

inline std::vector<NavCategory> buildNavigation(const std::vector<NavInput> &pages,
                                                std::vector<std::string> *problems = nullptr) {
    std::vector<NavCategory> out;
    for (const auto &def : navCategoryDefs()) {
        NavCategory c;
        c.id = def.first;
        c.title = def.second;
        for (const NavInput &p : pages)
            if (p.category == def.first) c.pages.push_back({p.id, p.title});
        if (!c.pages.empty()) out.push_back(std::move(c));
    }
    if (problems) {
        for (const NavInput &p : pages) {
            bool known = false;
            for (const auto &def : navCategoryDefs()) known = known || def.first == p.category;
            if (!known) problems->push_back("page '" + p.id + "' has the unknown category '" + p.category + "'");
        }
    }
    return out;
}

inline bool showTabBar(const NavCategory &c) { return c.pages.size() > 1; }

inline size_t navPageCount(const std::vector<NavCategory> &nav) {
    size_t n = 0;
    for (const NavCategory &c : nav) n += c.pages.size();
    return n;
}

}  // namespace spn::ui
