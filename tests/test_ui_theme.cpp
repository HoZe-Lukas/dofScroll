// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Theme setting, colours (WCAG contrast) and spacing tokens.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "testutil.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/theme_colors.hpp"
#include "ui/model/ui_settings.hpp"

using namespace spn::ui;

static bool near(double a, double b, double eps = 0.01) { return std::fabs(a - b) < eps; }

static void testSetting() {
    assert(themeFromId("light") == Theme::Light && themeFromId("dark") == Theme::Dark);
    assert(themeFromId("system") == Theme::System && themeFromId("") == Theme::System && themeFromId("Dark") == Theme::System);
    assert(std::string(themeId(Theme::Dark)) == "dark" && std::string(themeId(Theme::Light)) == "light" &&
           std::string(themeId(Theme::System)) == "system");
    // both settings round-trip, unknown values fall back, order and spaces do not matter
    UiSettings s;
    s.language = Language::German;
    s.theme = Theme::Dark;
    assert(parseUiSettings(serializeUiSettings(s)) == s);
    assert(parseUiSettings("theme=light\n  language =  en \n").theme == Theme::Light);
    assert(parseUiSettings("theme=light\n  language =  en \n").language == Language::English);
    assert(parseUiSettings("theme = neon\n").theme == Theme::System);
    assert(parseUiSettings("language = de\n").theme == Theme::System);  // an old file without a theme
    assert(parseUiSettings("theme = dark\ntheme = light\n").theme == Theme::Light);  // last wins
    assert(!(UiSettings{Language::System, Theme::Dark} == UiSettings{Language::System, Theme::Light}));
}

static void testUpdateKeepsTheOtherSetting() {
    const std::string dir = tu::makeTempDir("dofscroll-ui-settings");
    const std::string path = dir + "/ui.conf";
    std::string err;
    // no file yet: defaults plus the change
    assert(updateUiSettings(path, [](UiSettings &s) { s.language = Language::German; }, &err));
    assert(loadUiSettings(path).language == Language::German && loadUiSettings(path).theme == Theme::System);
    // saving the theme keeps the language, saving the language keeps the theme
    assert(updateUiSettings(path, [](UiSettings &s) { s.theme = Theme::Dark; }, &err));
    assert(loadUiSettings(path).language == Language::German && loadUiSettings(path).theme == Theme::Dark);
    assert(updateUiSettings(path, [](UiSettings &s) { s.language = Language::English; }, &err));
    assert(loadUiSettings(path).language == Language::English && loadUiSettings(path).theme == Theme::Dark);
    // a directory that cannot be created (its parent is a regular file) reports the error
    std::ofstream(dir + "/blocker") << "x";
    err.clear();
    assert(!updateUiSettings(dir + "/blocker/sub/ui.conf", [](UiSettings &s) { s.theme = Theme::Light; }, &err) && !err.empty());
    std::filesystem::remove_all(dir);
}

static void testContrastMath() {
    const Rgb black{0, 0, 0}, white{255, 255, 255};
    assert(near(contrastRatio(black, white), 21.0) && near(contrastRatio(white, black), 21.0));
    assert(near(contrastRatio(white, white), 1.0));
    assert(near(relativeLuminance(white), 1.0) && near(relativeLuminance(black), 0.0));
    // a known mid grey: #777777 on white is about 4.48:1
    assert(near(contrastRatio({0x77, 0x77, 0x77}, white), 4.48, 0.05));
}

static void testThemesAreReadable() {
    for (Theme t : {Theme::Light, Theme::Dark}) {
        ThemeColors c;
        assert(themeColors(t, &c));
        // normal text on window and input background: 7:1 (AAA); everything else at least 4.5:1 (AA)
        assert(contrastRatio(c.windowText, c.window) >= 7.0);
        assert(contrastRatio(c.text, c.base) >= 7.0);
        assert(contrastRatio(c.buttonText, c.button) >= 7.0);
        assert(minTextContrast(c) >= 4.5);
    }
    ThemeColors none;
    assert(!themeColors(Theme::System, &none));
    // the themes are really different: light is light, dark is dark
    assert(relativeLuminance(lightColors().window) > 0.8 && relativeLuminance(darkColors().window) < 0.05);
    assert(!(lightColors().window == darkColors().window));
}

static void testTokens() {
    using namespace spn::ui::tokens;
    static_assert(kSpaceXs < kSpaceSm && kSpaceSm < kSpaceMd && kSpaceMd < kSpaceLg && kSpaceLg < kSpaceXl, "scale grows");
    static_assert(kSpaceXs % 4 == 0 && kSpaceSm % 4 == 0 && kSpaceMd % 4 == 0 && kSpaceLg % 4 == 0 && kSpaceXl % 4 == 0,
                  "multiples of 4");
    static_assert(kIconSize >= 16 && kMinWindowWidth >= 480 && kMinWindowHeight >= 320, "usable minimum sizes");
}

int main() {
    testSetting();
    testUpdateKeepsTheOtherSetting();
    testContrastMath();
    testThemesAreReadable();
    testTokens();
    return 0;
}
