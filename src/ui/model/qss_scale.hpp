// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Relative sizes for the style sheet (accessibility, issue #104): the style sheet is written in design
// pixels, and this turns every length in it into em, so that padding, heights, radii and indicator sizes
// follow the font size of the system (a style sheet length in em is relative to the font of the widget).
// Border widths stay in pixels: a hairline must not grow to a bar. Toolkit-free, so it is tested without Qt.
//
//   pxToEm(qss, 13)   // "padding: 5px 18px" -> "padding: 0.385em 1.385em" at a font of 13 px
#pragma once
#include <cstdio>
#include <regex>
#include <string>

namespace spn::ui {

// The reference font in design pixels: at this font size the em lengths come out as the original pixels.
constexpr double kQssReferenceFontPx = 14.0;

inline std::string emText(double px, double basePx) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.3f", px / basePx);
    std::string s = buf;
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
                conv += emText(std::stod((*pit)[1].str()), basePx);
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
