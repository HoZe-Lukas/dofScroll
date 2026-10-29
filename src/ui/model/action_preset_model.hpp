// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The big action tiles of the visual joystick mode editor (issue #143, toolkit-free): a short list of ready
// choices a person understands at a glance, like the action palette of a stream deck, instead of the seven kinds
// of the file format. Choosing a tile gives the selected motion a ChannelForm; the tile that is lit is the one
// that matches the motion's action now. "Keys" is the way to anything else (any key or chord per direction),
// "One key tap per push" is the step action.
//
// The ready choices "Page up / Page down", "Switch tabs", "Volume" and "Change track" are not written down a second
// time: what they write is the action of the built-in example mode (examples/modes/slides.conf, tabs.conf, volume.conf,
// tracks.conf, read from the embedded library), so a tile and its example mode cannot drift apart
// (tests/test_ui_action_presets.cpp checks it). On lift they write the digital form (step:) of the same keys.
//
//   presets(motion, strings)            // the tiles for a motion, in a fixed order
//   presetForm(preset, motion, current) // the ChannelForm a tile writes
//   activePreset(motion, form)          // the id of the tile that matches, "keys" when none does
#pragma once
#include <string>
#include <vector>

#include "builtin_library.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

struct ActionPreset {
    std::string id;     // "scroll", "scroll_h", "zoom", "pages", "arrows", "tabs", "volume", "tracks", "keys", "taps", "click", "hold", "off", "default"
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
            {"tabs", s.tr("Switch tabs"), "\xE2\x87\x84", s.tr("Presses Ctrl+Page up and Ctrl+Page down: previous and next tab in browsers and terminals")},
            {"volume", s.tr("Volume"), "\xE2\x99\xAA", s.tr("Presses Volume up and Volume down (the volume keys are not sent on macOS)")},
            {"tracks", s.tr("Change track"), "\xE2\x99\xAB", s.tr("One press of Previous track or Next track for each push (the media keys are not sent on macOS)")},
            {"keys", s.tr("Other keys"), motionGlyph("keys"), s.tr("Presses any key or key combination for each direction")},
            {"taps", s.tr("One key tap per push"), motionGlyph("step"), s.tr("One key press for each push or tilt")},
            {"click", s.tr("Mouse click per push"), "\xE2\x97\x89", s.tr("One mouse click for each push or tilt")},
            {"hold", s.tr("Hold a key or button while pushed"), "\xE2\x97\x86", s.tr("Holds a key, a key combination or a mouse button while you push, and lets go when you ease back")},
            {"off", s.tr("Do nothing"), motionGlyph("off"), s.tr("This motion has no effect in this joystick mode")},
            {"default", s.tr("As in the profile"), "\xE2\x86\xBA", s.tr("Scroll and zoom as the profile says")}};
}

namespace preset_detail {
// The tiles made from a built-in example mode: tile id, mode id. The action is the first one of the file that does
// something (not default, not none).
struct ExampleSource {
    const char *preset, *mode;
};
inline const ExampleSource *exampleSources() {
    static const ExampleSource t[] = {{"pages", "slides"}, {"tabs", "tabs"}, {"volume", "volume"}, {"tracks", "tracks"}, {nullptr, nullptr}};
    return t;
}
inline const char *exampleModeOf(const std::string &preset) {
    for (const ExampleSource *e = exampleSources(); e->preset; ++e)
        if (preset == e->preset) return e->mode;
    return nullptr;
}
// "keys:a,b" / "step:a,b" -> ChannelForm. Chords have no comma, so the first one separates the sides.
inline bool formFromText(const std::string &text, ChannelForm *out) {
    ChannelForm c;
    size_t colon = text.find(':');
    if (colon == std::string::npos) return false;
    const std::string kind = text.substr(0, colon);
    c.type = kind == "keys" ? ChannelActionType::Keys : kind == "step" ? ChannelActionType::Step : ChannelActionType::Hold;
    if (kind != "keys" && kind != "step" && kind != "hold") return false;
    const size_t comma = text.find(',', colon);
    if (comma == std::string::npos) return false;
    c.neg = text.substr(colon + 1, comma - colon - 1);
    c.pos = text.substr(comma + 1);
    *out = c;
    return true;
}
// The action of a built-in example mode that does something (keys:, step: or hold:), as the file says it.
inline bool exampleAction(const std::string &modeId, ChannelForm *out) {
    for (const BuiltinFile *f = builtinModeFiles(); f && f->id; ++f) {
        if (modeId != f->id) continue;
        const std::string text = f->text;
        size_t pos = 0;
        while (pos < text.size()) {
            size_t end = text.find('\n', pos);
            if (end == std::string::npos) end = text.size();
            std::string line = text.substr(pos, end - pos);
            pos = end + 1;
            const size_t eq = line.find('=');
            if (line.empty() || line[0] == '#' || eq == std::string::npos || eq < 8 || line.compare(eq - 8, 8, "_ACTION ") != 0) continue;
            std::string v = line.substr(eq + 1);
            while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.erase(v.begin());
            while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\t')) v.pop_back();
            ChannelForm c;
            if (formFromText(v, &c)) {
                *out = c;
                return true;
            }
        }
    }
    return false;
}
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
    else if (const char *mode = exampleModeOf(id)) {
        if (!exampleAction(mode, &c)) c.type = ChannelActionType::Default;  // cannot happen: the test needs every example
    }
    else if (id == "arrows") c = isHorizontalLike(m) ? keys(ChannelActionType::Keys, "left", "right") : keys(ChannelActionType::Keys, "up", "down");
    else if (id == "keys" || id == "taps") {
        const bool had = current.usesKeys() && !current.neg.empty() && !current.pos.empty();
        c = keys(id == "keys" ? ChannelActionType::Keys : ChannelActionType::Step, had ? current.neg.c_str() : "up", had ? current.pos.c_str() : "down");
    } else if (id == "click") c = keys(ChannelActionType::Step, "none", "click");
    else if (id == "hold") c = keys(ChannelActionType::Hold, "none", "click");
    else if (id == "off") c.type = ChannelActionType::Off;
    else c.type = ChannelActionType::Default;
    if (m == Motion::Lift && c.type == ChannelActionType::Keys) c.type = ChannelActionType::Step;  // the lift is never analog (it fires when grabbed)
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
        case ChannelActionType::Step:
            if (presetForm("tracks", m, f) == f) return "tracks";
            if (m == Motion::Lift)
                for (const char *id : {"pages", "tabs", "volume"})
                    if (presetForm(id, m, f) == f) return id;
            return f.anyMouse() && !f.anyKeySide() ? "click" : "taps";
        case ChannelActionType::Hold: return "hold";
        case ChannelActionType::Keys:
            for (const char *id : {"pages", "tabs", "volume"})
                if (presetForm(id, m, f) == f) return id;
            if (presetForm("arrows", m, f) == f) return "arrows";
            return "keys";
    }
    return "default";
}

}  // namespace spn::ui
