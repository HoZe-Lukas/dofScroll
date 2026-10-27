// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Relative sizes for the style sheet (accessibility, issue #104): the style sheet is written in design
// pixels, and this turns every length in it into em, so that padding, heights, radii and indicator sizes
// follow the font size of the system (a style sheet length in em is relative to the font of the widget).
// Border widths stay in pixels: a hairline must not grow to a bar. Toolkit-free, so it is tested without Qt.
//
//   pxToEm(qss, 13)   // "padding: 5px 18px" -> "padding: 0.385em 1.385em" at a font of 13 px
#pragma once
#include <charconv>
#include <regex>
#include <string>

namespace spn::ui {

// The reference font in design pixels: at this font size the em lengths come out as the original pixels.
constexpr double kQssReferenceFontPx = 14.0;

// The em length with a '.' as decimal point, whatever LC_NUMERIC is: a locale with a decimal comma
// (it_IT, de_DE, ...) would make snprintf write "0,571em", which Qt cannot parse - the rules then
// lose their padding and the interface collapses (see docs/troubleshooting.md). std::to_chars does
// not use the locale.
inline std::string emText(double px, double basePx) {
    char buf[64];
    const auto res = std::to_chars(buf, buf + sizeof buf, px / basePx, std::chars_format::fixed, 3);
    std::string s = res.ec == std::errc() ? std::string(buf, res.ptr) : std::string("0");
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    if (s.empty() || s == "-0") s = "0";
    return s + "em";
}

// True for the declarations whose pixel values are line widths: border, border-top, border-width, ...
// (border-radius and its corner variants are lengths like any other).
inline bool isBorderWidth(const std::string &prop) {
    return prop.rfind("border", 0) == 0 && prop.find("radius") == std::string::npos;
}

inline std::string pxToEm(const std::string &qss, double basePx = kQssReferenceFontPx) {
    if (basePx <= 0) return qss;
    static const std::regex decl(R"(([A-Za-z-]+)\s*:\s*([^;{}]*))");
    static const std::regex px(R"((-?[0-9]*\.?[0-9]+)px)");
    std::string out;
    auto last = qss.cbegin();
    for (std::sregex_iterator it(qss.begin(), qss.end(), decl), end; it != end; ++it) {
        const std::smatch &m = *it;
        out.append(last, m[2].first);  // everything up to the value
        const std::string prop = m[1].str(), value = m[2].str();
        if (isBorderWidth(prop) || value.find("url(") != std::string::npos) {
            out += value;
        } else {
            std::string conv;
            auto vlast = value.cbegin();
            for (std::sregex_iterator pit(value.begin(), value.end(), px), pend; pit != pend; ++pit) {
                conv.append(vlast, (*pit)[0].first);
                // The pixel value is C-format; std::stod would stop at the '.' in a decimal-comma locale
                // and turn "6.5px" into "6px". std::from_chars does not use the locale.
                const std::string tok = (*pit)[1].str();
                double v = 0.0;
                const auto fc = std::from_chars(tok.data(), tok.data() + tok.size(), v, std::chars_format::general);
                if (fc.ec != std::errc()) v = 0.0;
                conv += emText(v, basePx);
                vlast = (*pit)[0].second;
            }
            conv.append(vlast, value.cend());
            out += conv;
        }
        last = m[2].second;
    }
    out.append(last, qss.cend());
    return out;
}

}  // namespace spn::ui
