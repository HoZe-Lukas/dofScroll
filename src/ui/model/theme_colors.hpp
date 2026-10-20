// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Colours of the light and the dark theme, without any toolkit: the semantic roles of a palette
// and the contrast between them. The Qt layer turns a ThemeColors into a QPalette. The values are
// chosen so that text keeps at least the WCAG AA contrast (4.5:1; normal text on the window and
// on the input background even 7:1) in both themes, and the tests check that, so a change of a
// colour cannot silently make the interface hard to read. No information is carried by colour
// alone: the selected tab and the enabled state also differ in shape and text.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ui/model/ui_settings.hpp"

namespace spn::ui {

struct Rgb {
    uint8_t r = 0, g = 0, b = 0;
    bool operator==(const Rgb &o) const { return r == o.r && g == o.g && b == o.b; }
};

struct ThemeColors {
    Rgb window;           // window and page background
    Rgb windowText;       // text on the window
    Rgb base;             // background of inputs and lists
    Rgb text;             // text on base
    Rgb button;           // buttons and tabs
    Rgb buttonText;       // text on buttons and tabs
    Rgb highlight;        // selection
    Rgb highlightedText;  // text on the selection
    Rgb link;             // links
    Rgb disabledText;     // text of disabled controls (on window)
    Rgb placeholderText;  // hints and placeholders (on window and on base)
};

// Linear luminance of an sRGB colour (WCAG 2.x).
inline double relativeLuminance(Rgb c) {
    auto lin = [](uint8_t v) {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(c.r) + 0.7152 * lin(c.g) + 0.0722 * lin(c.b);
}

// Contrast ratio of two colours, 1.0 (equal) to 21.0 (black on white).
inline double contrastRatio(Rgb a, Rgb b) {
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

inline ThemeColors lightColors() {
    ThemeColors c;
    c.window = {0xF4, 0xF5, 0xF7};
    c.windowText = {0x1B, 0x1F, 0x24};
    c.base = {0xFF, 0xFF, 0xFF};
    c.text = {0x1B, 0x1F, 0x24};
    c.button = {0xE6, 0xE8, 0xEC};
    c.buttonText = {0x1B, 0x1F, 0x24};
    c.highlight = {0x1F, 0x5F, 0xBF};
    c.highlightedText = {0xFF, 0xFF, 0xFF};
    c.link = {0x0B, 0x57, 0xB8};
    c.disabledText = {0x62, 0x69, 0x72};
    c.placeholderText = {0x5A, 0x61, 0x6B};
    return c;
}

inline ThemeColors darkColors() {
    ThemeColors c;
    c.window = {0x20, 0x23, 0x28};
    c.windowText = {0xE8, 0xEA, 0xED};
    c.base = {0x15, 0x17, 0x1B};
    c.text = {0xE8, 0xEA, 0xED};
    c.button = {0x32, 0x37, 0x3F};
    c.buttonText = {0xE8, 0xEA, 0xED};
    c.highlight = {0x4C, 0x8D, 0xF6};
    c.highlightedText = {0x0B, 0x11, 0x1A};
    c.link = {0x7C, 0xB0, 0xFF};
    c.disabledText = {0x8A, 0x91, 0x9B};
    c.placeholderText = {0x9A, 0xA1, 0xAB};
    return c;
}

// The colours of a forced theme; System has none (the platform palette stays).
inline bool themeColors(Theme t, ThemeColors *out) {
    if (t == Theme::Light) {
        *out = lightColors();
        return true;
    }
    if (t == Theme::Dark) {
        *out = darkColors();
        return true;
    }
    return false;
}

// Smallest contrast of the text pairs of a theme (what the tests and the self-test check).
inline double minTextContrast(const ThemeColors &c) {
    return std::min({contrastRatio(c.windowText, c.window), contrastRatio(c.text, c.base),
                     contrastRatio(c.buttonText, c.button), contrastRatio(c.highlightedText, c.highlight),
                     contrastRatio(c.link, c.window), contrastRatio(c.disabledText, c.window),
                     contrastRatio(c.placeholderText, c.window), contrastRatio(c.placeholderText, c.base)});
}

}  // namespace spn::ui
