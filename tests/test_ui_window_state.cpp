// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Window state in the per-user UI settings (src/ui/model/ui_settings.hpp): parsing, bounds, screens.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "testutil.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/ui_settings.hpp"

using namespace spn::ui;

static void testParse() {
    WindowGeometry g = parseWindowGeometry("10,20,960,560");
    assert(g.valid && g.x == 10 && g.y == 20 && g.w == 960 && g.h == 560);
    g = parseWindowGeometry(" -1920 , -5 , 800 , 600 ");  // negative origin (a screen left of the primary)
    assert(g.valid && g.x == -1920 && g.y == -5 && g.w == 800 && g.h == 600);
    assert(parseWindowGeometry("+3,4,5,6").valid);
    for (const char *bad : {"", "1,2,3", "1,2,3,4,5", "a,b,c,d", "1,2,0,5", "1,2,5,0", "1,2,-5,5", "1,2,20001,5",
                            "1,2,5,20001", "100001,0,5,5", "0,-100001,5,5", "1,2,3,", ",1,2,3", "1,,2,3", "1.5,2,3,4",
                            "1,2,3,99999999999999999999", "1 2,3,4,5", "-,2,3,4", "0x10,2,3,4"})
        assert(!parseWindowGeometry(bad).valid);
    // round trip through the text form
    WindowGeometry r;
    r.valid = true;
    r.x = -30;
    r.y = 7;
    r.w = 1000;
    r.h = 700;
    assert(parseWindowGeometry(windowGeometryText(r)) == r);
}

static void testPageId() {
    assert(validPageId("overview") && validPageId("button-modes") && validPageId("a_b9"));
    assert(!validPageId("") && !validPageId("Overview") && !validPageId("a b") && !validPageId("a/b") && !validPageId("a=b") &&
           !validPageId(std::string(kMaxPageIdLength + 1, 'a')));
    assert(validPageId(std::string(kMaxPageIdLength, 'a')));
}

static void testSettingsRoundTrip() {
    UiSettings s;
    s.language = Language::German;
    s.theme = Theme::Dark;
    s.window = parseWindowGeometry("12,34,900,600");
    s.page = "axes";
    const std::string text = serializeUiSettings(s);
    assert(text.find("window = 12,34,900,600\n") != std::string::npos && text.find("page = axes\n") != std::string::npos);
    assert(parseUiSettings(text) == s);
    // nothing stored -> no lines, and an old file (language and theme only) still loads
    UiSettings plain;
    const std::string plainText = serializeUiSettings(plain);
    assert(plainText.find("window") == std::string::npos && plainText.find("page") == std::string::npos);
    assert(parseUiSettings("language = en\ntheme = dark\n").window.valid == false);
    // bad values are dropped, the other settings stay
    const UiSettings bad = parseUiSettings("theme = light\nwindow = 1,2,3\npage = ../etc\n");
    assert(bad.theme == Theme::Light && !bad.window.valid && bad.page.empty());
    // the last value wins, also an invalid one
    assert(!parseUiSettings("window = 1,2,300,300\nwindow = nonsense\n").window.valid);
    assert(parseUiSettings("page = a\npage = b\n").page == "b");
    assert(!(parseUiSettings("page = a\n") == parseUiSettings("page = b\n")));
}

static void testUpdateKeepsWindowState() {
    const std::string dir = tu::makeTempDir("dofscroll-ui-window");
    const std::string path = dir + "/ui.conf";
    std::string err;
    assert(updateUiSettings(path, [](UiSettings &s) { s.theme = Theme::Dark; s.language = Language::German; }, &err));
    assert(updateUiSettings(path, [](UiSettings &s) { s.window = parseWindowGeometry("1,2,800,500"); s.page = "modes"; }, &err));
    // saving the theme or the language keeps the window state, and the other way round
    assert(updateUiSettings(path, [](UiSettings &s) { s.theme = Theme::Light; }, &err));
    UiSettings s = loadUiSettings(path);
    assert(s.theme == Theme::Light && s.language == Language::German && s.window.valid && s.window.w == 800 && s.page == "modes");
    assert(updateUiSettings(path, [](UiSettings &s2) { s2.page = "overview"; }, &err));
    s = loadUiSettings(path);
    assert(s.theme == Theme::Light && s.window.x == 1 && s.page == "overview");
    std::filesystem::remove_all(dir);
}

static void testMinimumAndScreens() {
    using namespace tokens;
    WindowGeometry g = parseWindowGeometry("0,0,100,100");
    g = withMinimumSize(g, kMinWindowWidth, kMinWindowHeight);
    assert(g.w == kMinWindowWidth && g.h == kMinWindowHeight && g.valid);
    g = withMinimumSize(parseWindowGeometry("0,0,2000,900"), kMinWindowWidth, kMinWindowHeight);
    assert(g.w == 2000 && g.h == 900);

    const std::vector<ScreenRect> one = {{0, 0, 1920, 1080}};
    const std::vector<ScreenRect> two = {{0, 0, 1920, 1080}, {1920, 0, 1280, 1024}};
    assert(geometryVisibleOn(parseWindowGeometry("100,100,960,560"), one));
    assert(geometryVisibleOn(parseWindowGeometry("-800,100,960,560"), one));  // 160 px of it on the screen
    assert(!geometryVisibleOn(parseWindowGeometry("-950,100,960,560"), one));  // only 10 px on the screen
    assert(!geometryVisibleOn(parseWindowGeometry("5000,100,960,560"), one));  // far right
    assert(!geometryVisibleOn(parseWindowGeometry("100,5000,960,560"), one));  // far below
    assert(!geometryVisibleOn(parseWindowGeometry("100,-9000,960,560"), one));
    assert(geometryVisibleOn(parseWindowGeometry("2500,100,960,560"), two));  // on the second screen
    assert(!geometryVisibleOn(parseWindowGeometry("2500,100,960,560"), one));  // the second screen is gone
    assert(!geometryVisibleOn(WindowGeometry(), one));                         // nothing stored
    assert(geometryVisibleOn(parseWindowGeometry("5000,100,960,560"), {}));    // no screen information: keep
}

int main() {
    testParse();
    testPageId();
    testSettingsRoundTrip();
    testUpdateKeepsWindowState();
    testMinimumAndScreens();
    puts("test_ui_window_state: ok");
    return 0;
}
