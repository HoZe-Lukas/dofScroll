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
    Rgb placeholderText;  // hints, help text and placeholders: the muted text colour (on window, base, surface)
    // Derived by finishColors(): the surfaces and states of the application style sheet.
    Rgb surface;          // cards and group boxes
    Rgb border;           // thin borders of cards and inputs
    Rgb hover;            // hovered buttons and rows
    Rgb pressed;          // pressed buttons
    Rgb accentSoft;       // selected tab, row and menu entry (text stays windowText)
    Rgb accentHover;      // hovered primary button
    Rgb accentPressed;    // pressed primary button
};

// Colour between a and b: t = 0 is a, t = 1 is b.
inline Rgb mixColors(Rgb a, Rgb b, double t) {
    auto m = [t](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * t)); };
    return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b)};
}

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

// The accent colour is c.highlight (primary buttons, selection, focus ring); everything else of
// the style sheet is derived from the base colours, so the platform palette ("System") works too.
inline ThemeColors finishColors(ThemeColors c) {
    const bool dark = relativeLuminance(c.window) < 0.2;
    c.surface = dark ? mixColors(c.window, c.windowText, 0.05) : c.base;
    c.border = mixColors(c.window, c.windowText, dark ? 0.24 : 0.22);
    c.hover = mixColors(c.button, c.windowText, 0.07);
    c.pressed = mixColors(c.button, c.windowText, 0.14);
    c.accentSoft = mixColors(c.window, c.highlight, dark ? 0.22 : 0.16);
    const Rgb towards = dark ? Rgb{255, 255, 255} : Rgb{0, 0, 0};
    c.accentHover = mixColors(c.highlight, towards, 0.12);
    c.accentPressed = mixColors(c.highlight, towards, 0.24);
    return c;
}

inline ThemeColors lightColors() {
    ThemeColors c;
    c.window = {0xF3, 0xF4, 0xF6};
    c.windowText = {0x1B, 0x1F, 0x24};
    c.base = {0xFF, 0xFF, 0xFF};
    c.text = {0x1B, 0x1F, 0x24};
    c.button = {0xFF, 0xFF, 0xFF};
    c.buttonText = {0x1B, 0x1F, 0x24};
    c.highlight = {0x1F, 0x5F, 0xBF};
    c.highlightedText = {0xFF, 0xFF, 0xFF};
    c.link = {0x0B, 0x57, 0xB8};
    c.disabledText = {0x62, 0x69, 0x72};
    c.placeholderText = {0x5A, 0x61, 0x6B};
    return finishColors(c);
}

inline ThemeColors darkColors() {
    ThemeColors c;
    c.window = {0x1C, 0x1F, 0x24};
    c.windowText = {0xE8, 0xEA, 0xED};
    c.base = {0x14, 0x16, 0x1A};
    c.text = {0xE8, 0xEA, 0xED};
    c.button = {0x2C, 0x31, 0x38};
    c.buttonText = {0xE8, 0xEA, 0xED};
    c.highlight = {0x4C, 0x8D, 0xF6};
    c.highlightedText = {0x0B, 0x11, 0x1A};
    c.link = {0x7C, 0xB0, 0xFF};
    c.disabledText = {0x8A, 0x91, 0x9B};
    c.placeholderText = {0xA3, 0xAA, 0xB4};
    return finishColors(c);
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
                     contrastRatio(c.placeholderText, c.window), contrastRatio(c.placeholderText, c.base),
                     contrastRatio(c.placeholderText, c.surface), contrastRatio(c.windowText, c.surface),
                     contrastRatio(c.windowText, c.accentSoft), contrastRatio(c.placeholderText, c.accentSoft),
                     contrastRatio(c.windowText, c.hover), contrastRatio(c.highlightedText, c.accentHover),
                     contrastRatio(c.highlightedText, c.accentPressed)});
}

// Smallest contrast of the non-text parts that carry meaning: the accent (focus ring, selected
// tab underline, checked box) against the window and the surface (WCAG 1.4.11: 3:1).
inline double minAccentContrast(const ThemeColors &c) {
    return std::min(contrastRatio(c.highlight, c.window), contrastRatio(c.highlight, c.surface));
}

}  // namespace spn::ui
