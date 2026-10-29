// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The big action tiles of the visual joystick mode editor (issue #143, toolkit-free): a short list of ready
// choices a person understands at a glance, like the action palette of a stream deck, instead of the seven kinds
// of the file format. Choosing a tile gives the selected motion a ChannelForm; the tile that is lit is the one
// that matches the motion's action now. "Keys" is the way to anything else (any key or chord per direction),
// "One key tap per push" is the step action.
//
//   presets(motion, strings)            // the tiles for a motion, in a fixed order
//   presetForm(preset, motion, current) // the ChannelForm a tile writes
//   activePreset(motion, form)          // the id of the tile that matches, "keys" when none does
#pragma once
#include <string>
#include <vector>

#include "ui/model/motion_map_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct ActionPreset {
    std::string id;     // "scroll", "scroll_h", "zoom", "pages", "arrows", "keys", "taps", "off", "default"
    std::string label;  // "Scroll up and down"
    std::string glyph;  // the symbol of the tile (symbols that common fonts have)
    std::string tip;    // one sentence
};

// The tiles for every motion but lift. The same list for all motions; what a tile writes can depend on the
// motion (arrow keys: up and down for the vertical motions, left and right for the others).
inline std::vector<ActionPreset> actionPresets(const StringProvider &s) {
    return {{"scroll", s.tr("Scroll up and down"), motionGlyph("scroll"), s.tr("Turns the vertical mouse wheel")},
            {"scroll_h", s.tr("Scroll sideways"), motionGlyph("scroll_h"), s.tr("Turns the horizontal mouse wheel")},
            {"zoom", s.tr("Zoom"), motionGlyph("zoom"), s.tr("Zooms: the mouse wheel with the zoom keys held")},
            {"pages", s.tr("Page up / Page down"), "\xE2\x87\x85", s.tr("Presses Page up and Page down")},
            {"arrows", s.tr("Arrow keys"), "\xE2\x9C\xA5", s.tr("Presses the arrow keys")},
            {"keys", s.tr("Other keys"), motionGlyph("keys"), s.tr("Presses any key or key combination for each direction")},
            {"taps", s.tr("One key tap per push"), motionGlyph("step"), s.tr("One key press for each push or tilt")},
            {"click", s.tr("Mouse click per push"), "\xE2\x97\x89", s.tr("One mouse click for each push or tilt")},
            {"hold", s.tr("Hold a key or button while pushed"), "\xE2\x97\x86", s.tr("Holds a key, a key combination or a mouse button while you push, and lets go when you ease back")},
            {"off", s.tr("Do nothing"), motionGlyph("off"), s.tr("This motion has no effect in this joystick mode")},
            {"default", s.tr("As in the profile"), "\xE2\x86\xBA", s.tr("Scroll and zoom as the profile says")}};
}

namespace preset_detail {
inline bool isHorizontalLike(Motion m) { return m == Motion::HPush || m == Motion::HTilt || m == Motion::Twist; }
inline ChannelForm keys(ChannelActionType t, const char *neg, const char *pos) {
    ChannelForm c;
    c.type = t;
    c.neg = neg;
    c.pos = pos;
    return c;
}
}  // namespace preset_detail

// What the tile writes. "keys" and "taps" keep the keys the motion has now (up / down when it has none).
inline ChannelForm presetForm(const std::string &id, Motion m, const ChannelForm &current) {
    using namespace preset_detail;
    ChannelForm c;
    if (id == "scroll") c.type = ChannelActionType::WheelV;
    else if (id == "scroll_h") c.type = ChannelActionType::WheelH;
    else if (id == "zoom") c.type = ChannelActionType::Zoom;
    else if (id == "pointer_x") c.type = ChannelActionType::PointerX;
    else if (id == "pointer_y") c.type = ChannelActionType::PointerY;
    else if (id == "pages") c = keys(ChannelActionType::Keys, "pageup", "pagedown");
    else if (id == "arrows") c = isHorizontalLike(m) ? keys(ChannelActionType::Keys, "left", "right") : keys(ChannelActionType::Keys, "up", "down");
    else if (id == "keys" || id == "taps") {
        const bool had = current.usesKeys() && !current.neg.empty() && !current.pos.empty();
        c = keys(id == "keys" ? ChannelActionType::Keys : ChannelActionType::Step, had ? current.neg.c_str() : "up", had ? current.pos.c_str() : "down");
    } else if (id == "click") c = keys(ChannelActionType::Step, "none", "click");
    else if (id == "hold") c = keys(ChannelActionType::Hold, "none", "click");
    else if (id == "off") c.type = ChannelActionType::Off;
    else c.type = ChannelActionType::Default;
    return c;
}

// The tile that is lit for the action: the exact preset when there is one, else "keys" or "taps".
inline std::string activePreset(Motion m, const ChannelForm &f) {
    switch (f.type) {
        case ChannelActionType::Default: return "default";
        case ChannelActionType::Off: return "off";
        case ChannelActionType::WheelV: return "scroll";
        case ChannelActionType::WheelH: return "scroll_h";
        case ChannelActionType::Zoom: return "zoom";
        case ChannelActionType::PointerX: return "pointer_x";
        case ChannelActionType::PointerY: return "pointer_y";
        case ChannelActionType::Step: return f.anyMouse() && !f.anyKeySide() ? "click" : "taps";
        case ChannelActionType::Hold: return "hold";
        case ChannelActionType::Keys:
            if (f.neg == "pageup" && f.pos == "pagedown") return "pages";
            if (presetForm("arrows", m, f) == f) return "arrows";
            return "keys";
    }
    return "default";
}

}  // namespace spn::ui
