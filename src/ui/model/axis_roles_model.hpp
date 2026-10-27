// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What the six raw axes mean on this system, read from the configuration (issue #186, toolkit-free).
//
// Axis names differ between drivers (spacenavd: z is push forward/back; the official 3Dconnexion driver on
// Windows and macOS: y is). The interface therefore never assumes them. It reads what the EFFECTIVE settings of
// what is shown say (SCROLL_V_MIX, SCROLL_H_MIX, ZOOM_MIX: global < device < joystick mode < program) and
// derives the roles from that. Nothing here knows an operating system, a driver or a device name: a new one
// needs only data (its examples/default/<os>/dofscroll.conf, a profile of the device), see docs/ui.md.
//
//   deriveAxisRoles(settings, axisMask)   // the one derivation; a plain data result
//   axisRoleText(roles, axis, strings)    // neutral words: "Axis y scrolls vertically"
//   axesHint(roles, settings, strings)    // the sentence of the Axes page
//   LiftWatch                             // remembers the last sure lift/press axis while a mix is typed
//
// Neutral truth (always right): per axis, which channels use it (uses[]). Physical hint (only where the
// mixes match a known pattern of rolePatterns()): push, tilt, twist, lift/press per motion. The hint is
// not a claim about the device: it says "the configuration treats this axis as the push".
#pragma once
#include <array>
#include <string>
#include <vector>

#include "common.hpp"
#include "config.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

enum class Motion { VPush, VTilt, HPush, HTilt, Twist, Lift };
constexpr int kMotionCount = 6;
constexpr unsigned kAllAxes = (1u << AX_COUNT) - 1;  // axis mask of a device with all six axes

// ---- data: the known patterns -------------------------------------------------------------------------
// A pattern is one mix expression per channel over placeholders: T<n> = a translation axis (x, y, z),
// R<n> = a rotation axis (rx, ry, rz), taken in the order the mix lists them. The operators and the signs
// are not compared (inverting an axis does not change what it is). The first pattern that fits all three
// channels wins; adding a pattern is one row (and one row in tests/test_ui_axis_roles.cpp).
struct RolePattern {
    const char *name;
    const char *vertical, *horizontal, *zoom;
};
inline const std::vector<RolePattern> &rolePatterns() {
    static const std::vector<RolePattern> p = {
        {"push and tilt", "T1 + R1", "T2 - R2", "R3"},
        {"push only", "T1", "T2", "R3"},
    };
    return p;
}
// What each placeholder is called for the user (the physical hint).
struct PlaceholderRole {
    const char *placeholder;
    Motion motion;
};
inline const std::vector<PlaceholderRole> &placeholderRoles() {
    static const std::vector<PlaceholderRole> r = {{"T1", Motion::VPush}, {"R1", Motion::VTilt}, {"T2", Motion::HPush},
                                                   {"R2", Motion::HTilt}, {"R3", Motion::Twist}};
    return r;
}

// ---- result ---------------------------------------------------------------------------------------------
struct AxisRoles {
    struct Use {
        bool v = false, h = false, zoom = false;  // the axis is a term of that channel mix
    };
    std::array<Use, AX_COUNT> uses{};
    std::array<int, kMotionCount> axisOf{-1, -1, -1, -1, -1, -1};  // per Motion: the axis, -1 = no hint
    bool matched = false;                                          // a known pattern fits: the hints are valid
    std::string pattern;                                           // its name, "" when none fits

    bool has(Motion m) const { return axisOf[size_t(m)] >= 0; }
    Axis axis(Motion m) const { return Axis(axisOf[size_t(m)]); }  // only when has(m)
    bool motionOf(Axis a, Motion *out) const {
        for (int m = 0; m < kMotionCount; ++m)
            if (axisOf[size_t(m)] == int(a)) {
                if (out) *out = Motion(m);
                return true;
            }
        return false;
    }
    bool complete() const {
        for (int a : axisOf)
            if (a < 0) return false;
        return true;
    }
};

namespace roles_detail {

inline bool isTranslation(Axis a) { return a == AX_X || a == AX_Y || a == AX_Z; }

inline std::vector<std::string> words(const char *expr) {
    std::vector<std::string> out;
    std::string cur;
    for (const char *p = expr;; ++p) {
        if (*p == ' ' || *p == '\0') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            if (!*p) break;
        } else {
            cur += *p;
        }
    }
    return out;
}

inline void termAxes(const Mix &m, std::vector<Axis> *trans, std::vector<Axis> *rot) {
    for (const Term &t : m.terms) {
        if (t.weight == 0.0) continue;
        std::vector<Axis> &v = isTranslation(t.axis) ? *trans : *rot;
        bool dup = false;
        for (Axis a : v) dup = dup || a == t.axis;
        if (!dup) v.push_back(t.axis);
    }
}

// One channel against one expression: exactly as many rotation terms as the expression has R, at least as many
// translation terms as it has T (a further translation term is an addition, such as lift/press typed into a
// mix, and stays unnamed). Fills assign[placeholder] = axis.
inline bool matchChannel(const Mix &mix, const char *expr, std::vector<std::pair<std::string, Axis>> *assign) {
    std::vector<Axis> trans, rot;
    termAxes(mix, &trans, &rot);
    size_t nt = 0, nr = 0;
    for (const std::string &w : words(expr)) {
        if (w[0] != 'T' && w[0] != 'R') continue;
        const bool t = w[0] == 'T';
        size_t &n = t ? nt : nr;
        const std::vector<Axis> &src = t ? trans : rot;
        if (n >= src.size()) return false;
        assign->push_back({w, src[n++]});
    }
    return rot.size() == nr && trans.size() >= nt;
}

}  // namespace roles_detail

// The one derivation. `axisMask`: bit a set = the device has axis a (default all six); the lift/press hint
// needs exactly one translation axis left over among them.
inline AxisRoles deriveAxisRoles(const Settings &s, unsigned axisMask = kAllAxes) {
    using namespace roles_detail;
    AxisRoles r;
    const Mix *mixes[3] = {&s.scroll_v.mix, &s.scroll_h.mix, &s.zoom.mix};
    for (int c = 0; c < 3; ++c)
        for (const Term &t : mixes[c]->terms) {
            if (t.weight == 0.0) continue;
            AxisRoles::Use &u = r.uses[size_t(t.axis)];
            (c == 0 ? u.v : c == 1 ? u.h : u.zoom) = true;
        }
    for (const RolePattern &p : rolePatterns()) {
        std::vector<std::pair<std::string, Axis>> assign;
        const char *exprs[3] = {p.vertical, p.horizontal, p.zoom};
        bool ok = true;
        for (int c = 0; ok && c < 3; ++c) ok = matchChannel(*mixes[c], exprs[c], &assign);
        for (size_t i = 0; ok && i < assign.size(); ++i)
            for (size_t j = i + 1; j < assign.size(); ++j) ok = ok && assign[i].second != assign[j].second;
        if (!ok) continue;
        r.matched = true;
        r.pattern = p.name;
        unsigned taken = 0;
        for (const auto &a : assign) {
            taken |= 1u << unsigned(a.second);
            for (const PlaceholderRole &pr : placeholderRoles())
                if (a.first == pr.placeholder) r.axisOf[size_t(pr.motion)] = int(a.second);
        }
        int spare = -1, count = 0;
        for (int a = 0; a < AX_COUNT; ++a)
            if (isTranslation(Axis(a)) && (axisMask & (1u << a)) && !(taken & (1u << a))) {
                spare = a;
                ++count;
            }
        if (count == 1) r.axisOf[size_t(Motion::Lift)] = spare;
        break;
    }
    return r;
}

// "Axis y scrolls vertically", "Axis ry zooms", "Axis z is in no channel mix": what the config says, no physical word.
inline std::string axisRoleText(const AxisRoles &r, Axis a, const StringProvider &s) {
    const AxisRoles::Use &u = r.uses[size_t(a)];
    std::string what;
    auto add = [&](const std::string &t) { what += (what.empty() ? "" : ", ") + t; };
    if (u.v) add(s.tr("scrolls vertically"));
    if (u.h) add(s.tr("scrolls horizontally"));
    if (u.zoom) add(s.tr("zooms"));
    if (what.empty()) what = s.tr("is in no channel mix");
    return format(s.tr("Axis %1 %2"), {axisName(a), what});
}

inline std::string axesOfChannel(const Mix &m, const StringProvider &s) {
    std::string out;
    for (const Term &t : m.terms)
        if (t.weight != 0.0) out += (out.empty() ? "" : ", ") + std::string(axisName(t.axis));
    return out.empty() ? s.tr("none") : out;
}

// The sentence under the live bars of the Axes page. With the full pattern it names the movements; otherwise it
// says only which axes the channels use.
inline std::string axesHint(const AxisRoles &r, const Settings &st, const StringProvider &s) {
    if (r.matched && r.complete())
        return format(s.tr("Push (%1, %2, %3) and tilt (%4, %5) are the two parts of a scroll channel. A joystick mode "
                           "that splits a channel gives them different functions; twist (%6) is zoom by default."),
                      {axisName(r.axis(Motion::VPush)), axisName(r.axis(Motion::HPush)), axisName(r.axis(Motion::Lift)),
                       axisName(r.axis(Motion::VTilt)), axisName(r.axis(Motion::HTilt)), axisName(r.axis(Motion::Twist))});
    return format(s.tr("In this configuration vertical scroll uses the axes %1, horizontal scroll %2 and zoom %3. A joystick "
                       "mode that splits a channel gives the push and the tilt parts different functions."),
                  {axesOfChannel(st.scroll_v.mix, s), axesOfChannel(st.scroll_h.mix, s), axesOfChannel(st.zoom.mix, s)});
}

// Lift/press is the axis the configuration leaves out of the channels, so a typed mix can change the answer
// itself ("y" as the whole vertical mix breaks the pattern). The watch keeps the last roles that were sure and
// answers from them: it warns only when it knows the lift/press axis.
class LiftWatch {
public:
    void observe(const Settings &s) {
        const AxisRoles r = deriveAxisRoles(s);
        if (r.matched && r.has(Motion::Lift)) lift_ = int(r.axis(Motion::Lift));
    }
    bool known() const { return lift_ >= 0; }
    Axis lift() const { return Axis(lift_); }
    // True if the mix text (valid) has a term on the known lift/press axis.
    bool usesLift(const std::string &text) const {
        if (lift_ < 0) return false;
        Mix m;
        std::string err;
        if (!parseMix(text, &m, &err)) return false;
        for (const Term &t : m.terms)
            if (int(t.axis) == lift_ && t.weight != 0) return true;
        return false;
    }

private:
    int lift_ = -1;
};

}  // namespace spn::ui
