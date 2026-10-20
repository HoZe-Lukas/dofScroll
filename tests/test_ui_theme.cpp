// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Theme setting, colours (WCAG contrast) and spacing tokens.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "testutil.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/theme_colors.hpp"
#include "ui/model/ui_settings.hpp"

using namespace spn::ui;

static bool closeTo(double a, double b, double eps = 0.01) { return std::fabs(a - b) < eps; }

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
    UiSettings dark, light;
    dark.theme = Theme::Dark;
    light.theme = Theme::Light;
    assert(!(dark == light));
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
    // an existing file that cannot be loaded (oversized) is not overwritten: the other setting stays
    const std::string big = dir + "/big.conf";
    {
        std::ofstream out(big, std::ios::binary);
        out << "language = de\n# " << std::string(5000, 'x') << "\n";
    }
    err.clear();
    const bool bigSaved = updateUiSettings(big, [](UiSettings &s) { s.theme = Theme::Dark; }, &err);
    assert(!bigSaved && !err.empty());
    std::string kept;
    {
        std::ifstream in(big, std::ios::binary);
        kept.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    assert(kept.find("language = de") == 0 && kept.size() > 5000);
    // a directory in place of the file is refused as well
    std::error_code ec;
    std::filesystem::create_directory(dir + "/asdir.conf", ec);
    err.clear();
    const bool dirSaved = updateUiSettings(dir + "/asdir.conf", [](UiSettings &s) { s.theme = Theme::Dark; }, &err);
    assert(!dirSaved && !err.empty());
    std::filesystem::remove_all(dir);
}

static void testContrastMath() {
    const Rgb black{0, 0, 0}, white{255, 255, 255};
    assert(closeTo(contrastRatio(black, white), 21.0) && closeTo(contrastRatio(white, black), 21.0));
    assert(closeTo(contrastRatio(white, white), 1.0));
    assert(closeTo(relativeLuminance(white), 1.0) && closeTo(relativeLuminance(black), 0.0));
    // a known mid grey: #777777 on white is about 4.48:1
    assert(closeTo(contrastRatio({0x77, 0x77, 0x77}, white), 4.48, 0.05));
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
        assert(minAccentContrast(c) >= 3.0);  // focus ring and selection marks (WCAG 1.4.11)
        assert(contrastRatio(c.highlightedText, c.highlight) >= 4.5);  // text on a primary button
        assert(contrastRatio(c.placeholderText, c.surface) >= 4.5);    // muted help text on a card
        assert(contrastRatio(c.danger, c.window) >= 4.5 && contrastRatio(c.danger, c.surface) >= 4.5);  // error text next to a field
        assert(contrastRatio(c.windowText, c.accentSoft) >= 7.0);      // label of the selected tab or row
        // selected rows (lists, trees, the second line of the mode lists): readable on the selection and on hover
        assert(contrastRatio(c.selectedText, c.accentSoft) >= 7.0 && contrastRatio(c.selectedText, c.hover) >= 7.0);
        assert(contrastRatio(c.selectedMutedText, c.accentSoft) >= 4.5 && contrastRatio(c.selectedMutedText, c.hover) >= 4.5);
        // the old clash: the primary button text on the soft selection is NOT readable, so it must not be used there
        assert(contrastRatio(c.highlightedText, c.accentSoft) < 4.5);
        assert(contrastRatio(c.border, c.window) >= 1.15 && !(c.border == c.window));
        // the style sheet colours derive from any base palette (the System theme)
        assert(minTextContrast(finishColors(c)) >= 4.5);
    }
    // System: any platform palette (light, dark, odd ones) still gives readable selected rows
    const Rgb samples[][2] = {{{0xFF, 0xFF, 0xFF}, {0, 0, 0}}, {{0x30, 0x30, 0x30}, {0xEE, 0xEE, 0xEE}},
                              {{0xEF, 0xEB, 0xE7}, {0x4A, 0x40, 0x30}}, {{0x20, 0x30, 0x60}, {0xA0, 0xA0, 0xFF}}};
    for (const auto &s : samples) {
        ThemeColors c = lightColors();
        c.window = c.base = c.button = s[0];
        c.windowText = c.text = c.buttonText = s[1];
        c = finishColors(c);
        assert(contrastRatio(c.selectedText, c.accentSoft) >= 7.0 && contrastRatio(c.selectedText, c.hover) >= 7.0);
        assert(contrastRatio(c.selectedMutedText, c.accentSoft) >= 4.5 && contrastRatio(c.selectedMutedText, c.hover) >= 4.5);
    }
    ThemeColors none;
    assert(!themeColors(Theme::System, &none));
    // the themes are really different: light is light, dark is dark
    assert(relativeLuminance(lightColors().window) > 0.8 && relativeLuminance(darkColors().window) < 0.05);
    assert(!(lightColors().window == darkColors().window));
}

// Accessibility of the palette: status colours, control borders, focus ring, and colour-vision deficiencies.
static void testAccessibility() {
    for (Theme t : {Theme::Light, Theme::Dark}) {
        ThemeColors c;
        assert(themeColors(t, &c));
        // (a) text of every status colour on window, card and input: AA
        for (Rgb status : {c.danger, c.warning, c.success, c.info})
            for (Rgb bg : {c.window, c.surface, c.base}) assert(contrastRatio(status, bg) >= 4.5);
        // (b) non-text parts that carry meaning: 3:1 against every neighbour (window, input, card, hover, selection)
        for (Rgb bg : {c.window, c.base, c.surface, c.hover, c.accentSoft}) assert(contrastRatio(c.controlBorder, bg) >= 3.0);
        for (Rgb bg : {c.window, c.base, c.surface}) assert(contrastRatio(c.focusRing, bg) >= 3.0);
        // the knob of a switch (highlightedText) on its on-track (highlight)
        assert(contrastRatio(c.highlightedText, c.highlight) >= 3.0);
        // (e) the cues that must stay apart with colour-vision deficiencies keep their luminance contrast
        // against the background (the shape or the text carries the rest): status text, accent, control border
        for (Cvd kind : {Cvd::Protanopia, Cvd::Deuteranopia, Cvd::Tritanopia}) {
            const Rgb win = simulateCvd(c.window, kind), surf = simulateCvd(c.surface, kind);
            for (Rgb status : {c.danger, c.warning, c.success, c.info, c.highlight, c.focusRing, c.controlBorder}) {
                assert(contrastRatio(simulateCvd(status, kind), win) >= 3.0);
                assert(contrastRatio(simulateCvd(status, kind), surf) >= 3.0);
            }
            // red and green (error versus success) differ in lightness, not only in hue
            assert(std::fabs(relativeLuminance(simulateCvd(c.danger, kind)) - relativeLuminance(simulateCvd(c.success, kind))) > 0.02 ||
                   kind == Cvd::Tritanopia);
            // the selection stays visible against the window: text on it is still readable
            assert(contrastRatio(simulateCvd(c.selectedText, kind), simulateCvd(c.accentSoft, kind)) >= 7.0);
        }
    }
    // the simulation itself: grey stays grey, white and black stay put
    for (Cvd kind : {Cvd::Protanopia, Cvd::Deuteranopia, Cvd::Tritanopia}) {
        const Rgb w = simulateCvd({255, 255, 255}, kind), k = simulateCvd({0, 0, 0}, kind);
        assert(w.r >= 253 && w.g >= 253 && w.b >= 253 && k.r == 0 && k.g == 0 && k.b == 0);
    }
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
    testAccessibility();
    testTokens();
    return 0;
}
