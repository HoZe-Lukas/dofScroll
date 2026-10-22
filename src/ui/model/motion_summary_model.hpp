// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// What a joystick mode does with each of the six motions of the SpaceMouse cap (toolkit-free), for the
// chips of the Joystick modes list, the "what each motion does" panel and the "i" popover. Read from the
// loaded ModeDef (and the channel mixes of the settings, where a mix override of the mode matters); the
// file format is not touched.
//
// The six motions, in this fixed order (the 3x2 panel shows pushes in the top row, tilts and the twist below):
//   v_push  Vertical push   (push forward / back, axis z)       v_tilt  Vertical tilt   (tilt forward / back, axis rx)
//   h_push  Horizontal push (push sideways, axis x)             h_tilt  Horizontal tilt (tilt sideways, axis rz)
//   lift    Lift / press    (push down / pull up, axis y)       twist   Twist           (axis ry)
// A motion belongs to the first channel (vertical, horizontal, twist) whose mix has a term with its axis. An
// unsplit scroll channel gives push and tilt the same action; a split one (V_PUSH_ACTION, ...) its own action
// per part (push = x, y, z terms, tilt = rx, rz terms). The built-in mixes are V = z + rx, H = x - rz,
// twist = ry, so lift is normally "Not used" (only a mix with y uses it).
//
//   motionSummary(mode, settings, strings)   // exactly six rows
//   motionChipTexts(rows)                    // "↕ Scroll", ... for the list row
//   (the buttons of a mode: button_chip_model.hpp)
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"
#include "config.hpp"
#include "mode_list.hpp"
#include "modes.hpp"
#include "ui/model/strings.hpp"
#include "utf8.hpp"

namespace spn::ui {

enum class MotionKind { Active, Off, NotUsed };

struct MotionRow {
    std::string id;       // "v_push", "v_tilt", "h_push", "h_tilt", "lift", "twist"
    std::string name;     // "Vertical push"
    std::string how;      // "Push forward / back"
    std::string action;   // "Scroll", "Keys: Up / Down", "Off", "Not used"
    std::string chip;     // short word for the list chip: "Scroll", "Sideways", "Zoom", "Keys", "Taps", "Off", "Unused"
    std::string detail;   // keys of a Keys / Step action ("Left / Right"), "" otherwise: the chip shows it instead of the word
    std::string symbol;   // icon key: "scroll", "scroll_h", "zoom", "keys", "step", "off", "unused"
    std::string source;   // where the action comes from ("from the joystick mode", "inherited"); "" = not said
    MotionKind kind = MotionKind::Active;
    bool muted() const { return kind != MotionKind::Active; }
};

// The glyph of an icon key (symbols that common fonts have; the words next to them carry the meaning).
inline std::string motionGlyph(const std::string &symbol) {
    if (symbol == "scroll") return "\xE2\x86\x95";    // up-down arrow
    if (symbol == "scroll_h") return "\xE2\x86\x94";  // left-right arrow
    if (symbol == "zoom") return "\xE2\x8A\x95";      // circled plus
    if (symbol == "keys") return "\xE2\x8C\xA8";      // keyboard
    if (symbol == "step") return "\xE2\x87\xA5";      // arrow to bar (one tap)
    if (symbol == "off") return "\xE2\x8A\x98";       // circled slash
    if (symbol == "lift_in") return "+";              // lift / press added to another motion
    return "\xE2\x80\x93";                            // en dash: not used
}

// Marks the kind of motion in front of the action symbol, so a push and a tilt of the same channel are told
// apart without colour: a flat bar for a push (and lift / press), a tilted wedge for a tilt, a turn arrow for the twist.
inline std::string motionMarker(const std::string &id) {
    if (id == "v_tilt" || id == "h_tilt") return "\xE2\x97\xA2";  // black lower right triangle
    if (id == "twist") return "\xE2\x86\xBB";                    // clockwise open circle arrow
    return "\xE2\x96\xAC";                                       // black rectangle
}

namespace motion_detail {

// "pageup" -> "Page up", "ctrl+pageup" -> "Ctrl+Page up" (ASCII key names).
inline std::string keyWords(const std::string &chord) {
    std::string out;
    size_t start = 0;
    while (start <= chord.size()) {
        size_t plus = chord.find('+', start);
        if (plus == std::string::npos) plus = chord.size();
        std::string part = chord.substr(start, plus - start);
        if (part == "pageup") part = "page up";
        else if (part == "pagedown") part = "page down";
        if (!part.empty() && part[0] >= 'a' && part[0] <= 'z') part[0] = char(part[0] - 'a' + 'A');
        if (start != 0) out += "+";
        out += part;
        start = plus + 1;
    }
    return out;
}

inline bool hasAxis(const Mix &m, Axis a) {
    for (const Term &t : m.terms)
        if (t.axis == a && t.weight != 0.0) return true;
    return false;
}

inline MotionRow rowFor(const char *id, const std::string &name, const std::string &how, const ChannelAction &a, bool vertical,
                        bool twist, unsigned zoomModifier, const StringProvider &s) {
    MotionRow r;
    r.id = id;
    r.name = name;
    r.how = how;
    ChannelActionType t = a.type;
    if (t == ChannelActionType::Default) t = twist ? ChannelActionType::Zoom : vertical ? ChannelActionType::WheelV : ChannelActionType::WheelH;
    const std::string neg = keyWords(modelist::chordText(a.neg, a.negMods)), pos = keyWords(modelist::chordText(a.pos, a.posMods));
    switch (t) {
        case ChannelActionType::Off:
            r.kind = MotionKind::Off;
            r.action = s.tr("Off");
            r.chip = s.tr("Off");
            r.symbol = "off";
            break;
        case ChannelActionType::WheelV:
            r.action = s.tr("Scroll");
            r.chip = s.tr("Scroll");
            r.symbol = "scroll";
            break;
        case ChannelActionType::WheelH:
            r.action = s.tr("Scroll sideways");
            r.chip = s.tr("Sideways");
            r.symbol = "scroll_h";
            break;
        case ChannelActionType::Zoom:
            // "Zoom" alone is ambiguous: a Keys action (Ctrl+Plus / Ctrl+Minus) zooms too. This one holds the keys
            // of ZOOM_MODIFIER (read from the settings, they differ per profile) and sends the mouse wheel.
            r.chip = s.tr("Zoom");
            if (zoomModifier == 0) {
                r.action = s.tr("Zoom: mouse wheel");
                r.detail = s.tr("Wheel");
            } else {
                const std::string mods = keyWords(zoomModifierToString(zoomModifier));
                r.action = format(s.tr("Zoom: %1 + mouse wheel"), {mods});
                r.detail = mods + "+" + s.tr("Wheel");
            }
            r.symbol = "zoom";
            break;
        case ChannelActionType::Keys:
            r.action = format(s.tr("Keys: %1 / %2"), {neg, pos});
            r.chip = s.tr("Keys");
            r.detail = neg + " / " + pos;
            r.symbol = "keys";
            break;
        default:  // Step
            r.action = format(s.tr("One tap per push: %1 / %2"), {neg, pos});
            r.chip = s.tr("Taps");
            r.detail = neg + " / " + pos;
            r.symbol = "step";
            break;
    }
    return r;
}

inline MotionRow unusedRow(const char *id, const std::string &name, const std::string &how, const StringProvider &s) {
    MotionRow r;
    r.id = id;
    r.name = name;
    r.how = how;
    r.action = s.tr("Not used");
    r.chip = s.tr("Unused");
    r.symbol = "unused";
    r.kind = MotionKind::NotUsed;
    return r;
}

}  // namespace motion_detail

// The six rows of a mode. `settings` supplies the channel mixes (the mode's own overrides are applied on
// a copy, so a SCROLL_V_MIX override of the mode counts).
inline std::vector<MotionRow> motionSummary(const ModeDef &m, Settings settings, const StringProvider &s) {
    using namespace motion_detail;
    for (const auto &kv : m.overrides) applyKey(settings, utf8::upperAscii(kv.first), kv.second, nullptr);
    struct Motion {
        const char *id;
        std::string name, how;
        Axis axis;
    };
    // the names are literal so that the translation extraction finds them
    const Motion motions[6] = {{"v_push", s.tr("Vertical push"), s.tr("Push forward / back"), AX_Z},
                               {"v_tilt", s.tr("Vertical tilt"), s.tr("Tilt forward / back"), AX_RX},
                               {"h_push", s.tr("Horizontal push"), s.tr("Push sideways"), AX_X},
                               {"h_tilt", s.tr("Horizontal tilt"), s.tr("Tilt sideways"), AX_RZ},
                               {"lift", s.tr("Lift / press"), s.tr("Push down / pull up"), AX_Y},
                               {"twist", s.tr("Twist"), s.tr("Twist the cap"), AX_RY}};
    struct Chan {
        const Mix *mix;
        const ChannelAction *action;
        const SplitChannel *split;
        bool vertical, twist;
    };
    const SplitChannel noSplit;
    const Chan chans[3] = {{&settings.scroll_v.mix, &m.v, &m.vSplit, true, false},
                           {&settings.scroll_h.mix, &m.h, &m.hSplit, false, false},
                           {&settings.zoom.mix, &m.twist, &noSplit, false, true}};
    std::vector<MotionRow> out;
    for (const Motion &mo : motions) {
        bool found = false;
        for (const Chan &c : chans) {
            const bool split = c.split->active();
            // a split channel drops the twist term; its parts take the translation and rotation terms
            if (split && mo.axis == AX_RY) continue;
            if (!hasAxis(*c.mix, mo.axis)) continue;
            const bool tilt = mo.axis == AX_RX || mo.axis == AX_RZ;
            const ChannelAction &a = !split ? *c.action : tilt ? c.split->tiltAction(*c.action) : c.split->pushAction(*c.action);
            out.push_back(rowFor(mo.id, mo.name, mo.how, a, c.vertical, c.twist, settings.zoom_modifier, s));
            found = true;
            break;
        }
        if (!found) out.push_back(unusedRow(mo.id, mo.name, mo.how, s));
    }
    return out;
}

// With the built-in channel mixes (no settings at hand).
inline std::vector<MotionRow> motionSummary(const ModeDef &m, const StringProvider &s) { return motionSummary(m, Settings(), s); }

// The text of one chip: "▬ ↕ Scroll" (motion marker, action symbol, word); a Keys or Step action shows its keys
// instead of the word ("◢ ⌨ Left / Right", "▬ ⌨ Ctrl+Page up / Ctrl+Page down"), because "Keys" alone does not
// say which. Muted rows keep their word ("⊘ Off", "– Unused"), so colour is never the only cue.
inline std::string motionChipText(const MotionRow &r) {
    return motionMarker(r.id) + " " + motionGlyph(r.symbol) + " " + (r.detail.empty() ? r.chip : r.detail);
}

inline std::vector<std::string> motionChipTexts(const std::vector<MotionRow> &rows) {
    std::vector<std::string> out;
    for (const MotionRow &r : rows) out.push_back(motionChipText(r));
    return out;
}

// One line per motion ("Vertical push: Scroll"), for tooltips and the accessible description of a list row.
inline std::string motionSummaryText(const std::vector<MotionRow> &rows, const char *separator = "\n") {
    std::string t;
    for (const MotionRow &r : rows) {
        if (!t.empty()) t += separator;
        t += r.name + ": " + r.action;
    }
    return t;
}

}  // namespace spn::ui
