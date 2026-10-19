// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Navigation model: categories, page order, tab bar rule, unknown categories.
#undef NDEBUG
#include <cassert>
#include <string>
#include <vector>

#include "ui/model/navigation_model.hpp"

using namespace spn::ui;

static void testGrouping() {
    const std::vector<NavInput> in = {{"profiles", "Profiles", "profiles"}, {"channels", "Channels", "profiles"},
                                      {"modes", "Modes", "modes"},          {"axes", "Axes", "device"},
                                      {"general", "General", "settings"}};
    std::vector<std::string> problems;
    const std::vector<NavCategory> nav = buildNavigation(in, &problems);
    assert(problems.empty());
    assert(nav.size() == 4);
    assert(nav[0].id == "profiles" && nav[1].id == "modes" && nav[2].id == "device" && nav[3].id == "settings");
    assert(nav[0].pages.size() == 2 && nav[0].pages[0].id == "profiles" && nav[0].pages[1].id == "channels");
    assert(navPageCount(nav) == 5);
    // a single page needs no top tab bar
    assert(showTabBar(nav[0]) && !showTabBar(nav[1]) && !showTabBar(nav[2]) && !showTabBar(nav[3]));
}

static void testOrderFollowsTheRegistry() {
    // the registry order inside a category is kept, whatever the order of the categories in the input
    const std::vector<NavInput> in = {{"b", "B", "settings"}, {"a", "A", "profiles"}, {"c", "C", "settings"}};
    const std::vector<NavCategory> nav = buildNavigation(in);
    assert(nav.size() == 2 && nav[0].id == "profiles" && nav[1].id == "settings");
    assert(nav[1].pages.size() == 2 && nav[1].pages[0].id == "b" && nav[1].pages[1].id == "c");
}

static void testProblemsAndEmpty() {
    std::vector<std::string> problems;
    const std::vector<NavCategory> nav = buildNavigation({{"x", "X", "nosuch"}, {"y", "Y", "modes"}}, &problems);
    assert(problems.size() == 1 && problems[0].find("'x'") != std::string::npos);
    assert(nav.size() == 1 && nav[0].id == "modes" && navPageCount(nav) == 1);  // x is dropped
    assert(buildNavigation({}).empty());
    // no categories without pages: an empty input gives no tabs at all
    assert(navCategoryDefs().size() == 4);
}

int main() {
    testGrouping();
    testOrderFollowsTheRegistry();
    testProblemsAndEmpty();
    return 0;
}
