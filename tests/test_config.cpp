// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tests for src/config.cpp - plain assert tests, no framework.
// Build/run:
//   g++ -std=c++17 -Wall -Wextra -Isrc tests/test_config.cpp src/config.cpp -o test_config && ./test_config
#undef NDEBUG
#ifndef _WIN32
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <chrono>
#include <filesystem>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <fstream>
#include <string>
#include <iostream>

#include "config.hpp"
#include "testutil.hpp"

using namespace spn;

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

static bool mixEq(const Mix &a, const Mix &b) {
    if (a.terms.size() != b.terms.size()) return false;
    for (size_t i = 0; i < a.terms.size(); i++)
        if (a.terms[i].axis != b.terms[i].axis || !near(a.terms[i].weight, b.terms[i].weight))
            return false;
    return true;
}

static bool chanEq(const ChannelCfg &a, const ChannelCfg &b) {
    if (!mixEq(a.mix, b.mix) || a.sensitivity != b.sensitivity || a.deadzone != b.deadzone ||
        a.sign != b.sign || !near(a.curve_exp, b.curve_exp) || !near(a.smooth_ms, b.smooth_ms) ||
        a.curve_points.size() != b.curve_points.size())
        return false;
    for (size_t i = 0; i < a.curve_points.size(); i++)
        if (!near(a.curve_points[i].first, b.curve_points[i].first) ||
            !near(a.curve_points[i].second, b.curve_points[i].second))
            return false;
    return true;
}

static bool settingsEq(const Settings &a, const Settings &b) {
    return chanEq(a.scroll_v, b.scroll_v) && chanEq(a.scroll_h, b.scroll_h) &&
           chanEq(a.zoom, b.zoom) && a.zoom_lateral == b.zoom_lateral &&
           a.zoom_enter_ms == b.zoom_enter_ms && near(a.zoom_exit_ratio, b.zoom_exit_ratio) &&
           a.backend == b.backend && a.tick_hz == b.tick_hz && a.nav_button == b.nav_button &&
           a.nav_button_enabled == b.nav_button_enabled && a.click_button == b.click_button &&
           a.click_button_enabled == b.click_button_enabled && a.enabled == b.enabled && a.mode == b.mode;
}

static std::string tmpPath(const char *name) {
    const char *d = std::getenv("TMPDIR");
    return std::string(d && *d ? d : ".") + "/" + name + "." + std::to_string(tu::pid());
}

// Restores the variables that select the configuration base directory (all OSes).
struct EnvGuard {
    struct Var { const char *name; bool had; std::string value; };
    std::vector<Var> vars;
    EnvGuard() {
        for (const char *n : {"XDG_CONFIG_HOME", "HOME", "APPDATA"}) {
            const char *v = std::getenv(n);
            vars.push_back({n, v != nullptr, v ? v : ""});
        }
    }
    ~EnvGuard() {
        for (const Var &v : vars) {
            if (v.had) tu::setEnv(v.name, v.value);
            else tu::unsetEnv(v.name);
        }
    }
};

static void writeFile(const std::string &path, const std::string &content) {
    std::ofstream f(path, std::ios::binary);
    f << content;
}

static void testMix() {
    Mix m;
    std::string err;
    assert(parseMix("z + rx", &m, &err));
    assert(m.terms.size() == 2 && m.terms[0].axis == AX_Z && m.terms[1].axis == AX_RX);
    assert(near(m.terms[1].weight, 1.0));
    assert(parseMix("x - rz", &m, &err));
    assert(m.terms[1].axis == AX_RZ && near(m.terms[1].weight, -1.0));
    assert(parseMix("0.5*rx + z", &m, &err));
    assert(near(m.terms[0].weight, 0.5) && m.terms[0].axis == AX_RX);
    assert(parseMix("-ry", &m, &err));
    assert(m.terms.size() == 1 && near(m.terms[0].weight, -1.0) && m.terms[0].axis == AX_RY);
    assert(parseMix("  X+Y  ", &m, &err) && m.terms.size() == 2);  // case/whitespace
    assert(parseMix("", &m, &err) && m.terms.empty());
    assert(parseMix("2 * z - 0.25*rx", &m, &err) && near(m.terms[1].weight, -0.25));

    for (const char *bad : {"q", "z +", "z rx", "2*", "2 z", "+", "z + + rx", "z * 2", "rw"}) {
        err.clear();
        Mix before;
        before.terms = {{AX_X, 1.0}};
        Mix tmp = before;
        assert(!parseMix(bad, &tmp, &err));
        assert(!err.empty());
    }

    for (const char *good : {"z + rx", "x - rz", "0.5*rx + z", "-ry", "-2*x - 0.1*y + rz", "ry", ""}) {
        Mix a, b;
        assert(parseMix(good, &a, &err));
        std::string s = mixToString(a);
        assert(parseMix(s, &b, &err));
        assert(mixEq(a, b));
    }
    assert(mixToString(Mix{{{AX_Z, 1}, {AX_RX, 1}}}) == "z + rx");
    assert(mixToString(Mix{{{AX_X, 1}, {AX_RZ, -1}}}) == "x - rz");
}

static void testDefaultsAndApplyKey() {
    Settings s;
    assert(mixToString(s.scroll_v.mix) == "z + rx" && s.scroll_v.sensitivity == 5 &&
           s.scroll_v.deadzone == 30);
    assert(mixToString(s.scroll_h.mix) == "x - rz" && s.scroll_h.sensitivity == 3 &&
           s.scroll_h.deadzone == 20);
    assert(mixToString(s.zoom.mix) == "ry" && s.zoom.sensitivity == 5 && s.zoom.deadzone == 40);
    assert(s.zoom_lateral == (std::vector<Axis>{AX_X, AX_Z, AX_RX, AX_RZ}));
    assert(s.tick_hz == 250 && s.backend == "auto");

    std::string err;
    assert(applyKey(s, "scroll_v_sens", "300", &err) && s.scroll_v.sensitivity == 255);
    assert(applyKey(s, "SCROLL_V_SENS", "-4", &err) && s.scroll_v.sensitivity == 0);
    assert(applyKey(s, "SCROLL_V_DEADZONE", "-3", &err) && s.scroll_v.deadzone == 0);
    assert(applyKey(s, "ZOOM_SIGN", "-1", &err) && s.zoom.sign == -1);
    Settings before = s;
    assert(!applyKey(s, "ZOOM_SIGN", "2", &err) && settingsEq(s, before));
    assert(!applyKey(s, "ZOOM_SIGN", "abc", &err));
    assert(!applyKey(s, "NOPE", "1", &err) && !err.empty());
    assert(!applyKey(s, "SCROLL_V_MIX", "q", &err) && settingsEq(s, before));
    assert(applyKey(s, "ZOOM_CURVE", "0.01", &err) && near(s.zoom.curve_exp, 0.1));
    assert(applyKey(s, "ZOOM_SMOOTH_MS", "9999", &err) && near(s.zoom.smooth_ms, 500));
    assert(applyKey(s, "TICK_HZ", "5", &err) && s.tick_hz == 30);
    assert(applyKey(s, "TICK_HZ", "5000", &err) && s.tick_hz == 1000);
    assert(applyKey(s, "BACKEND", "UInput", &err) && s.backend == "uinput");
    assert(!applyKey(s, "BACKEND", "wayland", &err) && s.backend == "uinput");
    assert(applyKey(s, "ZOOM_LATERAL", "x, rz  y", &err));
    assert(s.zoom_lateral == (std::vector<Axis>{AX_X, AX_RZ, AX_Y}));
    assert(!applyKey(s, "ZOOM_LATERAL", "x foo", &err));
    assert(applyKey(s, "NAV_BUTTON_ENABLED", "1", &err) && s.nav_button_enabled);
    assert(s.mode == "normal" && applyKey(s, "MODE", "Timeline", &err) && s.mode == "timeline");
    assert(!applyKey(s, "MODE", "Bad Mode!", &err) && s.mode == "timeline");
    assert(applyKey(s, "MODE", "normal", &err));
    assert(s.enabled && applyKey(s, "ENABLED", "0", &err) && !s.enabled);
    assert(!applyKey(s, "ENABLED", "maybe", &err) && !s.enabled);

    // CURVE as a point list
    assert(applyKey(s, "SCROLL_H_CURVE", "0:0 50:0.1 200:1 350:3", &err));
    assert(s.scroll_h.curve_points.size() == 4 && near(s.scroll_h.curve_points[3].second, 3));
    assert(!applyKey(s, "SCROLL_H_CURVE", "0:0 200:1 100:2", &err));  // not ascending
    assert(s.scroll_h.curve_points.size() == 4);
    assert(!applyKey(s, "SCROLL_H_CURVE", "0:0 abc:1", &err));
    assert(applyKey(s, "SCROLL_H_CURVE", "1.5", &err));
    assert(s.scroll_h.curve_points.empty() && near(s.scroll_h.curve_exp, 1.5));

    // legacy aliases
    Settings l;
    assert(applyKey(l, "SCROLL_SENSITIVITY_Y", "7", &err) && l.scroll_v.sensitivity == 7);
    assert(applyKey(l, "SCROLL_SENSITIVITY_X", "8", &err) && l.scroll_h.sensitivity == 8);
    assert(applyKey(l, "ZOOM_SENSITIVITY", "9", &err) && l.zoom.sensitivity == 9);
    assert(applyKey(l, "SCROLL_DEADZONE_Y", "11", &err) && l.scroll_v.deadzone == 11);
    assert(applyKey(l, "SCROLL_DEADZONE_X", "12", &err) && l.scroll_h.deadzone == 12);
    assert(applyKey(l, "ZOOM_DEADZONE", "13", &err) && l.zoom.deadzone == 13);
    assert(applyKey(l, "INVERT_Y", "-1", &err) && l.scroll_v.sign == -1);
    assert(applyKey(l, "INVERT_X", "1", &err) && l.scroll_h.sign == 1);
    assert(applyKey(l, "ZOOM_INVERT", "1", &err) && l.zoom.sign == -1);
    assert(applyKey(l, "ZOOM_INVERT", "0", &err) && l.zoom.sign == 1);
    assert(applyKey(l, "RESPONSE_CURVE", "2", &err));
    assert(near(l.scroll_v.curve_exp, 2) && near(l.scroll_h.curve_exp, 2) && near(l.zoom.curve_exp, 2));
}

static void testLegacyFile() {
    std::string p = tmpPath("legacy.conf");
    writeFile(p,
              "# Independent Speed settings (0 to 255)\n"
              "SCROLL_SENSITIVITY_Y = 5\nSCROLL_SENSITIVITY_X = 3\nZOOM_SENSITIVITY = 2\n\n"
              "SCROLL_DEADZONE_Y = 30\nSCROLL_DEADZONE_X = 20\nZOOM_DEADZONE = 20\n\n"
              "INVERT_Y = 1\nINVERT_X = -1\nZOOM_INVERT = 1\n\n"
              "RESPONSE_CURVE = 1\n\nNAV_BUTTON = 0\nNAV_BUTTON_ENABLED = 0\n"
              "CLICK_BUTTON = 1\nCLICK_BUTTON_ENABLED = 0\n");
    ProfileSet ps;
    std::vector<std::string> w;
    assert(loadProfiles(p, &ps, &w));
    assert(w.empty());
    Settings s = ps.resolve("", "");
    assert(s.scroll_v.sensitivity == 5 && s.scroll_h.sensitivity == 3 && s.zoom.sensitivity == 2);
    assert(s.scroll_v.deadzone == 30 && s.scroll_h.deadzone == 20 && s.zoom.deadzone == 20);
    assert(s.scroll_v.sign == 1 && s.scroll_h.sign == -1 && s.zoom.sign == -1);
    assert(near(s.scroll_v.curve_exp, 1.0) && s.click_button == 1 && !s.click_button_enabled);
    assert(!s.nav_button_enabled && s.nav_button == 0);
    std::remove(p.c_str());
}

static void testResolve() {
    std::string p = tmpPath("resolve.conf");
    writeFile(p,
              "SCROLL_V_SENS = 10\n"
              "[device:SpaceMouse Pro]\nSCROLL_V_SENS = 20\nZOOM_SENS = 21\n"
              "[app:FireFox]\nZOOM_SENS = 30\n"
              "[app:term]\r\nSCROLL_V_SENS = 40\r\n");
    ProfileSet ps;
    std::vector<std::string> w;
    assert(loadProfiles(p, &ps, &w) && w.empty());
    assert(ps.sections.size() == 4);
    assert(ps.resolve("", "").scroll_v.sensitivity == 10);
    assert(ps.resolve("3Dconnexion SPACEMOUSE PRO Wireless", "").scroll_v.sensitivity == 20);
    assert(ps.resolve("spacemouse pro", "").zoom.sensitivity == 21);
    assert(ps.resolve("", "Navigator.firefox").zoom.sensitivity == 30);
    assert(ps.resolve("", "Navigator.firefox").scroll_v.sensitivity == 10);
    // device then program: app wins
    Settings s = ps.resolve("SpaceMouse Pro", "firefox");
    assert(s.zoom.sensitivity == 30 && s.scroll_v.sensitivity == 20);
    assert(ps.resolve("other", "XTERM").scroll_v.sensitivity == 40);  // CRLF section
    assert(ps.resolve("other", "chrome").scroll_v.sensitivity == 10);
    std::remove(p.c_str());
}

static void testSectionRoundtrip() {
    ProfileSet ps;
    Settings g;
    g.scroll_v.sensitivity = 12;
    ps.setSectionFromSettings(0, g);
    size_t a = ps.findOrAdd(Section::APP, "firefox");
    assert(a == 1 && ps.findOrAdd(Section::APP, "FIREFOX") == a);
    assert(ps.findOrAdd(Section::DEVICE, "firefox") != a);
    Settings s = ps.sectionSettings(a);
    assert(settingsEq(s, g));  // empty section = global
    s.zoom.sensitivity = 77;
    s.scroll_h.curve_points = {{0, 0}, {100, 1}};
    ps.setSectionFromSettings(a, s);
    auto d = diffKeys(g, s);
    assert(ps.sections[a].kv == d && d.size() == 2);
    assert(settingsEq(ps.sectionSettings(a), s));
    assert(diffKeys(s, s).empty());
    // a global change propagates to the section, the override stays
    Settings g2 = g;
    g2.scroll_v.sensitivity = 50;
    ps.setSectionFromSettings(0, g2);
    Settings r = ps.resolve("", "firefox");
    assert(r.scroll_v.sensitivity == 50 && r.zoom.sensitivity == 77);
    // allKeys applied to fresh Settings reproduces it
    Settings fresh;
    for (auto &kv : allKeys(s)) {
        std::string err;
        assert(applyKey(fresh, kv.first, kv.second, &err));
    }
    assert(settingsEq(fresh, s));
    ps.remove(0);
    assert(ps.sections.size() == 3);
    ps.remove(a);
    assert(ps.sections.size() == 2);
}

static void testSaveLoad() {
    std::string p = tmpPath("save.conf");
    ProfileSet ps;
    Settings g;
    g.scroll_v.mix = Mix{{{AX_Y, 0.5}, {AX_RX, -1}}};
    g.zoom.curve_points = {{0, 0}, {50, 0.1}, {350, 3}};
    g.zoom_lateral = {AX_X, AX_Y};
    g.backend = "x11";
    g.tick_hz = 125;
    g.click_button_enabled = true;
    g.enabled = false;
    g.mode = "timeline";
    g.scroll_h.smooth_ms = 20.5;
    ps.setSectionFromSettings(0, g);
    Settings app = g;
    app.zoom.sensitivity = 99;
    app.zoom.sign = -1;
    ps.setSectionFromSettings(ps.findOrAdd(Section::APP, "fire fox"), app);
    Settings dev = g;
    dev.scroll_v.sensitivity = 1;
    ps.setSectionFromSettings(ps.findOrAdd(Section::DEVICE, "SpaceMouse Pro"), dev);

    std::string err;
    assert(saveProfiles(p, ps, &err));
    ProfileSet back;
    std::vector<std::string> w;
    assert(loadProfiles(p, &back, &w) && w.empty());
    assert(back.sections.size() == 3);
    assert(settingsEq(back.sectionSettings(0), g));
    assert(settingsEq(back.resolve("", "fire fox"), app));
    assert(settingsEq(back.resolve("spacemouse pro", ""), dev));
    // header contains a comment note
    std::ifstream f(p);
    std::string first;
    std::getline(f, first);
    assert(!first.empty() && first[0] == '#');
    // the temp file must not be left over
    std::string tmp = p + ".tmp." + std::to_string(tu::pid());
    assert(!std::ifstream(tmp));
    std::remove(p.c_str());

    // not writable
#ifdef __linux__
    assert(!saveProfiles("/proc/nonexistent/x.conf", ps, &err) && !err.empty());
#else
    // a regular file as the parent "directory" cannot be created through
    writeFile(p, "x");
    assert(!saveProfiles(p + "/x/y.conf", ps, &err) && !err.empty());
    std::remove(p.c_str());
#endif
}

static void testBrokenAndMisc() {
    // missing file -> defaults, true
    ProfileSet ps;
    std::vector<std::string> w;
    assert(loadProfiles(tmpPath("does-not-exist.conf"), &ps, &w) && w.empty());
    assert(ps.sections.size() == 1 && settingsEq(ps.resolve("", ""), Settings()));

    std::string p = tmpPath("broken.conf");
    writeFile(p,
              "; Kommentar\n"
              "SCROLL_V_SENS = 7   # Zeilenende\n"
              "ZOOM_SENS = # leer\n"             // empty value -> error -> warning
              "kein gleichheitszeichen\n"
              "UNKNOWN_KEY = 3\n"
              "SCROLL_H_SIGN = 5\n"
              "scroll_h_mix =   x   +  y  \n"
              "[weird]\nTICK_HZ = 100\n"         // unknown section: ignored
              "[app:ok]\nZOOM_SENS = 3\n"
              "[app:\nZOOM_SENS = 4\n"
              "\n[global]\nTICK_HZ = 60\n");
    ProfileSet b;
    w.clear();
    assert(loadProfiles(p, &b, &w));
    assert(w.size() == 6);  // ZOOM_SENS empty, without '=', UNKNOWN, SIGN=5, [weird], [app:
    Settings s = b.resolve("", "");
    assert(s.scroll_v.sensitivity == 7);
    assert(mixToString(s.scroll_h.mix) == "x + y");
    assert(s.scroll_h.sign == 1);
    assert(s.tick_hz == 60);
    assert(b.resolve("", "ok").zoom.sensitivity == 3);
    assert(b.sections.size() == 2);
    std::remove(p.c_str());

    // defaultConfigPath
#ifdef __linux__
    setenv("XDG_CONFIG_HOME", "/xdg", 1);
    assert(defaultConfigPath() == "/xdg/dofscroll/dofscroll.conf");
    unsetenv("XDG_CONFIG_HOME");
    setenv("HOME", "/home/x", 1);
    assert(defaultConfigPath() == "/home/x/.config/dofscroll/dofscroll.conf");
#else
    {
        EnvGuard guard;
        const std::string root = tu::makeTempDir("spn_cfgbase");
        const std::string base = tu::useConfigBase(root);
        assert(defaultConfigPath() == base + "/dofscroll/dofscroll.conf");
        assert(legacyConfigPath() == base + "/spnav-scroll.conf");
        tu::removeAll(root);
    }
#endif
}

// Regression S2 (docs/security.md): upper bounds for curve and mix weights.
static void testUpperBounds() {
    Settings s;
    std::string err;
    assert(applyKey(s, "SCROLL_V_CURVE", "60", &err));
    assert(s.scroll_v.curve_exp == 10.0);
    assert(applyKey(s, "RESPONSE_CURVE", "1e9", &err));
    assert(s.zoom.curve_exp == 10.0);
    Settings t;
    assert(!applyKey(t, "SCROLL_V_CURVE", "0:0 350:1e6", &err));
    assert(t.scroll_v.curve_points.empty());
    assert(!applyKey(t, "SCROLL_V_MIX", "1e6*z", &err));
    assert(applyKey(t, "SCROLL_V_MIX", "100*z + rx", &err));
}

// Review fixes: symlink, mode, RESPONSE_CURVE, ']', BOM, directory, device override.
static void testReviewFixes() {
    std::string d = tu::makeTempDir("spn_cfg"), err;

    // 1+2: symlink stays, content lands in the target, mode stays (POSIX; Windows: plain file)
    std::string real = d + "/real.conf", link = d + "/link.conf";
    { std::ofstream(real) << "SCROLL_V_SENS = 1\n"; }
#ifndef _WIN32
    assert(chmod(real.c_str(), 0640) == 0);
    assert(symlink("real.conf", link.c_str()) == 0);
#else
    link = real;
#endif
    ProfileSet ps;
    assert(loadProfiles(link, &ps, nullptr));
    Settings g = ps.sectionSettings(0);
    g.scroll_v.sensitivity = 9;
    ps.setSectionFromSettings(0, g);
    assert(saveProfiles(link, ps, &err));
#ifndef _WIN32
    struct stat lst, rst;
    assert(lstat(link.c_str(), &lst) == 0 && S_ISLNK(lst.st_mode));
    assert(stat(real.c_str(), &rst) == 0 && (rst.st_mode & 07777) == 0640);
#endif
    ProfileSet back;
    assert(loadProfiles(real, &back, nullptr));
    assert(back.sectionSettings(0).scroll_v.sensitivity == 9);
    // new file: 0600
    std::string fresh = d + "/sub/new.conf";
    assert(saveProfiles(fresh, ps, &err));
#ifndef _WIN32
    assert(stat(fresh.c_str(), &rst) == 0 && (rst.st_mode & 07777) == 0600);
#else
    assert(tu::exists(fresh));
#endif
    // create_directories error is reported
    assert(!saveProfiles(real + "/x/y.conf", ps, &err) && !err.empty());

    // 3: RESPONSE_CURVE clears points
    Settings s;
    assert(applyKey(s, "SCROLL_V_CURVE", "0:0 100:1 300:3", &err));
    assert(!s.scroll_v.curve_points.empty());
    assert(applyKey(s, "RESPONSE_CURVE", "2", &err));
    assert(s.scroll_v.curve_points.empty() && near(s.scroll_v.curve_exp, 2.0));

    // 4: ']' / newline rejected, whitespace trimmed
    ProfileSet p4;
    assert(p4.findOrAdd(Section::APP, "a]b") == ProfileSet::npos);
    assert(p4.findOrAdd(Section::APP, "a\nb") == ProfileSet::npos);
    assert(p4.findOrAdd(Section::APP, "   ") == ProfileSet::npos);
    size_t t = p4.findOrAdd(Section::APP, "  fox  ");
    assert(t == 1 && p4.sections[1].pattern == "fox" && p4.findOrAdd(Section::APP, "FOX") == 1);
    p4.sections[1].pattern = "bad]";  // injected by hand
    assert(!saveProfiles(d + "/bad.conf", p4, &err) && !err.empty());
    assert(!tu::exists(d + "/bad.conf"));

    // 6: BOM
    std::string bom = d + "/bom.conf";
    { std::ofstream(bom) << "\xEF\xBB\xBF[app:ff]\nZOOM_SENS = 12\n"; }
    ProfileSet pb;
    assert(loadProfiles(bom, &pb, nullptr));
    assert(pb.sections.size() == 2 && pb.sections[1].pattern == "ff");
    assert(pb.resolve("", "ff").zoom.sensitivity == 12);

    // 7: directory
    std::vector<std::string> w;
    assert(!loadProfiles(d, &pb, &w) && !w.empty());

    // 5: app profile restores the global value against a device override
    ProfileSet p5;
    Settings g5 = p5.sectionSettings(0);
    g5.zoom.sensitivity = 5;
    p5.setSectionFromSettings(0, g5);
    size_t dv = p5.findOrAdd(Section::DEVICE, "mouse");
    Settings sd = g5;
    sd.zoom.sensitivity = 8;
    p5.setSectionFromSettings(dv, sd);
    size_t ap = p5.findOrAdd(Section::APP, "ff");
    p5.setSectionFromSettings(ap, g5);  // app wants global 5
    assert(p5.resolve("mouse", "ff").zoom.sensitivity == 5);
    assert(p5.resolve("mouse", "other").zoom.sensitivity == 8);
    assert(p5.resolve("", "ff").zoom.sensitivity == 5);
    // round trip through a file
    assert(saveProfiles(d + "/p5.conf", p5, &err));
    ProfileSet r5;
    assert(loadProfiles(d + "/p5.conf", &r5, nullptr));
    assert(r5.resolve("mouse", "ff").zoom.sensitivity == 5);
    // device section stays diffable against global
    assert(p5.sections[dv].kv.size() == 1);

    tu::removeAll(d);
}

static std::string mkTmpDir() { return tu::makeTempDir("spn_audit"); }

static size_t countTmpFiles(const std::string &dir) {
    size_t n = 0;
    for (const auto &e : std::filesystem::directory_iterator(dir))
        if (e.path().filename().string().find(".tmp.") != std::string::npos) n++;
    return n;
}

static long peakRssKb() {
    std::ifstream f("/proc/self/status");
    std::string l;
    while (std::getline(f, l))
        if (l.compare(0, 6, "VmHWM:") == 0) return std::atol(l.c_str() + 6);
    return 0;
}

// Audit fixes: limits, index, fd-based loading, safe saving, paths, duplicates.
static void testAuditFixes() {
    const std::string d = mkTmpDir();
    std::string err;
    std::vector<std::string> w;

    // 1: 10k sections: fast, cap applies, keys in skipped sections ignored
    {
        std::string c;
        for (int i = 0; i < 10000; i++)
            c += "[app:prog" + std::to_string(i) + "]\nZOOM_SENS = " + std::to_string(i % 200) + "\n";
        c += "[app:PROG3]\nZOOM_DEADZONE = 77\n";  // an existing section stays reachable even at the cap
        std::string p = d + "/many.conf";
        writeFile(p, c);
        auto t0 = std::chrono::steady_clock::now();
        ProfileSet ps;
        w.clear();
        assert(loadProfiles(p, &ps, &w));
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        assert(sec < 2.0);
        assert(ps.sections.size() == kMaxSections);
        assert(!w.empty());
        assert(ps.find(Section::APP, "PROG0") == 1);
        assert(ps.find(Section::APP, "prog254") == kMaxSections - 1);
        assert(ps.find(Section::APP, "prog255") == ProfileSet::npos);
        assert(ps.resolve("", "prog3").zoom.deadzone == 77);
        assert(ps.resolve("", "prog3").zoom.sensitivity == 3);
        // findOrAdd/remove keep the index consistent
        size_t a = ps.findOrAdd(Section::DEVICE, "  NewDev ");
        assert(a == ps.sections.size() - 1 && ps.findOrAdd(Section::DEVICE, "newdev") == a);
        assert(ps.findOrAdd(Section::APP, "newdev") != a);
        ps.remove(1);
        assert(ps.find(Section::APP, "prog0") == ProfileSet::npos);
        assert(ps.find(Section::APP, "prog1") == 1);
        ProfileSet cp = ps;  // copy keeps the index
        assert(cp.find(Section::APP, "prog1") == 1 && cp.findOrAdd(Section::DEVICE, "NEWDEV") == a - 1);
    }

    // 1: keys per section limited (there are only ~25 distinct keys; the cap therefore does
    // not apply via duplicates - here only: warning flood is limited)
    {
        std::string c;
        for (int i = 0; i < 5000; i++) c += "BOGUS" + std::to_string(i) + " = 1\n";
        writeFile(d + "/warn.conf", c);
        w.clear();
        ProfileSet ps;
        assert(loadProfiles(d + "/warn.conf", &ps, &w));
        assert(w.size() <= 202);
    }

    // 2: file size and line length
    {
        long before = peakRssKb();
        std::string big(2u << 20, 'x');
        big += "\n";
        writeFile(d + "/big.conf", big);
        ProfileSet ps;
        w.clear();
        assert(!loadProfiles(d + "/big.conf", &ps, &w));
        assert(w.size() == 1 && w[0].find("too large") != std::string::npos);
        assert(peakRssKb() - before < 64 * 1024);

        std::string c = "ZOOM_SENS = 9\nZOOM_MIX = ry # " + std::string(kMaxLineBytes + 10, 'a') +
                        "\nSCROLL_V_SENS = 4\n[app:x]\nZOOM_SENS = 1\n";
        writeFile(d + "/long.conf", c);
        w.clear();
        assert(loadProfiles(d + "/long.conf", &ps, &w));
        assert(w.size() == 1 && w[0].find("too long") != std::string::npos);
        assert(ps.resolve("", "").zoom.sensitivity == 9 && ps.resolve("", "").scroll_v.sensitivity == 4);
        // long line that was a section header: keys after it must not go to the old section
        c = "[app:ok]\nZOOM_SENS = 3\n[app:" + std::string(kMaxLineBytes + 5, 'b') + "]\nZOOM_SENS = 99\n";
        writeFile(d + "/long2.conf", c);
        assert(loadProfiles(d + "/long2.conf", &ps, nullptr));
        assert(ps.resolve("", "ok").zoom.sensitivity == 3);
        // exactly at the limit: ok
        std::string edge = "# " + std::string(kMaxLineBytes - 2, 'c') + "\nZOOM_SENS = 8\n";
        writeFile(d + "/edge.conf", edge);
        w.clear();
        assert(loadProfiles(d + "/edge.conf", &ps, &w) && w.empty() && ps.resolve("", "").zoom.sensitivity == 8);
    }

    // 2: mix/curve/lateral caps
    {
        Mix m;
        std::string e12, e13;
        for (int i = 0; i < 13; i++) {
            if (i) { e13 += " + "; if (i < 12) e12 += " + "; }
            e13 += "x";
            if (i < 12) e12 += "x";
        }
        assert(parseMix(e12, &m, &err) && m.terms.size() == kMaxMixTerms);
        assert(!parseMix(e13, &m, &err));
        std::string c64, c65;
        for (int i = 1; i <= 65; i++) {
            std::string pt = std::to_string(i) + ":" + std::to_string(i % 5) + " ";
            if (i <= 64) c64 += pt;
            c65 += pt;
        }
        Settings s;
        assert(applyKey(s, "ZOOM_CURVE", c64, &err) && s.zoom.curve_points.size() == kMaxCurvePoints);
        Settings t;
        assert(!applyKey(t, "ZOOM_CURVE", c65, &err) && t.zoom.curve_points.empty());
        assert(applyKey(s, "ZOOM_LATERAL", "x y z rx ry rz", &err) && s.zoom_lateral.size() == 6);
        assert(!applyKey(s, "ZOOM_LATERAL", "x y z rx ry rz x y z x y z x y z x y z x y z x y z x y z", &err));
        assert(s.zoom_lateral.size() == 6);
    }

    // 9: ZOOM_EXIT_RATIO upper bound
    {
        Settings s;
        assert(applyKey(s, "ZOOM_EXIT_RATIO", "1e9", &err) && s.zoom_exit_ratio == 100.0);
    }

    // 3: FIFO / directory rejected (a FIFO without a writer must not block)
    {
        ProfileSet ps;
#ifndef _WIN32
        std::string fifo = d + "/fifo.conf";
        assert(mkfifo(fifo.c_str(), 0600) == 0);
        w.clear();
        assert(!loadProfiles(fifo, &ps, &w));
        assert(w.size() == 1 && w[0].find("not a regular file") != std::string::npos);
#endif
        w.clear();
        assert(!loadProfiles(d, &ps, &w) && w.size() == 1);
#ifndef _WIN32
        // symlinked config stays readable, dangling = defaults
        writeFile(d + "/t.conf", "ZOOM_SENS = 6\n");
        assert(symlink("t.conf", (d + "/l.conf").c_str()) == 0);
        assert(loadProfiles(d + "/l.conf", &ps, nullptr) && ps.resolve("", "").zoom.sensitivity == 6);
        assert(symlink("nowhere", (d + "/dangling.conf").c_str()) == 0);
        assert(loadProfiles(d + "/dangling.conf", &ps, nullptr) && ps.sections.size() == 1);
#endif
    }

#ifndef _WIN32

    // 5: symlink loop -> error, link stays a link; chain < 16 ok
    {
        std::string a = d + "/loopa.conf", b = d + "/loopb.conf";
        assert(symlink("loopb.conf", a.c_str()) == 0 && symlink("loopa.conf", b.c_str()) == 0);
        ProfileSet ps;
        err.clear();
        assert(!saveProfiles(a, ps, &err));
        assert(err.find(std::strerror(ELOOP)) != std::string::npos);
        struct stat st;
        assert(lstat(a.c_str(), &st) == 0 && S_ISLNK(st.st_mode));
        assert(countTmpFiles(d) == 0);
        // chain of 10 links
        writeFile(d + "/chain_target.conf", "");
        std::string prev = "chain_target.conf";
        for (int i = 0; i < 10; i++) {
            std::string n = "chain" + std::to_string(i) + ".conf";
            assert(symlink(prev.c_str(), (d + "/" + n).c_str()) == 0);
            prev = n;
        }
        assert(saveProfiles(d + "/" + prev, ps, &err));
        assert(lstat((d + "/" + prev).c_str(), &st) == 0 && S_ISLNK(st.st_mode));
        assert(lstat((d + "/chain_target.conf").c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0);
    }

    // 5: mode: setuid/setgid/sticky dropped
    {
        std::string p = d + "/mode.conf";
        writeFile(p, "");
        assert(chmod(p.c_str(), 06755) == 0);
        ProfileSet ps;
        assert(saveProfiles(p, ps, &err));
        struct stat st;
        assert(stat(p.c_str(), &st) == 0);
        assert((st.st_mode & 07777) == 0755);
    }

#endif  // _WIN32 (symlink loop, chain, setuid mode)

    // 6: kv validation on save
    {
        std::string p = d + "/kv.conf";
        writeFile(p, "ZOOM_SENS = 2\n");
        auto tryKv = [&](const KV &kv) {
            ProfileSet ps;
            size_t i = ps.findOrAdd(Section::APP, "x");
            ps.sections[i].kv.push_back(kv);
            return saveProfiles(p, ps, &err);
        };
        assert(!tryKv({"ZOOM_SENS", "5\n[global]"}));
        assert(!tryKv({"ZOOM_SENS", "5\r"}));
        assert(!tryKv({"", "5"}));
        assert(!tryKv({"[evil", "5"}));
        assert(!tryKv({"#ZOOM_SENS", "5"}));
        assert(!tryKv({";ZOOM_SENS", "5"}));
        assert(!tryKv({"ZOOM\nSENS", "5"}));
        assert(!tryKv({"NOT_A_KEY", "5"}));
        assert(!tryKv({"ZOOM_SENS", "abc"}));
        std::ifstream f(p);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        assert(content == "ZOOM_SENS = 2\n");  // nothing written
        assert(countTmpFiles(d) == 0);
        assert(tryKv({"ZOOM_SENS", "5"}));
    }

    // 7: relative XDG ignored, empty HOME -> passwd
#ifdef __linux__
    {
        const char *oldHome = std::getenv("HOME");
        std::string saved = oldHome ? oldHome : "";
        setenv("XDG_CONFIG_HOME", "relative/dir", 1);
        setenv("HOME", "/home/y", 1);
        assert(defaultConfigPath() == "/home/y/.config/dofscroll/dofscroll.conf");
        setenv("XDG_CONFIG_HOME", "/abs", 1);
        assert(defaultConfigPath() == "/abs/dofscroll/dofscroll.conf");
        unsetenv("XDG_CONFIG_HOME");
        setenv("HOME", "", 1);
        std::string pth = defaultConfigPath();
        assert(!pth.empty() && pth.size() > std::string("/.config/dofscroll.conf").size());
        unsetenv("HOME");
        assert(defaultConfigPath() == pth);
        if (oldHome) setenv("HOME", saved.c_str(), 1);
    }
#endif
    {
        // a new directory is created private (0700 on POSIX)
        std::string p = d + "/new/sub/x.conf";
        ProfileSet ps;
        assert(saveProfiles(p, ps, &err));
#ifndef _WIN32
        struct stat st;
        assert(stat((d + "/new").c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
        assert(stat((d + "/new/sub").c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
#else
        assert(tu::exists(d + "/new/sub/x.conf"));
#endif
    }

    // 8: merge duplicates
    {
        writeFile(d + "/dup.conf",
                  "ZOOM_SENS = 1\nSCROLL_V_SENS = 2\nzoom_sens = 7\n[app:a]\nZOOM_SENS = 3\nZOOM_SENS = 4\n");
        ProfileSet ps;
        assert(loadProfiles(d + "/dup.conf", &ps, nullptr));
        assert(ps.sections[0].kv.size() == 2);
        assert(ps.sections[0].kv[0] == KV("SCROLL_V_SENS", "2") && ps.sections[0].kv[1] == KV("ZOOM_SENS", "7"));
        assert(ps.sections[1].kv.size() == 1 && ps.sections[1].kv[0].second == "4");
    }

    // 4: error paths on save - write/fsync errors remove the temp file
    {
#ifndef _WIN32
        struct rlimit old, lim;
        getrlimit(RLIMIT_FSIZE, &old);
        void (*oldh)(int) = signal(SIGXFSZ, SIG_IGN);
        std::string p = d + "/fsz.conf";
        writeFile(p, "ZOOM_SENS = 2\n");
        lim = old;
        lim.rlim_cur = 10;
        assert(setrlimit(RLIMIT_FSIZE, &lim) == 0);
        ProfileSet ps;
        bool ok = saveProfiles(p, ps, &err);
        setrlimit(RLIMIT_FSIZE, &old);
        signal(SIGXFSZ, oldh);
        assert(!ok && err.find("write failed") != std::string::npos);
        assert(countTmpFiles(d) == 0);
        std::ifstream f(p);
        std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        assert(content == "ZOOM_SENS = 2\n");
#else
        ProfileSet ps;
#endif
        // target is a directory -> error, no leftovers
        assert(std::filesystem::create_directory(d + "/isdir.conf"));
        assert(!saveProfiles(d + "/isdir.conf", ps, &err));
        assert(countTmpFiles(d) == 0);
#ifndef _WIN32
        // directory not writable (only if not root): error, no leftovers
        if (geteuid() != 0) {
            std::string ro = d + "/ro";
            assert(mkdir(ro.c_str(), 0500) == 0);
            assert(!saveProfiles(ro + "/x.conf", ps, &err));
            chmod(ro.c_str(), 0700);
        }
#endif
    }

    // 4: a stale temp file (old pid name or similar) does not disturb saving
    {
        std::string p = d + "/stale.conf";
        writeFile(p + ".tmp." + std::to_string(tu::pid()), "stale");
        writeFile(p + ".tmp.0000000000000000", "stale");
        ProfileSet ps;
        assert(saveProfiles(p, ps, &err));
        assert(saveProfiles(p, ps, &err));
        assert(countTmpFiles(d) >= 2);  // foreign files stay untouched
        ProfileSet back;
        assert(loadProfiles(p, &back, nullptr));
    }

    tu::removeAll(d);
}

#ifdef __linux__
static std::string slurp(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static void testMigration() {
    char tpl[] = "/tmp/spn_mig_XXXXXX";
    assert(mkdtemp(tpl));
    const std::string d = tpl;
    const char *oldXdg = std::getenv("XDG_CONFIG_HOME");
    const std::string savedXdg = oldXdg ? oldXdg : "";
    setenv("XDG_CONFIG_HOME", d.c_str(), 1);
    const std::string legacy = d + "/spnav-scroll.conf";  // oldest location (old project name)
    const std::string prevdir = d + "/spnav-scroll";
    const std::string prev = prevdir + "/spnav-scroll.conf";  // previous location
    const std::string nd = d + "/dofscroll";
    const std::string np = nd + "/dofscroll.conf";
    std::string log;
    struct stat st;

    assert(legacyConfigPath() == legacy);
    assert(previousConfigPath() == prev);
    // nothing there: new path, migration false
    assert(defaultConfigPath() == np);
    assert(!migrateLegacyConfig(&log) && !log.empty());
    assert(!migrateLegacyConfig(nullptr));
    assert(::lstat(nd.c_str(), &st) != 0);  // no directory created

    // regular file
    writeFile(legacy, "[global]\nTICK_HZ = 60\n");
    assert(::chmod(legacy.c_str(), 0640) == 0);
    assert(defaultConfigPath() == legacy);  // Fallback
    assert(migrateLegacyConfig(&log) && !log.empty());
    assert(defaultConfigPath() == np);
    assert(slurp(np) == "[global]\nTICK_HZ = 60\n");
    assert(::lstat(np.c_str(), &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & 0777) == 0640);
    assert(::stat(nd.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    assert(slurp(legacy) == "[global]\nTICK_HZ = 60\n");  // untouched
    assert(countTmpFiles(nd) == 0);
    // idempotent
    log.clear();
    writeFile(legacy, "changed\n");
    assert(migrateLegacyConfig(&log));
    assert(slurp(np) == "[global]\nTICK_HZ = 60\n");
    assert(countTmpFiles(nd) == 0);
    // saveProfiles into a non-existent directory
    {
        std::string p2 = d + "/fresh/dofscroll/dofscroll.conf";
        ProfileSet ps;
        std::string err;
        assert(saveProfiles(p2, ps, &err));
        assert(::stat((d + "/fresh/dofscroll").c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
    }
    assert(system(("rm -rf " + nd + " " + legacy).c_str()) == 0);

    // previous location (spnav-scroll/spnav-scroll.conf) takes precedence over the oldest
    writeFile(legacy, "oldest\n");
    assert(::mkdir(prevdir.c_str(), 0700) == 0);
    writeFile(prev, "[global]\nTICK_HZ = 70\n");
    assert(defaultConfigPath() == prev);  // Fallback
    assert(migrateLegacyConfig(&log) && !log.empty());
    assert(defaultConfigPath() == np);
    assert(slurp(np) == "[global]\nTICK_HZ = 70\n");
    assert(slurp(prev) == "[global]\nTICK_HZ = 70\n");  // untouched
    assert(system(("rm -rf " + nd + " " + legacy + " " + prevdir).c_str()) == 0);

    // no overwriting
    writeFile(legacy, "legacy\n");
    assert(::mkdir(nd.c_str(), 0700) == 0);
    writeFile(np, "mine\n");
    assert(migrateLegacyConfig(&log));
    assert(slurp(np) == "mine\n");
    assert(defaultConfigPath() == np);
    assert(system(("rm -rf " + nd + " " + legacy).c_str()) == 0);

    // symlink (relative target)
    writeFile(d + "/dotfiles.conf", "[global]\nTICK_HZ = 90\n");
    assert(::symlink("dotfiles.conf", legacy.c_str()) == 0);
    assert(migrateLegacyConfig(&log));
    char buf[4096];
    ssize_t n = ::readlink(np.c_str(), buf, sizeof buf - 1);
    assert(n > 0);
    buf[n] = 0;
    assert(buf[0] == '/');
    char *r1 = ::realpath(np.c_str(), nullptr);
    char *r2 = ::realpath((d + "/dotfiles.conf").c_str(), nullptr);
    assert(r1 && r2 && std::string(r1) == r2);
    std::free(r1);
    std::free(r2);
    {
        ProfileSet ps;
        assert(loadProfiles(np, &ps, nullptr));
        assert(ps.sectionSettings(0).tick_hz == 90);
    }
    assert(::lstat(legacy.c_str(), &st) == 0 && S_ISLNK(st.st_mode));
    assert(migrateLegacyConfig(&log));  // idempotent
    assert(system(("rm -rf " + nd + " " + legacy).c_str()) == 0);

    // FIFO rejected
    assert(::mkfifo(legacy.c_str(), 0600) == 0);
    log.clear();
    assert(!migrateLegacyConfig(&log) && !log.empty());
    assert(::lstat(np.c_str(), &st) != 0);
    assert(system(("rm -rf " + nd + " " + legacy).c_str()) == 0);

    // too large
    {
        std::string big(kMaxConfigBytes + 1, '#');
        writeFile(legacy, big);
        log.clear();
        assert(!migrateLegacyConfig(&log) && !log.empty());
        assert(::lstat(np.c_str(), &st) != 0);
    }

    if (oldXdg) setenv("XDG_CONFIG_HOME", savedXdg.c_str(), 1);
    else unsetenv("XDG_CONFIG_HOME");
    assert(system(("rm -rf " + d).c_str()) == 0);
}
#endif  // __linux__ (legacy migration and XDG rules)

// ---- BUTTON_<n> keys, MODE key and mode-override precedence
static void testButtonsAndModes() {
    std::string err;
    Settings s;
    assert(applyKey(s, "BUTTON_1", "Click", &err) && s.buttons.at(1) == "click");
    assert(applyKey(s, "button_64", " KEY: Space ", &err) && s.buttons.at(64) == "key:space");
    assert(applyKey(s, "BUTTON_3", "nav", &err) && applyKey(s, "BUTTON_3", "none", &err) && s.buttons.at(3) == "none");
    for (const char *bad : {"BUTTON_0", "BUTTON_65", "BUTTON_", "BUTTON_x", "BUTTON_-1", "BUTTON_0001", "BUTTON_1000"})
        assert(!applyKey(s, bad, "click", &err));
    assert(!applyKey(s, "BUTTON_2", "key:nonsense", &err));
    assert(!applyKey(s, "BUTTON_2", "", &err));
    assert(s.buttons.count(2) == 0);

    // allKeys / diffKeys carry the buttons; diffKeys matches by name
    Settings base;
    Settings t = base;
    assert(diffKeys(base, t).empty());
    assert(applyKey(t, "BUTTON_5", "rightclick", &err));
    std::vector<KV> d = diffKeys(base, t);
    assert(d.size() == 1 && d[0].first == "BUTTON_5" && d[0].second == "rightclick");
    Settings withB = base;
    assert(applyKey(withB, "BUTTON_7", "click", &err));
    d = diffKeys(withB, base);  // binding present in base, absent in s -> explicit none
    assert(d.size() == 1 && d[0].first == "BUTTON_7" && d[0].second == "none");
    Settings fresh;
    for (const KV &p : allKeys(t)) assert(applyKey(fresh, p.first, p.second, &err));
    assert(fresh.buttons == t.buttons);

    // MODE in every section type
    ProfileSet ps;
    ps.sections[0].kv = {{"MODE", "timeline"}};
    size_t dv = ps.findOrAdd(Section::DEVICE, "pro");
    ps.sections[dv].kv = {{"MODE", "off"}};
    size_t ap = ps.findOrAdd(Section::APP, "resolve");
    ps.sections[ap].kv = {{"MODE", "normal"}};
    assert(ps.resolve("", "").mode == "timeline");
    assert(ps.resolve("SpaceMouse Pro", "").mode == "off");
    assert(ps.resolve("SpaceMouse Pro", "resolve").mode == "normal");
    assert(!applyKey(s, "MODE", "Bad Mode!", &err));

    // precedence: global < device < mode overrides < app
    ProfileSet q;
    q.sections[0].kv = {{"SCROLL_H_SENS", "1"}, {"SCROLL_V_SENS", "1"}, {"ZOOM_SENS", "1"}, {"TICK_HZ", "100"}};
    size_t qd = q.findOrAdd(Section::DEVICE, "pro");
    q.sections[qd].kv = {{"SCROLL_H_SENS", "2"}, {"SCROLL_V_SENS", "2"}, {"ZOOM_SENS", "2"}};
    size_t qa = q.findOrAdd(Section::APP, "app");
    q.sections[qa].kv = {{"SCROLL_H_SENS", "4"}, {"SCROLL_V_SENS", "4"}};
    const std::vector<KV> ov = {{"SCROLL_H_SENS", "3"}, {"SCROLL_V_SENS", "3"}, {"ZOOM_SENS", "3"}, {"TICK_HZ", "300"}};
    Settings r = q.resolve("pro", "app", &ov);
    assert(r.scroll_h.sensitivity == 4 && r.scroll_v.sensitivity == 4);  // app beats mode
    assert(r.zoom.sensitivity == 3);                                      // mode beats device
    assert(r.tick_hz == 300);                                             // mode beats global
    r = q.resolve("pro", "", &ov);
    assert(r.scroll_h.sensitivity == 3);
    r = q.resolve("other", "", &ov);
    assert(r.zoom.sensitivity == 3);                                      // mode beats global
    // nullptr and the two-argument overload agree
    assert(settingsEq(q.resolve("pro", "app", nullptr), q.resolve("pro", "app")));
    assert(q.resolve("pro", "app").zoom.sensitivity == 2);
    // invalid override entries are skipped, not fatal
    const std::vector<KV> badov = {{"NO_SUCH_KEY", "1"}, {"ZOOM_SENS", "9"}};
    assert(q.resolve("", "", &badov).zoom.sensitivity == 9);

    // buttons survive profile save + load, also in device/app sections
    ProfileSet bp;
    Settings g = bp.sectionSettings(0);
    assert(applyKey(g, "BUTTON_1", "click", &err));
    bp.setSectionFromSettings(0, g);
    size_t bd = bp.findOrAdd(Section::DEVICE, "pro");
    Settings dset = bp.sectionSettings(0);
    assert(applyKey(dset, "BUTTON_2", "key:space", &err));
    bp.setSectionFromSettings(bd, dset);
    const std::string path = tmpPath("buttons.conf");
    assert(saveProfiles(path, bp, &err));
    ProfileSet back;
    assert(loadProfiles(path, &back, nullptr));
    Settings res = back.resolve("pro", "");
    assert(res.buttons.size() == 2 && res.buttons.at(1) == "click" && res.buttons.at(2) == "key:space");
    assert(back.resolve("", "").buttons.size() == 1);
    std::remove(path.c_str());
}

int main() {
    testUpperBounds();
    testMix();
    testDefaultsAndApplyKey();
    testLegacyFile();
    testResolve();
    testSectionRoundtrip();
    testSaveLoad();
    testBrokenAndMisc();
    testReviewFixes();
    testAuditFixes();
#ifdef __linux__
    testMigration();
#endif
    testButtonsAndModes();
    std::cout << "test_config: all tests passed\n";
    return 0;
}
