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
    Rgb selectedText;     // text of a selected, hovered-selected or unfocused-selected row (on accentSoft and hover)
    Rgb selectedMutedText;  // the muted second line of a selected row: a tint of selectedText, still AA on accentSoft
    Rgb danger;           // error text next to a field and error borders (always together with a text)
    Rgb warning;          // warning text (always together with the word "Warning" or a symbol)
    Rgb success;          // success text (always together with a text or a symbol)
    Rgb info;             // neutral notes
    Rgb controlBorder;    // borders and marks of controls (inputs, boxes, switch off, slider groove, scroll handle): 3:1
    Rgb focusRing;        // the 2 px focus indicator: the accent, strengthened to 3:1 against window, surface and base
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

// Text colour with at least `minRatio` against every background in bg: starts from `start`, then
// moves towards black or white (whichever has more contrast with the backgrounds) until it fits.
inline Rgb readableOn(Rgb start, const Rgb *bg, int n, double minRatio) {
    auto worst = [&](Rgb c) {
        double w = 100.0;
        for (int i = 0; i < n; ++i) w = std::min(w, contrastRatio(c, bg[i]));
        return w;
    };
    if (worst(start) >= minRatio) return start;
    const Rgb black{0, 0, 0}, white{255, 255, 255};
    const Rgb towards = worst(black) >= worst(white) ? black : white;
    for (int i = 1; i <= 20; ++i) {
        const Rgb c = mixColors(start, towards, i / 20.0);
        if (worst(c) >= minRatio) return c;
    }
    return towards;
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
    // Selected rows sit on accentSoft (hovered selected rows too): AAA for the name, AA for the muted line.
    const Rgb selBg[] = {c.accentSoft, c.hover};
    c.selectedText = readableOn(c.text, selBg, 2, 7.0);
    // the muted line: between the selected text and the background, as far as AA allows
    c.selectedMutedText = c.selectedText;
    for (int i = 10; i >= 1; --i) {
        const Rgb m = mixColors(c.selectedText, c.accentSoft, i * 0.04);
        if (contrastRatio(m, c.accentSoft) >= 5.0 && contrastRatio(m, c.hover) >= 4.5) {
            c.selectedMutedText = m;
            break;
        }
    }
    c.danger = dark ? Rgb{0xFF, 0x8A, 0x80} : Rgb{0xB3, 0x26, 0x1E};
    // Status colours are never the only cue (the texts carry a word or a symbol) and differ in lightness
    // from each other, so that they stay apart for colour-vision deficiencies. Orange/blue instead of
    // red/green pairs for the pair that matters most (success vs. danger differ also in luminance).
    c.warning = dark ? Rgb{0xFF, 0xC1, 0x4D} : Rgb{0x8A, 0x4B, 0x00};
    c.success = dark ? Rgb{0x7F, 0xD9, 0xA0} : Rgb{0x0F, 0x6B, 0x3A};
    c.info = dark ? Rgb{0x8F, 0xBF, 0xFF} : Rgb{0x0B, 0x57, 0xB8};
    {
        const Rgb bgs[] = {c.window, c.base, c.surface, c.hover, c.accentSoft};
        auto okAll = [&](Rgb x, double r) {
            for (const Rgb &b : bgs)
                if (contrastRatio(x, b) < r) return false;
            return true;
        };
        c.controlBorder = c.windowText;
        for (int i = 1; i <= 20; ++i) {
            const Rgb m = mixColors(c.window, c.windowText, i * 0.05);
            if (okAll(m, 3.0)) {
                c.controlBorder = m;
                break;
            }
        }
        c.focusRing = c.highlight;
        const Rgb towardsText = c.windowText;
        for (int i = 0; i <= 20 && !okAll(c.focusRing, 3.0); ++i) c.focusRing = mixColors(c.highlight, towardsText, i * 0.05);
    }
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
                     contrastRatio(c.danger, c.window), contrastRatio(c.danger, c.surface), contrastRatio(c.danger, c.base),
                     contrastRatio(c.warning, c.window), contrastRatio(c.warning, c.surface), contrastRatio(c.warning, c.base),
                     contrastRatio(c.success, c.window), contrastRatio(c.success, c.surface), contrastRatio(c.success, c.base),
                     contrastRatio(c.info, c.window), contrastRatio(c.info, c.surface), contrastRatio(c.info, c.base),
                     contrastRatio(c.text, c.base), contrastRatio(c.text, c.surface), contrastRatio(c.link, c.surface),
                     contrastRatio(c.windowText, c.hover), contrastRatio(c.selectedText, c.accentSoft),
                     contrastRatio(c.selectedText, c.hover), contrastRatio(c.selectedMutedText, c.accentSoft),
                     contrastRatio(c.selectedMutedText, c.hover), contrastRatio(c.highlightedText, c.accentHover),
                     contrastRatio(c.highlightedText, c.accentPressed)});
}

// Smallest contrast of the non-text parts that carry meaning: the accent (focus ring, selected
// tab underline, checked box) against the window and the surface (WCAG 1.4.11: 3:1).
inline double minAccentContrast(const ThemeColors &c) {
    return std::min({contrastRatio(c.highlight, c.window), contrastRatio(c.highlight, c.surface),
                     contrastRatio(c.focusRing, c.window), contrastRatio(c.focusRing, c.surface),
                     contrastRatio(c.focusRing, c.base), contrastRatio(c.controlBorder, c.window),
                     contrastRatio(c.controlBorder, c.base), contrastRatio(c.controlBorder, c.surface)});
}

// Colour-vision deficiency simulation (Machado et al. 2009, severity 1.0; matrices act on linear RGB).
enum class Cvd { Protanopia, Deuteranopia, Tritanopia };
inline Rgb simulateCvd(Rgb c, Cvd kind) {
    static const double m[3][3][3] = {
        {{0.152286, 1.052583, -0.204868}, {0.114503, 0.786281, 0.099216}, {-0.003882, -0.048116, 1.051998}},
        {{0.367322, 0.860646, -0.227968}, {0.280085, 0.672501, 0.047413}, {-0.011820, 0.042940, 0.968881}},
        {{1.255528, -0.076749, -0.178779}, {-0.078411, 0.930809, 0.147602}, {0.004733, 0.691367, 0.303900}}};
    auto lin = [](uint8_t v) {
        const double s = v / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    auto enc = [](double v) {
        v = std::min(1.0, std::max(0.0, v));
        const double s = v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
        return static_cast<uint8_t>(std::lround(s * 255.0));
    };
    const double in[3] = {lin(c.r), lin(c.g), lin(c.b)};
    const auto &k = m[static_cast<int>(kind)];
    return {enc(k[0][0] * in[0] + k[0][1] * in[1] + k[0][2] * in[2]), enc(k[1][0] * in[0] + k[1][1] * in[1] + k[1][2] * in[2]),
            enc(k[2][0] * in[0] + k[2][1] * in[1] + k[2][2] * in[2])};
}

}  // namespace spn::ui
