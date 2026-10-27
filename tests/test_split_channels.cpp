// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Split push and tilt (docs/modes.md): mix splitting, per-part keys, the mode file keys V_PUSH_ACTION /
// V_TILT_ACTION / H_PUSH_ACTION / H_TILT_ACTION, and the runner evaluating the parts as independent
// sub-channels. Fake output, no threads. Compatibility of unsplit modes: tests/test_mode_compat.cpp.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "config.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "testutil.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

struct Rec : Output {
    std::vector<std::string> ev;
    int wv = 0, wh = 0;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { wv += n; ev.push_back("wv" + std::to_string(n)); }
    void wheelH(int n) override { wh += n; ev.push_back("wh" + std::to_string(n)); }
    void setKey(Key k, bool d) override { ev.push_back(std::string(d ? "+" : "-") + keyName(k)); }
    void tapKey(Key k) override { ev.push_back(std::string("tap:") + keyName(k)); }
    void mouseButton(int b, bool d) override { ev.push_back(std::string(d ? "+m" : "-m") + std::to_string(b)); }
    void releaseAll() override {}
    void flush() override {}
    int count(const std::string &e) const {
        int c = 0;
        for (const auto &x : ev) c += x == e;
        return c;
    }
    void clear() { ev.clear(); wv = wh = 0; }
};

static const double DT = 1.0 / 250.0;

static void run(ModeRunner &r, const Settings &s, const RawAxes &raw, double secs, uint32_t btn = 0) {
    for (int i = 0; i < static_cast<int>(secs / DT); ++i) r.tick(s, raw, btn, true, DT);
}

static RawAxes axes(int x, int y, int z, int rx, int ry, int rz) {
    RawAxes r{};
    r[AX_X] = x; r[AX_Y] = y; r[AX_Z] = z; r[AX_RX] = rx; r[AX_RY] = ry; r[AX_RZ] = rz;
    return r;
}
static RawAxes only(Axis a, int v) {
    RawAxes r{};
    r[a] = v;
    return r;
}

static ChannelAction act(const char *text) {
    ChannelAction a;
    std::string err;
    bool ok = parseChannelAction(text, &a, &err);
    CHECK(ok, "parse '%s': %s", text, err.c_str());
    return a;
}

static ModeDef mode(const char *id) {
    ModeDef m;
    m.id = m.label = id;
    return m;
}

// Settings with the dead zone off so that deflections map linearly: V rate = d / 4, H rate = d * 0.15.
static Settings plain() {
    Settings s;
    s.scroll_v.deadzone = s.scroll_h.deadzone = 0;
    s.scroll_v.smooth_ms = s.scroll_h.smooth_ms = 0.0;
    s.step_trigger = 20.0;
    return s;
}

static void setSplit(SplitChannel &sc, const char *push, const char *tilt) {
    if (push) { sc.push = act(push); sc.pushSet = true; }
    if (tilt) { sc.tilt = act(tilt); sc.tiltSet = true; }
}

// ---------------------------------------------------------------- mix splitting
static std::string mixText(const Mix &m) { return mixToString(m); }

static void testSplitMix() {
    auto split = [](const char *text, std::string *p, std::string *t) {
        Mix m, mp, mt;
        std::string err;
        CHECK(parseMix(text, &m, &err), "parse '%s'", text);
        splitMix(m, &mp, &mt);
        *p = mixText(mp);
        *t = mixText(mt);
    };
    std::string p, t;
    Settings d;  // defaults: V z + rx, H x - rz
    Mix mp, mt;
    splitMix(d.scroll_v.mix, &mp, &mt);
    CHECK(mixText(mp) == "z" && mixText(mt) == "rx", "V default: '%s' | '%s'", mixText(mp).c_str(), mixText(mt).c_str());
    splitMix(d.scroll_h.mix, &mp, &mt);
    CHECK(mixText(mp) == "x" && mixText(mt) == "-rz", "H default: '%s' | '%s'", mixText(mp).c_str(), mixText(mt).c_str());
    split("0.5*rx + z", &p, &t);
    CHECK(p == "z" && t == "0.5*rx", "weights kept: '%s' | '%s'", p.c_str(), t.c_str());
    split("x - rz", &p, &t);
    CHECK(p == "x" && t == "-rz", "signs kept");
    split("-2*z + x + 3*rz - rx", &p, &t);
    CHECK(p == "-2*z + x" && t == "3*rz - rx", "several terms per part: '%s' | '%s'", p.c_str(), t.c_str());
    split("z", &p, &t);
    CHECK(p == "z" && t.empty(), "no rotation terms: empty tilt part");
    split("rx + rz", &p, &t);
    CHECK(p.empty() && t == "rx + rz", "no translation terms: empty push part");
    split("y + z", &p, &t);
    CHECK(p == "y + z" && t.empty(), "lift/press y is a translation term");
    split("z + ry + rx", &p, &t);
    CHECK(p == "z" && t == "ry + rx", "every rotation axis is a tilt part (driver axis names differ): '%s' | '%s'", p.c_str(), t.c_str());
    split("y + rx", &p, &t);
    CHECK(p == "y" && t == "rx", "Windows/macOS order: push y");
    split("x - ry", &p, &t);
    CHECK(p == "x" && t == "-ry", "Windows/macOS order: tilt sideways ry is a tilt part");
    split("", &p, &t);
    CHECK(p.empty() && t.empty(), "empty mix");
    splitMix(d.scroll_v.mix, nullptr, &mt);
    splitMix(d.scroll_v.mix, &mp, nullptr);  // null outputs are allowed
    CHECK(mixText(mt) == "rx" && mixText(mp) == "z", "null outputs");
}

// ---------------------------------------------------------------- per-part keys
static void testPartKeys() {
    Settings s;
    std::string err;
    CHECK(applyKey(s, "SCROLL_V_TILT_SENS", "9", &err), "%s", err.c_str());
    CHECK(applyKey(s, "scroll_h_push_deadzone", "44", &err), "%s", err.c_str());
    CHECK(applyKey(s, "SCROLL_H_TILT_SIGN", "-1", &err), "%s", err.c_str());
    CHECK(applyKey(s, "SCROLL_V_PUSH_CURVE", "1.5", &err), "%s", err.c_str());
    CHECK(applyKey(s, "SCROLL_V_PUSH_SMOOTH_MS", "30", &err), "%s", err.c_str());
    CHECK(s.parts[PART_V_TILT].set == PartTuning::F_SENS && s.parts[PART_V_TILT].cfg.sensitivity == 9, "V tilt sens");
    CHECK(s.parts[PART_H_PUSH].set == PartTuning::F_DEADZONE && s.parts[PART_H_PUSH].cfg.deadzone == 44, "H push dz");
    CHECK(s.parts[PART_H_TILT].cfg.sign == -1, "H tilt sign");
    CHECK(s.parts[PART_V_PUSH].set == (PartTuning::F_CURVE | PartTuning::F_SMOOTH), "V push curve+smooth");
    CHECK(s.scroll_v.sensitivity == 5 && s.scroll_h.deadzone == 20, "the parent channel is untouched");

    // Invalid values and unknown keys are rejected and leave the settings as they were.
    Settings t;
    CHECK(!applyKey(t, "SCROLL_V_TILT_SENS", "abc", &err), "not a number");
    CHECK(!applyKey(t, "SCROLL_H_PUSH_SIGN", "2", &err), "sign 2");
    CHECK(!applyKey(t, "SCROLL_V_PUSH_MIX", "z", &err), "a part has no mix key of its own");
    CHECK(!applyKey(t, "SCROLL_V_MIDDLE_SENS", "5", &err), "unknown part");
    CHECK(!applyKey(t, "SCROLL_X_PUSH_SENS", "5", &err), "unknown channel");
    CHECK(!applyKey(t, "ZOOM_PUSH_SENS", "5", &err), "twist has no parts");
    CHECK(t.parts[PART_V_TILT].set == 0 && t.parts[PART_H_PUSH].set == 0, "failed keys set nothing");

    // Same ranges as the parent keys: the sensitivity is clamped to 0..255, the smoothing to 0..500.
    CHECK(applyKey(t, "SCROLL_V_TILT_SENS", "1000", &err) && t.parts[PART_V_TILT].cfg.sensitivity == 255, "clamped");
    CHECK(applyKey(t, "SCROLL_V_TILT_SMOOTH_MS", "9999", &err) && t.parts[PART_V_TILT].cfg.smooth_ms == 500.0, "clamped");

    // Round trip through allKeys: only the tuned values appear, and applying them again reproduces them.
    const std::vector<KV> kv = allKeys(s);
    int found = 0;
    for (const KV &p : kv)
        if (p.first.rfind("SCROLL_V_PUSH_", 0) == 0 || p.first.rfind("SCROLL_V_TILT_", 0) == 0 ||
            p.first.rfind("SCROLL_H_PUSH_", 0) == 0 || p.first.rfind("SCROLL_H_TILT_", 0) == 0)
            ++found;
    CHECK(found == 5, "five tuned values listed, got %d", found);
    Settings back;
    for (const KV &p : kv) CHECK(applyKey(back, p.first, p.second, &err), "%s: %s", p.first.c_str(), err.c_str());
    CHECK(allKeys(back) == kv, "round trip");
    CHECK(allKeys(Settings()).size() == allKeys(Settings()).size() && diffKeys(Settings(), Settings()).empty(),
          "defaults list no part keys");
    CHECK(partTuningKeys().size() == 20, "20 per-part keys");

    // partConfig: parent + the part's mix + tuned fields only.
    Mix mp, mt;
    splitMix(s.scroll_v.mix, &mp, &mt);
    const ChannelCfg c = partConfig(s.scroll_v, s.parts[PART_V_TILT], mt);
    CHECK(c.sensitivity == 9 && c.deadzone == s.scroll_v.deadzone && c.sign == 1 && mixText(c.mix) == "rx", "tilt cfg");
    const ChannelCfg c2 = partConfig(s.scroll_v, s.parts[PART_V_PUSH], mp);
    CHECK(c2.curve_exp == 1.5 && c2.smooth_ms == 30.0 && c2.sensitivity == 5 && mixText(c2.mix) == "z", "push cfg");

    // Part keys work in a profile section too and resolve like every other key (the active mode decides if they act).
    ProfileSet ps;
    std::vector<std::string> warnings;
    const std::string dir = tu::makeTempDir("spn_split_cfg");
    {
        std::ofstream f(dir + "/c.conf");
        f << "[global]\nSCROLL_V_TILT_SENS = 12\n[app:foo]\nSCROLL_V_TILT_SENS = 20\n";
    }
    CHECK(loadProfiles(dir + "/c.conf", &ps, &warnings) && warnings.empty(), "profile with part keys loads cleanly");
    CHECK(ps.resolve("", "").parts[PART_V_TILT].cfg.sensitivity == 12 &&
              ps.resolve("", "foo").parts[PART_V_TILT].cfg.sensitivity == 20, "resolve precedence");
    tu::removeAll(dir);
}

// ---------------------------------------------------------------- mode file parser
static ModeSet loadText(const std::string &text, std::vector<std::string> *warnings, const char *id = "sp") {
    static std::string dir;
    if (!dir.empty()) tu::removeAll(dir);
    dir = tu::makeTempDir("spn_split_modes");
    std::ofstream(dir + "/" + id + ".conf") << text;
    return loadModes(dir, warnings);
}

static bool warned(const std::vector<std::string> &w, const std::string &needle) {
    for (const auto &x : w)
        if (x.find(needle) != std::string::npos) return true;
    return false;
}

static void testParser() {
    std::vector<std::string> w;
    {
        ModeSet ms = loadText("V_PUSH_ACTION = zoom\nV_TILT_ACTION = step:pageup,pagedown\n"
                              "H_PUSH_ACTION = wheel_h\nH_TILT_ACTION = keys:ctrl+left,ctrl+right\n"
                              "SCROLL_V_TILT_SENS = 9\nSCROLL_H_PUSH_DEADZONE = 40\n", &w);
        const ModeDef *m = ms.find("sp");
        CHECK(m != nullptr && w.empty(), "valid file loads without warnings (%zu)", w.size());
        if (m) {
            CHECK(m->vSplit.pushSet && m->vSplit.push.type == ChannelActionType::Zoom, "V push zoom");
            CHECK(m->vSplit.tiltSet && m->vSplit.tilt.type == ChannelActionType::Step && m->vSplit.tilt.neg == Key::PageUp &&
                      m->vSplit.tilt.pos == Key::PageDown, "V tilt step");
            CHECK(m->hSplit.pushSet && m->hSplit.push.type == ChannelActionType::WheelH, "H push wheel_h");
            CHECK(m->hSplit.tiltSet && m->hSplit.tilt.type == ChannelActionType::Keys && m->hSplit.tilt.negMods.n == 1,
                  "H tilt chord");
            CHECK(m->v.type == ChannelActionType::Default && m->h.type == ChannelActionType::Default, "channels untouched");
            CHECK(!m->allDefaultChannels(), "a split mode is a custom-path mode");
            CHECK(m->overrides.size() == 2, "the per-part tuning keys are overrides");
        }
    }
    {  // lower case keys and spaces like every other key; only one key set
        ModeSet ms = loadText("v_tilt_action =  KEYS:Left , Right  # comment\n", &w);
        const ModeDef *m = ms.find("sp");
        CHECK(m && m->vSplit.tiltSet && !m->vSplit.pushSet && !m->hSplit.active(), "one key");
        CHECK(m && m->vSplit.tilt.neg == Key::Left && m->vSplit.tilt.pos == Key::Right, "value parsed");
        CHECK(m && m->vSplit.pushAction(m->v).type == ChannelActionType::Default, "unset part follows the channel");
    }
    {  // invalid values: the line is dropped with a warning, the part stays unset
        w.clear();
        ModeSet ms = loadText("V_PUSH_ACTION = bogus\nH_TILT_ACTION = keys:left\nH_PUSH_ACTION = step:a,b,c\n"
                              "V_TILT_ACTION = keys:nokey,right\n", &w);
        const ModeDef *m = ms.find("sp");
        CHECK(m && !m->vSplit.active() && !m->hSplit.active(), "invalid actions leave the parts unset");
        CHECK(warned(w, "V_PUSH_ACTION") && warned(w, "H_TILT_ACTION") && warned(w, "H_PUSH_ACTION") && warned(w, "V_TILT_ACTION"),
              "one warning per bad line");
        CHECK(m && m->allDefaultChannels(), "stays on the legacy path");
    }
    {  // unknown part keys and bad tuning values
        w.clear();
        ModeSet ms = loadText("V_MIDDLE_ACTION = none\nTWIST_PUSH_ACTION = none\nSCROLL_V_TILT_SENS = x\n"
                              "SCROLL_V_TILT_SIGN = 3\nSCROLL_V_LIFT_SENS = 4\n", &w);
        const ModeDef *m = ms.find("sp");
        CHECK(m && m->overrides.empty() && !m->vSplit.active(), "nothing accepted");
        CHECK(warned(w, "V_MIDDLE_ACTION") && warned(w, "TWIST_PUSH_ACTION") && warned(w, "SCROLL_V_TILT_SENS") &&
                  warned(w, "SCROLL_V_TILT_SIGN") && warned(w, "SCROLL_V_LIFT_SENS"), "all warned");
    }
    {  // the override limit counts the per-part keys like every other override
        w.clear();
        std::string text;
        for (int i = 0; i < 129; ++i) text += "SCROLL_V_TILT_SENS = " + std::to_string(i % 200) + "\n";
        ModeSet ms = loadText(text, &w);
        CHECK(ms.find("sp") == nullptr && warned(w, "too many settings overrides"), "129 overrides: file ignored");
    }
    {  // a later line replaces an earlier one, like the other channel keys
        w.clear();
        ModeSet ms = loadText("H_PUSH_ACTION = none\nH_PUSH_ACTION = wheel_h\n", &w);
        const ModeDef *m = ms.find("sp");
        CHECK(m && m->hSplit.push.type == ChannelActionType::WheelH, "last wins");
    }
    {  // chord warnings also cover the parts
        w.clear();
        loadText("V_TILT_ACTION = keys:alt+left,alt+right\n", &w);
        CHECK(warned(w, "can press shortcut chords"), "risky chord on a part is reported");
    }
}

// ---------------------------------------------------------------- runner
static ModeDef splitMode(const char *vp, const char *vt, const char *hp = nullptr, const char *ht = nullptr) {
    ModeDef m = mode("split");
    setSplit(m.vSplit, vp, vt);
    setSplit(m.hSplit, hp, ht);
    return m;
}

static void testEachPartSeparately() {
    const Settings s = plain();
    // V push = step: pagedown/pageup, V tilt = wheel_v.
    const ModeDef m = splitMode("step:pageup,pagedown", "wheel_v");
    {  // only z (push forward): the push part fires, the tilt part stays silent
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        CHECK(!r.legacyPath(), "custom path");
        run(r, s, only(AX_Z, 300), 1.0);
        CHECK(o.count("tap:pagedown") == 1 && o.wv == 0 && o.ev.size() == 1, "only z: one tap, no wheel");
    }
    {  // only rx (tilt forward): the tilt part scrolls, no taps
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, s, only(AX_RX, 200), 1.0);
        CHECK(o.wv > 0 && o.count("tap:pagedown") == 0 && o.count("tap:pageup") == 0, "only rx: wheel only (wv=%d)", o.wv);
        CHECK(std::abs(o.wv - 50) <= 1, "rate 200/4 = 50 steps/s for 1 s (got %d)", o.wv);
    }
    {  // both at once: both parts work independently
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, s, axes(0, 0, 300, 200, 0, 0), 1.0);
        CHECK(o.count("tap:pagedown") == 1 && std::abs(o.wv - 50) <= 1, "both: tap and wheel (wv=%d)", o.wv);
    }
    {  // opposite signs do not cancel: push forward and tilt back
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, s, axes(0, 0, 300, -200, 0, 0), 1.0);
        CHECK(o.count("tap:pagedown") == 1 && o.wv < -45, "opposite signs: tap down and wheel up (wv=%d)", o.wv);
        // The mixed channel would have summed them: z + rx = 300 - 200 -> a small positive rate.
        Rec o2;
        ModeRunner r2(&o2);
        r2.setMode(mode("normal"));
        run(r2, s, axes(0, 0, 300, -200, 0, 0), 1.0);
        CHECK(o2.wv > 0 && o2.wv < 30, "mixed channel for comparison (wv=%d)", o2.wv);
    }
    {  // negative z fires the neg key
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, s, only(AX_Z, -300), 0.5);
        CHECK(o.count("tap:pageup") == 1, "neg key");
    }
    {  // sideways: H push = x, H tilt = -rz (the default H mix is x - rz)
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode(nullptr, nullptr, "wheel_h", "step:left,right"));
        run(r, s, only(AX_X, 300), 1.0);  // rate 300 * 0.15 = 45
        CHECK(std::abs(o.wh - 45) <= 1 && o.count("tap:right") == 0, "x only: H push scrolls (wh=%d)", o.wh);
        o.clear();
        r.setMode(splitMode(nullptr, nullptr, "wheel_h", "step:left,right"));
        run(r, s, only(AX_RZ, 300), 1.0);  // tilt part = -rz: rate -45 -> the neg key
        CHECK(o.wh == 0 && o.count("tap:left") == 1 && o.count("tap:right") == 0, "rz only: H tilt uses the negated sign");
    }
}

static void testPartActions() {
    const Settings s = plain();
    {  // keys: rate-driven taps on a part
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode(nullptr, "keys:up,down"));
        run(r, s, only(AX_RX, 200), 1.0);
        CHECK(std::abs(o.count("tap:down") - 50) <= 1, "keys on tilt: ~50 taps (%d)", o.count("tap:down"));
        CHECK(o.wv == 0, "no z, no push output");
    }
    {  // zoom on a part: Ctrl held while it is deflected, released afterwards; wheel is vertical
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode(nullptr, "zoom"));
        run(r, s, only(AX_RX, 200), 0.5);
        CHECK(o.count("+ctrl") == 1 && o.count("-ctrl") == 0 && o.wv > 0, "zoom: Ctrl held, wheel sent (wv=%d)", o.wv);
        run(r, s, RawAxes{}, 0.1);
        CHECK(o.count("-ctrl") == 1, "Ctrl released when the part returns to rest");
    }
    {  // none on a part
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode("none", nullptr));
        run(r, s, only(AX_Z, 300), 0.5);
        CHECK(o.ev.empty(), "none: nothing for z");
        run(r, s, only(AX_RX, 200), 0.5);
        CHECK(o.wv > 0, "the tilt part (unset, follows the default channel) still scrolls");
    }
    {  // wheel_h on a V part and wheel_v on an H part (actions are not tied to the channel's axis)
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode("wheel_h", nullptr, "wheel_v", nullptr));
        run(r, s, only(AX_Z, 200), 0.5);
        CHECK(o.wh > 0 && o.wv == 0, "V push on the horizontal wheel");
        o.clear();
        run(r, s, only(AX_X, 200), 0.5);
        CHECK(o.wv > 0 && o.wh == 0, "H push on the vertical wheel");
    }
    {  // step on both H parts with a chord
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode(nullptr, nullptr, "step:left,right", "step:ctrl+left,ctrl+right"));
        run(r, s, only(AX_X, 300), 0.5);
        CHECK(o.count("tap:right") == 1 && o.count("+ctrl") == 0, "plain step");
        o.clear();
        run(r, s, only(AX_RZ, -300), 0.5);  // tilt part = -rz -> positive rate -> pos chord
        CHECK(o.count("tap:right") == 1 && o.count("+ctrl") == 1 && o.count("-ctrl") == 1, "chord step");
    }
    {  // step: re-arms per part independently
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode("step:left,right", "step:up,down"));
        run(r, s, axes(0, 0, 300, 300, 0, 0), 1.0);
        CHECK(o.count("tap:right") == 1 && o.count("tap:down") == 1, "each part fired once");
        run(r, s, only(AX_RX, 300), 0.3);  // z released: its part re-arms, the tilt stays latched
        run(r, s, axes(0, 0, 300, 300, 0, 0), 0.3);
        CHECK(o.count("tap:right") == 2 && o.count("tap:down") == 1, "push re-armed, tilt still latched");
    }
}

static void testFallbackAndDefault() {
    const Settings s = plain();
    {  // a part without its own key uses V_ACTION
        ModeDef m = splitMode(nullptr, "wheel_v");
        m.v = act("step:pageup,pagedown");
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, s, only(AX_Z, 300), 0.5);
        CHECK(o.count("tap:pagedown") == 1 && o.wv == 0, "push follows V_ACTION");
        o.clear();
        run(r, s, RawAxes{}, 0.1);
        run(r, s, only(AX_RX, 200), 0.5);
        CHECK(o.wv > 0 && o.count("tap:pagedown") == 0, "tilt has its own key");
    }
    {  // an explicit "default" on a part is the channel's normal behavior, not V_ACTION
        ModeDef m = splitMode("default", nullptr);
        m.v = act("none");
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, s, only(AX_Z, 200), 0.5);
        CHECK(o.wv > 0, "push default = wheel");
        o.clear();
        run(r, s, only(AX_RX, 200), 0.5);
        CHECK(o.ev.empty(), "tilt follows V_ACTION = none");
    }
    {  // equivalence: with the default action on both parts, the parts add up to the mixed channel
        // (dead zone off and linear, so splitting the sum changes nothing)
        Rec a, b;
        ModeRunner ra(&a), rb(&b);
        ModeDef plainCustom = mode("c");
        plainCustom.twist = act("none");  // custom path without any split
        rb.setMode(plainCustom);
        ModeDef sp = splitMode("default", "default", "default", "default");
        sp.twist = act("none");
        ra.setMode(sp);
        const RawAxes in = axes(100, 0, 160, 120, 0, -80);
        run(ra, s, in, 2.0);
        run(rb, s, in, 2.0);
        CHECK(std::abs(a.wv - b.wv) <= 2 && std::abs(a.wh - b.wh) <= 2, "split(default) = mixed (equal up to the remainder of each accumulator): wv %d/%d wh %d/%d", a.wv, b.wv, a.wh, b.wh);
        CHECK(a.wv != 0 && a.wh != 0, "both channels produced output");
    }
    {  // an unsplit channel next to a split one keeps working as before
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode("none", "none"));
        run(r, s, only(AX_X, 300), 0.5);
        CHECK(o.wh > 0, "H channel unsplit: wheel_h");
        o.clear();
        run(r, s, only(AX_Z, 300), 0.5);
        run(r, s, only(AX_RX, 300), 0.5);
        CHECK(o.ev.empty(), "V parts both none");
    }
}

static void testPartTuning() {
    Settings s = plain();
    std::string err;
    CHECK(applyKey(s, "SCROLL_V_TILT_SENS", "10", &err), "%s", err.c_str());  // absolute: tilt at twice the parent's 5
    const ModeDef m = splitMode("wheel_v", "wheel_v");
    {
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, s, only(AX_Z, 200), 1.0);
        const int push = o.wv;
        o.clear();
        run(r, s, RawAxes{}, 0.1);
        run(r, s, only(AX_RX, 200), 1.0);
        CHECK(std::abs(push - 50) <= 1 && std::abs(o.wv - 100) <= 1, "push at parent sens 5, tilt at 10 (%d, %d)", push, o.wv);
    }
    {  // the parent's dead zone applies to a part unless the part overrides it
        Settings d = plain();
        d.scroll_v.deadzone = 100;
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, d, only(AX_Z, 90), 0.5);
        CHECK(o.ev.empty(), "parent dead zone 100 holds back z = 90");
        CHECK(applyKey(d, "SCROLL_V_PUSH_DEADZONE", "0", &err), "%s", err.c_str());
        run(r, d, only(AX_Z, 90), 0.5);
        CHECK(o.wv > 0, "per-part dead zone 0 lets it through");
        o.clear();
        run(r, d, RawAxes{}, 0.1);
        run(r, d, only(AX_RX, 90), 0.5);
        CHECK(o.ev.empty(), "the tilt part keeps the parent's dead zone");
    }
    {  // sign per part
        Settings d = plain();
        CHECK(applyKey(d, "SCROLL_V_PUSH_SIGN", "-1", &err), "%s", err.c_str());
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, d, only(AX_Z, 200), 0.5);
        CHECK(o.wv < 0, "push sign -1");
        o.clear();
        run(r, d, RawAxes{}, 0.1);
        run(r, d, only(AX_RX, 200), 0.5);
        CHECK(o.wv > 0, "tilt keeps the parent's sign");
    }
    {  // the parent's sign applies to both parts
        Settings d = plain();
        d.scroll_v.sign = -1;
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, d, axes(0, 0, 200, 200, 0, 0), 0.5);
        CHECK(o.wv < 0, "parent sign");
    }
    {  // sensitivity 0 turns a part off
        Settings d = plain();
        CHECK(applyKey(d, "SCROLL_V_TILT_SENS", "0", &err), "%s", err.c_str());
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, d, only(AX_RX, 300), 0.5);
        CHECK(o.ev.empty(), "tilt sens 0");
    }
    {  // a custom mix on the channel is split with its weights
        Settings d = plain();
        CHECK(applyKey(d, "SCROLL_V_MIX", "0.5*rx + 2*z", &err), "%s", err.c_str());
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, d, only(AX_RX, 200), 1.0);  // 0.5 * 200 / 4 = 25
        const int tilt = o.wv;
        o.clear();
        run(r, d, RawAxes{}, 0.1);
        run(r, d, only(AX_Z, 100), 1.0);   // 2 * 100 / 4 = 50
        CHECK(std::abs(tilt - 25) <= 1 && std::abs(o.wv - 50) <= 1, "weights kept (%d, %d)", tilt, o.wv);
    }
    {  // a mix without rotation terms: the tilt part never fires
        Settings d = plain();
        CHECK(applyKey(d, "SCROLL_V_MIX", "z", &err), "%s", err.c_str());
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, d, only(AX_RX, 300), 0.5);
        CHECK(o.ev.empty(), "no rotation term, no tilt output");
        run(r, d, only(AX_Z, 200), 0.5);
        CHECK(o.wv > 0, "push still works");
    }
    {  // a mix without translation terms: the push part never fires
        Settings d = plain();
        CHECK(applyKey(d, "SCROLL_V_MIX", "rx", &err), "%s", err.c_str());
        Rec o;
        ModeRunner r(&o);
        r.setMode(m);
        run(r, d, only(AX_Z, 300), 0.5);
        CHECK(o.ev.empty(), "no translation term, no push output");
    }
}

static void testReleasesAndModeSwitch() {
    const Settings s = plain();
    {  // mode switch while a zoom part is deflected: Ctrl is released at once, nothing leaks into the next mode
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode(nullptr, "zoom", nullptr, "keys:left,right"));
        run(r, s, axes(0, 0, 0, 200, 0, -200), 0.3);
        CHECK(o.count("+ctrl") == 1 && o.count("-ctrl") == 0, "Ctrl held by the zoom part");
        CHECK(r.axesActive() && r.holdsAnything(), "active and holding");
        o.clear();
        r.setMode(mode("other"));
        CHECK(o.count("-ctrl") == 1, "setMode releases Ctrl");
        CHECK(!r.holdsAnything(), "nothing held");
        run(r, s, axes(0, 0, 0, 200, 0, -200), 0.3);  // still deflected: the legacy path takes over
        CHECK(o.count("tap:left") == 0 && o.count("tap:right") == 0, "no keys from the old split mode");
        CHECK(r.info().rate_part[PART_V_TILT] == 0.0 && r.info().rate_part[PART_H_TILT] == 0.0, "part rates cleared");
    }
    {  // a part's accumulated remainder and step latch do not survive a mode switch
        Rec o;
        ModeRunner r(&o);
        const ModeDef m = splitMode("step:up,down", nullptr);
        r.setMode(m);
        run(r, s, only(AX_Z, 300), 0.3);
        CHECK(o.count("tap:down") == 1, "fired");
        r.setMode(m);  // re-entering re-arms
        run(r, s, only(AX_Z, 300), 0.3);
        CHECK(o.count("tap:down") == 2, "re-armed after the switch");
    }
    {  // releaseAll() (unplug, suspend, safety net) while the part holds Ctrl
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode("zoom", nullptr));
        run(r, s, only(AX_Z, 200), 0.3);
        CHECK(o.count("+ctrl") == 1, "Ctrl held");
        r.releaseAll();
        CHECK(o.count("-ctrl") == 1 && !r.holdsAnything(), "releaseAll releases the part's Ctrl");
        r.releaseAll();
        CHECK(o.count("-ctrl") == 1, "idempotent");
        run(r, s, RawAxes{}, 0.1);
        CHECK(!r.axesActive(), "quiet again");
    }
    {  // resetDsp() (profile change) releases Ctrl too
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode("zoom", nullptr));
        run(r, s, only(AX_Z, 200), 0.3);
        r.resetDsp();
        CHECK(o.count("-ctrl") == 1, "resetDsp releases Ctrl");
    }
    {  // axesActive() sees a part (the daemon's safety net must not release a part that is working)
        Rec o;
        ModeRunner r(&o);
        r.setMode(splitMode("none", "wheel_v"));
        run(r, s, only(AX_RX, 200), 0.1);
        CHECK(r.axesActive(), "tilt part active");
        CHECK(std::fabs(r.info().rate_part[PART_V_TILT]) > 1.0 && r.info().rate_part[PART_V_PUSH] == 0.0, "info part rates");
        CHECK(r.info().rate_v == r.info().rate_part[PART_V_TILT], "rate_v shows the stronger part");
        run(r, s, RawAxes{}, 0.1);
        CHECK(!r.axesActive(), "idle after release");
    }
    {  // nav: a wheel part becomes arrow keys while a nav button is held, like the channel does
        Rec o;
        ModeRunner r(&o);
        ModeDef m = splitMode("wheel_v", "wheel_v");
        ButtonBinding b;
        b.button = 1;
        b.type = ButtonActionType::Nav;
        m.buttons.push_back(b);
        r.setMode(m);
        run(r, s, only(AX_Z, 200), 0.5, 1u);
        CHECK(o.wv == 0 && o.count("tap:down") > 20, "nav: arrow keys instead of the wheel");
    }
}

int main() {
    testSplitMix();
    testPartKeys();
    testParser();
    testEachPartSeparately();
    testPartActions();
    testFallbackAndDefault();
    testPartTuning();
    testReleasesAndModeSwitch();
    if (failures) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("test_split_channels: OK");
    return 0;
}
