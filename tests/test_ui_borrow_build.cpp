// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Taking a motion or a button over from another profile, joystick mode or button mode (borrow_model.hpp), the
// choices of a motion with their notes (zoom from a tilt), and the profile builder (profile_builder_model.hpp):
// a copy of the EFFECTIVE assignment as explicit keys, push and tilt separately, cleared sources copy "cleared",
// nothing written on a refusal, follows-the-mode and own-copy, duplicate names refused, rollback.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <set>
#include <string>

#include "config.hpp"
#include "testutil.hpp"
#include "ui/model/borrow_model.hpp"
#include "ui/model/profile_builder_model.hpp"
#include "ui_fixture.hpp"

using namespace spn;
using namespace spn::ui;

static std::string sectionText(const SettingsModel &m, size_t i) {
    std::string t;
    for (const KV &kv : m.profiles().sections[i].kv) t += kv.first + "=" + kv.second + ";";
    return t;
}
static bool has(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

static BorrowSource profile(size_t i) {
    BorrowSource b;
    b.section = i;
    return b;
}
static BorrowSource joystickMode(const std::string &id) {
    BorrowSource b;
    b.kind = BorrowSource::Kind::JoystickMode;
    b.id = id;
    return b;
}
static BorrowSource buttonMode(const std::string &id) {
    BorrowSource b;
    b.kind = BorrowSource::Kind::ButtonMode;
    b.id = id;
    return b;
}

int main() {
    // the form's helpers: the next unused motion or button, plain names
    assert(nextUnusedId({"a", "b", "c"}, {}) == "a");
    assert(nextUnusedId({"a", "b", "c"}, {"a", "b"}) == "c");
    assert(nextUnusedId({"a"}, {"a"}).empty());
    assert(nextFreeButton({}, 4) == 1);
    assert(nextFreeButton({1, 2, 4}, 4) == 3);
    assert(nextFreeButton({1, 2}, 2) == 1);
    {
        const std::vector<std::string> n = plainChoiceNames({{"Timeline", "timeline"}, {"Twin", "a"}, {"Twin", "b"}, {"", "bare"}});
        assert(n[0] == "Timeline" && n[1] == "Twin (a)" && n[2] == "Twin (b)" && n[3] == "bare");
    }
    useFreshDefaultOf("linux");
    IdentityStrings id;
    const std::string root = tu::makeTempDir("dofscroll-borrow");
    ModeCatalog modes(root + "/modes");
    ButtonModeCatalog bmodes(root + "/buttons");

    ProfileSet ps;
    const size_t dev = ps.findOrAdd(Section::DEVICE, "Pro");
    const size_t code = ps.findOrAdd(Section::APP, "code");
    const size_t video = ps.findOrAdd(Section::APP, "video");
    SettingsModel m;
    m.setProfiles(ps, "");
    m.setModes(modes.modeSet(), modes.dir());
    std::string err;

    // ---- the source's effective assignment is copied as explicit keys, push and tilt separately ----
    assert(m.setValue(code, "V_TILT_ACTION", "zoom", &err));
    assert(m.setValue(code, "TWIST_ACTION", "keys:ctrl+minus,ctrl+plus", &err));
    assert(m.setButton(code, 2, "key:space", &err));
    assert(borrowMotion(m, video, "h_tilt", profile(code), &err));  // the tilt of the other channel, from another motion's source
    assert(sectionText(m, video) == "H_TILT_ACTION=default;");       // what code does there: the normal action, explicit
    assert(borrowMotion(m, video, "v_tilt", profile(code), &err));
    assert(m.effectiveWithMode(video).actions[kLayerApp].partSet[1] && sectionText(m, video).find("V_TILT_ACTION=zoom") != std::string::npos);
    assert(!has(sectionText(m, video), "V_PUSH_ACTION"));  // push is not touched by taking the tilt over
    assert(borrowMotion(m, video, "twist", profile(code), &err));
    assert(has(sectionText(m, video), "TWIST_ACTION=keys:ctrl+minus,ctrl+plus"));
    assert(m.setValue(code, "V_TILT_ACTION", "wheel_h", &err));  // later change in the source: the copy does not follow
    assert(has(sectionText(m, video), "V_TILT_ACTION=zoom"));
    // another motion of the source for this motion
    assert(borrowMotion(m, video, "v_push", profile(code), &err, "twist"));
    assert(has(sectionText(m, video), "V_PUSH_ACTION=keys:ctrl+minus,ctrl+plus"));

    // ---- cleared sources copy cleared; the effective source includes its joystick mode and Global ----
    assert(m.setValue(0, "H_ACTION", "none", &err));  // Global switches the horizontal channel off
    assert(borrowMotion(m, dev, "h_push", profile(0), &err) || !err.empty());  // a device profile below its mode: see below
    assert(clearMotion(m, code, "h_push", nullptr, &err));
    assert(borrowMotion(m, video, "h_push", profile(code), &err));
    assert(has(sectionText(m, video), "H_PUSH_ACTION=none"));
    assert(borrowedMotionWords(m, profile(code), "h_push", id).find("nothing (cleared)") != std::string::npos);
    assert(m.setValue(0, "H_ACTION", "default", &err));
    // a motion switched off by sensitivity 0 in the source is cleared too
    assert(m.setValue(dev, "SCROLL_V_SENS", "0", &err));
    {
        const BorrowedMotion b = borrowedMotion(m, profile(dev), "v_push");
        assert(b.ok && b.off);
    }

    // ---- sources: other profiles and joystick modes; not itself; lift has nothing ----
    {
        const std::vector<BorrowSource> src = motionSources(m, video);
        bool self = false, global = false, mode = false, btn = false;
        for (const BorrowSource &s : src) {
            self = self || (s.kind == BorrowSource::Kind::Profile && s.section == video);
            global = global || (s.kind == BorrowSource::Kind::Profile && s.section == 0 && s.label == "Global");
            mode = mode || (s.kind == BorrowSource::Kind::JoystickMode && s.id == "photo");
            btn = btn || s.kind == BorrowSource::Kind::ButtonMode;
        }
        assert(!self && global && mode && !btn);
        bool bm = false;
        for (const BorrowSource &s : buttonSources(m, video, &bmodes.set())) bm = bm || s.kind == BorrowSource::Kind::ButtonMode;
        assert(bm);
    }
    assert(!borrowMotion(m, video, "v_push", profile(video), &err) && !err.empty());
    assert(!borrowMotion(m, video, "lift", profile(code), &err));
    assert(!borrowMotion(m, 99, "v_push", profile(code), &err));
    assert(!borrowMotion(m, video, "v_push", profile(99), &err));
    assert(!borrowMotion(m, video, "v_push", joystickMode("nope"), &err));

    // from a joystick mode: its action for the motion; photo zooms with the vertical channel
    assert(borrowMotion(m, code, "v_push", joystickMode("photo"), &err));
    assert(has(sectionText(m, code), "V_PUSH_ACTION=zoom"));
    assert(borrowMotion(m, code, "h_tilt", joystickMode("photo"), &err));  // keys left/right in photo
    assert(has(sectionText(m, code), "H_TILT_ACTION=keys:left,right"));

    // a profile below its joystick mode cannot beat the mode: refused, nothing written
    assert(m.setValue(dev, "MODE", "photo", &err));
    const std::string devBefore = sectionText(m, dev);
    assert(!borrowMotion(m, dev, "twist", profile(code), &err) && has(err, "joystick mode"));
    assert(sectionText(m, dev) == devBefore);

    // ---- buttons: the effective binding, a mode's binding wins over Global, nothing is copied as nothing ----
    assert(borrowButton(m, video, 2, profile(code), &err));
    assert(has(sectionText(m, video), "BUTTON_2=key:space"));
    assert(borrowButton(m, video, 7, profile(code), &err));  // code binds nothing: an explicit nothing
    assert(has(sectionText(m, video), "BUTTON_7=none"));
    assert(borrowedButtonWords(m, profile(code), 2, nullptr, id).find("Space") != std::string::npos);
    assert(!borrowButton(m, video, 2, profile(video), &err));
    assert(!borrowButton(m, video, 0, profile(code), &err));
    assert(!borrowButton(m, video, 33, profile(code), &err));
    assert(!borrowButton(m, video, 3, buttonMode("latch-nav"), &err));  // no button modes given: refused
    assert(borrowButton(m, video, 3, buttonMode("latch-nav"), &err, &bmodes.set()));
    assert(has(sectionText(m, video), "BUTTON_3=toggle:nav"));
    {
        ModeDef d;
        const ModeDef *any = nullptr;
        for (const ModeDef &x : modes.modeSet().modes)
            if (!x.buttons.empty()) any = &x;
        if (any) {
            const int n = any->buttons[0].button;
            assert(borrowButton(m, video, n, joystickMode(any->id), &err));
            assert(has(sectionText(m, video), ("BUTTON_" + std::to_string(n) + "=").c_str()));
        }
        (void)d;
    }

    // ---- the choices of a motion: any motion can zoom; a profile that cannot beat its mode says so ----
    {
        const MotionChoiceCheck z = motionChoiceCheck(m, video, "v_tilt", ChannelActionType::Zoom, id);
        assert(z.works && has(z.note, "own channel") && has(z.note, "pause"));
        const MotionChoiceCheck t = motionChoiceCheck(m, video, "twist", ChannelActionType::Zoom, id);
        assert(t.works && t.note.empty());
        const MotionChoiceCheck k = motionChoiceCheck(m, video, "h_push", ChannelActionType::Keys, id);
        assert(k.works && k.note.empty());
        const MotionChoiceCheck pinned = motionChoiceCheck(m, dev, "twist", ChannelActionType::Keys, id);
        assert(!pinned.works && has(pinned.note, "joystick mode"));
        // lift / press: zoom works (with the warning that lifting fires when the cap is grabbed), analog keys never do
        const MotionChoiceCheck lz = motionChoiceCheck(m, video, "lift", ChannelActionType::Zoom, id);
        assert(lz.works && has(lz.note, "grab"));
        const MotionChoiceCheck lk = motionChoiceCheck(m, video, "lift", ChannelActionType::Keys, id);
        assert(!lk.works && has(lk.note, "analog"));
        assert(motionChoiceCheck(m, video, "lift", ChannelActionType::Step, id).works);
        assert(m.setValue(video, "ZOOM_MODIFIER", "none", &err));
        assert(has(motionChoiceCheck(m, video, "v_tilt", ChannelActionType::Zoom, id).note, "plain wheel"));
        m.clearOverride(video, "ZOOM_MODIFIER");
    }

    // ---- into a joystick mode being edited: the same copy, through the form of the mode ----
    {
        ModeForm f;
        const Settings base;
        assert(borrowMotionIntoMode(m, f, base, Motion::VTilt, profile(code), &err, Motion::Twist));  // the twist action of code
        assert(actionOf(f, base, Motion::VTilt).type == ChannelActionType::Keys);
        assert(actionOf(f, base, Motion::VPush) == ChannelForm());  // the push keeps what it did
        assert(borrowMotionIntoMode(m, f, base, Motion::VPush, joystickMode("photo"), &err));
        assert(actionOf(f, base, Motion::VPush).type == ChannelActionType::Zoom);
        assert(!borrowMotionIntoMode(m, f, base, Motion::Lift, profile(code), &err) && !err.empty());
        assert(borrowButtonIntoMode(m, f, 2, profile(code), nullptr, &err));
        assert(f.buttons.size() == 1 && f.buttons[0].first == 2 && f.buttons[0].second == "key:space");
        assert(borrowButtonIntoMode(m, f, 2, buttonMode("latch-nav"), &bmodes.set(), &err) && f.buttons[0].second == "toggle:nav");
        assert(!borrowButtonIntoMode(m, f, 2, buttonMode("latch-nav"), nullptr, &err));
        bool self = false;
        for (const BorrowSource &s : motionSources(m, ProfileSet::npos, "photo")) self = self || s.id == "photo";
        assert(!self);
    }

    // ---- also take over the tuning (#253): off = action only; on = the five keys of the motion, effective, explicit ----
    {
        assert(m.setValue(code, "V_TILT_ACTION", "zoom", &err));
        assert(m.setValue(code, "SCROLL_V_TILT_SENS", "8", &err));  // a tuned part (the key is valid in a profile)
        assert(m.setValue(code, "SCROLL_V_TILT_DEADZONE", "50", &err));
        assert(m.setValue(code, "SCROLL_V_TILT_SIGN", "-1", &err));
        assert(m.setValue(code, "SCROLL_V_TILT_CURVE", "2", &err));
        assert(m.setValue(code, "SCROLL_V_TILT_SMOOTH_MS", "40", &err));
        assert(m.setValue(code, "ZOOM_SENS", "3", &err));
        assert(m.setValue(code, "SCROLL_H_PUSH_SENS", "0", &err));  // tuned off
        assert(m.clearOverride(video, "V_TILT_ACTION"));
        // default: the action only, no tuning key
        assert(borrowMotion(m, video, "v_tilt", profile(code), &err));
        assert(has(sectionText(m, video), "V_TILT_ACTION=zoom") && !has(sectionText(m, video), "SCROLL_V_TILT_"));
        // on: the action and the five keys of that motion, nothing of the other motions
        assert(borrowMotion(m, video, "v_tilt", profile(code), &err, "", true));
        {
            const std::string t = sectionText(m, video);
            assert(has(t, "V_TILT_ACTION=zoom") && has(t, "SCROLL_V_TILT_SENS=8;") && has(t, "SCROLL_V_TILT_DEADZONE=50;"));
            assert(has(t, "SCROLL_V_TILT_SIGN=-1;") && has(t, "SCROLL_V_TILT_CURVE=2;") && has(t, "SCROLL_V_TILT_SMOOTH_MS=40;"));
            assert(!has(t, "SCROLL_V_PUSH_") && !has(t, "SCROLL_H_") && !has(t, "ZOOM_SENS"));
        }
        // a part the source does not tune: the effective value of its channel, explicit
        assert(borrowMotion(m, video, "v_push", profile(code), &err, "", true));
        assert(has(sectionText(m, video), "SCROLL_V_PUSH_SENS=5;") && has(sectionText(m, video), "SCROLL_V_PUSH_DEADZONE=30;"));
        // the twist uses ZOOM_*; another motion of the source can be the model of this one
        assert(borrowMotion(m, video, "twist", profile(code), &err, "", true));
        assert(has(sectionText(m, video), "ZOOM_SENS=3;") && has(sectionText(m, video), "ZOOM_SIGN=1;"));
        assert(borrowMotion(m, video, "h_tilt", profile(code), &err, "v_tilt", true));
        assert(has(sectionText(m, video), "SCROLL_H_TILT_SENS=8;") && has(sectionText(m, video), "SCROLL_H_TILT_SIGN=-1;"));
        // a source that tuned the motion off is copied as an explicit nothing
        assert(borrowMotion(m, video, "h_push", profile(code), &err, "", true));
        assert(has(sectionText(m, video), "H_PUSH_ACTION=none") && has(sectionText(m, video), "SCROLL_H_PUSH_SENS=0;"));
        // the words say what the tuning is
        assert(has(borrowedMotionWords(m, profile(code), "v_tilt", id, true), "sensitivity 8, dead zone 50, direction reversed"));
        assert(!has(borrowedMotionWords(m, profile(code), "v_tilt", id), "dead zone"));
        // the source keeps what it had; the copy is explicit and does not follow
        assert(has(sectionText(m, code), "SCROLL_V_TILT_SENS=8;"));
        assert(m.setValue(code, "SCROLL_V_TILT_SENS", "9", &err));
        assert(has(sectionText(m, video), "SCROLL_V_TILT_SENS=8;"));
        // the key engine keys (rate limits, trigger) come with the tuning where the source sets them (issue #260)
        assert(m.setValue(code, "SCROLL_V_TILT_KEY_RATE_MIN", "3", &err) && m.setValue(code, "SCROLL_V_TILT_KEY_RATE_MAX", "25", &err) &&
               m.setValue(code, "SCROLL_V_TILT_TRIGGER", "30", &err) && m.setValue(code, "ZOOM_KEY_RATE_MAX", "12", &err));
        assert(borrowMotion(m, video, "h_tilt", profile(code), &err, "v_tilt", true));
        assert(has(sectionText(m, video), "SCROLL_H_TILT_KEY_RATE_MIN=3;") && has(sectionText(m, video), "SCROLL_H_TILT_KEY_RATE_MAX=25;") &&
               has(sectionText(m, video), "SCROLL_H_TILT_TRIGGER=30;"));
        assert(!has(sectionText(m, video), "SCROLL_V_TILT_KEY_RATE"));  // the keys of the destination motion, not the source's
        assert(borrowMotion(m, video, "twist", profile(code), &err, "", true));
        assert(has(sectionText(m, video), "ZOOM_KEY_RATE_MAX=12;") && !has(sectionText(m, video), "ZOOM_KEY_RATE_MIN") &&
               !has(sectionText(m, video), "ZOOM_TRIGGER"));
        {
            std::vector<KV> kv;
            assert(copyMotionSettings(m, profile(code), [&](const std::string &key, const std::string &v, std::string *) { kv.emplace_back(key, v); return true; },
                                      "v_tilt", &err));
            assert(kv.size() == 8 && kv[5].first == "SCROLL_V_TILT_KEY_RATE_MIN" && kv[5].second == "3" && kv[7].first == "SCROLL_V_TILT_TRIGGER");
        }
        // without "also take over the tuning" they stay out
        assert(m.clearOverride(video, "SCROLL_H_TILT_KEY_RATE_MIN") && m.clearOverride(video, "SCROLL_H_TILT_TRIGGER"));
        assert(borrowMotion(m, video, "h_tilt", profile(code), &err, "v_tilt"));
        assert(!has(sectionText(m, video), "SCROLL_H_TILT_TRIGGER") && !has(sectionText(m, video), "SCROLL_H_TILT_KEY_RATE_MIN"));
        // into a joystick mode form
        {
            ModeForm kf;
            const Settings kbase;
            assert(borrowMotionIntoMode(m, kf, kbase, Motion::VTilt, profile(code), &err, Motion::VTilt, true));
            assert(kf.overrides.size() == 8 && kf.overrideValue("SCROLL_V_TILT_TRIGGER") && *kf.overrideValue("SCROLL_V_TILT_TRIGGER") == "30");
        }
        for (const char *key : {"SCROLL_V_TILT_KEY_RATE_MIN", "SCROLL_V_TILT_KEY_RATE_MAX", "SCROLL_V_TILT_TRIGGER", "ZOOM_KEY_RATE_MAX"})
            assert(m.clearOverride(code, key));
        for (const char *key : {"SCROLL_H_TILT_KEY_RATE_MAX", "ZOOM_KEY_RATE_MAX"}) assert(m.clearOverride(video, key));
        // refused: nothing is written (a profile below its mode, lift, an unknown source)
        const std::string devNow = sectionText(m, dev), videoNow = sectionText(m, video);
        assert(!borrowMotion(m, dev, "twist", profile(code), &err, "", true) && has(err, "joystick mode"));
        assert(sectionText(m, dev) == devNow);
        assert(!borrowMotion(m, video, "lift", profile(code), &err, "", true));
        assert(!borrowMotion(m, video, "v_tilt", profile(99), &err, "", true));
        assert(!borrowMotion(m, video, "v_tilt", joystickMode("nope"), &err, "", true));
        assert(!borrowMotion(m, video, "v_tilt", buttonMode("latch-nav"), &err, "", true));
        assert(sectionText(m, video) == videoNow);
        // a joystick mode as the source: Global plus the mode's own overrides
        assert(m.setValue(0, "SCROLL_V_SENS", "6", &err));
        assert(borrowMotion(m, video, "v_push", joystickMode("photo"), &err, "", true));
        assert(has(sectionText(m, video), "SCROLL_V_PUSH_SENS=6;"));
        assert(m.clearOverride(0, "SCROLL_V_SENS"));

        // into a joystick mode: the same function, written as overrides of the form
        ModeForm f;
        const Settings base;
        assert(borrowMotionIntoMode(m, f, base, Motion::VTilt, profile(code), &err));
        assert(f.overrides.empty());  // default: the action only
        ModeForm g;
        assert(borrowMotionIntoMode(m, g, base, Motion::VTilt, profile(code), &err, Motion::Lift, true));
        assert(actionOf(g, base, Motion::VTilt).type == ChannelActionType::Zoom);
        assert(g.overrides.size() == 5 && g.overrideValue("SCROLL_V_TILT_SENS") && *g.overrideValue("SCROLL_V_TILT_SENS") == "9");
        assert(*g.overrideValue("SCROLL_V_TILT_SIGN") == "-1" && *g.overrideValue("SCROLL_V_TILT_SMOOTH_MS") == "40");
        ModeForm h;
        assert(borrowMotionIntoMode(m, h, base, Motion::Twist, profile(code), &err, Motion::Lift, true));
        assert(h.overrideValue("ZOOM_SENS") && *h.overrideValue("ZOOM_SENS") == "3");
        ModeForm k;
        assert(!borrowMotionIntoMode(m, k, base, Motion::Lift, profile(code), &err, Motion::Lift, true) && k.overrides.empty());
        assert(!borrowMotionIntoMode(m, k, base, Motion::VTilt, profile(99), &err, Motion::Lift, true) && k.overrides.empty());
        // the shared function itself, with a sink of its own
        std::vector<KV> got;
        assert(copyMotionSettings(m, profile(code), [&](const std::string &key, const std::string &v, std::string *) { got.emplace_back(key, v); return true; },
                                  "v_push", &err, "v_tilt"));
        assert(got.size() == 5 && got[0].first == "SCROLL_V_PUSH_SENS" && got[0].second == "9");
        assert(!copyMotionSettings(m, profile(code), [](const std::string &, const std::string &, std::string *) { return true; }, "lift", &err));
        // back to what the builder tests below expect of the program "code"
        assert(m.setValue(code, "V_TILT_ACTION", "wheel_h", &err));
        for (const char *key : {"SCROLL_V_TILT_SENS", "SCROLL_V_TILT_DEADZONE", "SCROLL_V_TILT_SIGN", "SCROLL_V_TILT_CURVE", "SCROLL_V_TILT_SMOOTH_MS", "ZOOM_SENS",
                                "SCROLL_H_PUSH_SENS"})
            assert(m.clearOverride(code, key));
    }

    // ---- the builder ----
    const size_t before = m.sectionCount();
    ProfileBuild b;
    b.kind = Section::APP;
    b.name = "  gimp ";
    b.modeId = "photo";
    b.buttons.push_back({4, "latch-nav", false});
    b.motions.push_back({"v_tilt", profile(code), ""});
    b.buttonTakes.push_back({2, profile(code)});
    ProfileBuildResult r = buildProfile(m, b, &modes, &bmodes);
    assert(r.ok && r.section == before && m.sectionCount() == before + 1);
    assert(m.sections()[r.section].pattern == "gimp");  // the same normalization as every profile name
    {
        const std::string t = sectionText(m, r.section);
        assert(has(t, "MODE=photo") && has(t, "BUTTON_4=buttonmode:latch-nav") && has(t, "V_TILT_ACTION=wheel_h") && has(t, "BUTTON_2=key:space"));
        assert(m.effectiveMode(r.section) == "photo");
    }
    // a normal profile afterwards: editable like any other
    assert(clearMotion(m, r.section, "v_tilt", nullptr, &err));
    assert(inheritMotion(m, r.section, "v_tilt", nullptr));
    assert(!has(sectionText(m, r.section), "V_TILT_ACTION"));
    // duplicates are refused (case-insensitive, same kind); another kind is fine
    b.name = "GIMP";
    r = buildProfile(m, b, &modes, &bmodes);
    assert(!r.ok && has(r.error, "already exists") && m.sectionCount() == before + 1);
    b.kind = Section::DEVICE;
    b.name = "gimp";
    b.motions.clear();
    b.buttonTakes.clear();
    b.buttons.clear();
    r = buildProfile(m, b, &modes, &bmodes);
    assert(r.ok);
    // from a base: the base's own keys are copied, then the extras on top
    ProfileBuild c;
    c.name = "krita";
    c.base = code;
    c.modeId = "timeline";
    r = buildProfile(m, c, &modes, &bmodes);
    assert(r.ok && has(sectionText(m, r.section), "V_PUSH_ACTION=zoom") && has(sectionText(m, r.section), "MODE=timeline"));
    assert(!has(sectionText(m, code), "krita"));  // the base is untouched
    // an invalid name, a missing mode, a missing button mode, a doubled button: nothing is created
    const size_t n0 = m.sectionCount();
    ProfileBuild bad;
    bad.name = "";
    assert(!buildProfile(m, bad, &modes, &bmodes).ok);
    bad.name = "x1";
    bad.modeId = "no-such-mode";
    assert(!buildProfile(m, bad, &modes, &bmodes).ok);
    bad.modeId.clear();
    bad.buttons.push_back({1, "no-such-button-mode", false});
    assert(!buildProfile(m, bad, &modes, &bmodes).ok);
    bad.buttons = {{1, "latch-nav", false}, {1, "latch-nav", false}};
    assert(!buildProfile(m, bad, &modes, &bmodes).ok);
    bad.buttons.clear();
    bad.motions.push_back({"twist", profile(99), ""});  // fails late: the new profile is removed again
    assert(!buildProfile(m, bad, &modes, &bmodes).ok && m.sectionCount() == n0);
    bad.motions.clear();
    bad.kind = Section::GLOBAL;
    assert(!buildProfile(m, bad, &modes, &bmodes).ok);

    // own copies: the profile names the copy, which is a file of its own; a refused build removes the copies again
    ProfileBuild own;
    own.name = "blender";
    own.modeId = "timeline";
    own.modeOwnCopy = true;
    own.buttons.push_back({5, "latch-nav", true});
    r = buildProfile(m, own, &modes, &bmodes);
    assert(r.ok && r.createdModes.size() == 1 && r.createdButtonModes.size() == 1);
    assert(r.modeId != "timeline" && modes.find(r.modeId) && !modes.find(r.modeId)->path.empty());
    assert(has(sectionText(m, r.section), ("MODE=" + r.modeId).c_str()));
    assert(has(sectionText(m, r.section), ("BUTTON_5=buttonmode:" + r.createdButtonModes[0]).c_str()));
    assert(bmodes.find(r.createdButtonModes[0]) && !bmodes.find(r.createdButtonModes[0])->builtin);
    assert(modes.find("timeline") && modes.find("timeline")->origin == ModeCatalog::Origin::BuiltIn);  // the original is untouched
    {
        const size_t modeCount = modes.modes().size(), btnCount = bmodes.buttonModes().size();
        ProfileBuild fail = own;
        fail.name = "blender2";
        fail.motions.push_back({"twist", profile(99), ""});
        assert(!buildProfile(m, fail, &modes, &bmodes).ok);
        assert(modes.modes().size() == modeCount && bmodes.buttonModes().size() == btnCount);
    }
    // long profile names: the copy's id is cut at 26 characters, the numeric suffix must still make every copy unique
    {
        const char *names[] = {"org.mozilla.firefox", "org.mozilla.firefox-dev", "org.mozilla.firefox-nightly", "org.mozilla.firefox-esr"};
        std::set<std::string> ids, bids;
        for (const char *nm : names) {
            ProfileBuild lb;
            lb.name = nm;
            lb.modeId = "timeline";
            lb.modeOwnCopy = true;
            lb.buttons.push_back({6, "latch-nav", true});
            const ProfileBuildResult lr = buildProfile(m, lb, &modes, &bmodes);
            assert(lr.ok && lr.createdModes.size() == 1 && lr.createdButtonModes.size() == 1);
            assert(ids.insert(lr.createdModes[0]).second && bids.insert(lr.createdButtonModes[0]).second);
        }
        // a refused build after the copies were made leaves nothing behind
        const size_t modeCount = modes.modes().size(), btnCount = bmodes.buttonModes().size(), sections = m.sectionCount();
        ProfileBuild lf;
        lf.name = "org.mozilla.firefox-beta";
        lf.modeId = "timeline";
        lf.modeOwnCopy = true;
        lf.buttons.push_back({6, "latch-nav", true});
        lf.motions.push_back({"twist", profile(99), ""});
        assert(!buildProfile(m, lf, &modes, &bmodes).ok);
        assert(modes.modes().size() == modeCount && bmodes.buttonModes().size() == btnCount && m.sectionCount() == sections);
    }
    // a hand-written base with odd BUTTON_ keys: skipped, no overflow, the build succeeds
    {
        const size_t base = m.addSection(Section::APP, "oddkeys", &err);
        assert(base != ProfileSet::npos);
        ProfileSet ps2 = m.profiles();
        ps2.sections[base].kv.push_back({"BUTTON_abc", "key:space"});
        ps2.sections[base].kv.push_back({"BUTTON_99999999999999999999", "key:space"});
        ps2.sections[base].kv.push_back({"BUTTON_0", "key:space"});
        m.setProfiles(ps2, "");
        ProfileBuild ob;
        ob.name = "from-odd";
        ob.base = base;
        const ProfileBuildResult orr = buildProfile(m, ob, &modes, &bmodes);
        if (!orr.ok) std::fprintf(stderr, "odd: %s\n", orr.error.c_str());
        assert(orr.ok);
    }
    // the lift is refused with a message that says it is not offered yet
    {
        std::string e2;
        assert(!borrowMotion(m, 1, "lift", profile(0), &e2) && e2.find("not offered for the lift yet") != std::string::npos);
    }
    std::puts("test_ui_borrow_build: OK");
    return 0;
}
