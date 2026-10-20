// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// pxToEm: lengths of the style sheet become em (relative to the font), border widths and urls stay.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "ui/model/qss_scale.hpp"

using namespace spn::ui;

int main() {
    assert(emText(13, 13) == "1em" && emText(0, 13) == "0em" && emText(6.5, 13) == "0.5em");
    assert(emText(-7, 14) == "-0.5em" && emText(34, 13) == "2.615em");

    // paddings, heights and radii scale; the border width does not
    const std::string in = "QPushButton { border: 1px solid #fff; border-radius: 8px; padding: 5px 18px; min-height: 22px; }";
    const std::string out = pxToEm(in, 13);
    assert(out.find("border: 1px solid #fff") != std::string::npos);
    assert(out.find("border-radius: 0.615em") != std::string::npos);
    assert(out.find("padding: 0.385em 1.385em") != std::string::npos);
    assert(out.find("min-height: 1.692em") != std::string::npos);
    assert(out.find("px") != std::string::npos && out.rfind("px") == out.find("1px") + 1);  // only the border width is left

    // border sides and widths stay, corner radii scale, negative margins keep their sign
    assert(pxToEm("a { border-bottom: 3px solid red; border-top-left-radius: 13px; margin: -13px 0; }", 13) ==
           "a { border-bottom: 3px solid red; border-top-left-radius: 1em; margin: -1em 0; }");
    // a focus rule that swaps a 1px border for a 2px one and pads one pixel less
    assert(pxToEm("b:focus { border: 2px solid #00f; padding: 4px 17px; }", 13) == "b:focus { border: 2px solid #00f; padding: 0.308em 1.308em; }");
    // urls and selectors are left alone
    const std::string url = "c::indicator { image: url(\"/tmp/a-12px.svg\"); width: 40px; height: 22px; }";
    assert(pxToEm(url, 13) == "c::indicator { image: url(\"/tmp/a-12px.svg\"); width: 3.077em; height: 1.692em; }");
    assert(pxToEm("QComboBox::drop-down { width: 30px; }", 13) == "QComboBox::drop-down { width: 2.308em; }");
    // idempotent on a sheet without pixel lengths, and a bad base changes nothing
    assert(pxToEm("a { padding: 1em; color: #fff; }", 13) == "a { padding: 1em; color: #fff; }");
    assert(pxToEm(in, 0) == in);
    // at the reference font nothing gets bigger than the design
    assert(pxToEm("a { min-height: 28px; }", kQssReferenceFontPx) == "a { min-height: 2em; }");
    std::puts("test_ui_qss_scale: ok");
    return 0;
}
