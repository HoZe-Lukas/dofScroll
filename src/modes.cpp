// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Mode files: parser, action grammar, built-in modes and the bounded directory loader.
// See modes.hpp for the file format and the contract.
#include "modes.hpp"
#include "builtin_library.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>

#include "conf_dir.hpp"
#include "config.hpp"
#include "platform/fs.hpp"
#include "platform/path_rules.hpp"

namespace spn {

namespace {
const char *mouseSideName(uint8_t b) { return b == kMouseLeft ? "click" : b == kMouseMiddle ? "middleclick" : "rightclick"; }

constexpr size_t kMaxModeOverrides = 128;
constexpr int kMaxModeButton = 32;       // highest button number in a mode file (1-based)
constexpr size_t kMaxLabel = 64;
constexpr size_t kMaxDescription = 256;
constexpr size_t kMaxModeLine = 4096;

std::string trim(const std::string &s) { return utf8::trim(s); }
std::string lower(std::string s) { return utf8::lowerAscii(std::move(s)); }
std::string upper(std::string s) { return utf8::upperAscii(std::move(s)); }

bool fail(std::string *err, const std::string &msg) {
    if (err) *err = msg;
    return false;
}

// Display text: control characters dropped, invalid UTF-8 bytes shown as U+FFFD, length capped
// in bytes on a character boundary.
std::string cleanText(const std::string &s, size_t cap) { return utf8::sanitizeDisplay(s, cap); }

ModeDef builtinMode(const std::string &id, const std::string &label, const std::string &desc) {
    ModeDef m;
    m.id = id;
    m.label = label;
    m.description = desc;
    m.builtin = true;
    return m;
}

// Forward declaration: the embedded files are parsed with the same parser as installed files.
bool parseModeText(const std::string &text, const std::string &file, ModeDef *out,
                   std::vector<std::string> *warnings);

// normal, off and timeline are defined here; the rest of the library (slides, media, map,
// precision, tabs, browse) is parsed from the embedded example files, see builtin_library.hpp.
std::vector<ModeDef> builtinModes(std::vector<std::string> *warnings, const BuiltinFile *library) {
    std::vector<ModeDef> v;
    v.push_back(builtinMode("normal", "Normal", "The everyday setting. Push the cap to scroll the window under the pointer (forward/back = up/down, sideways = left/right) and twist it to zoom (Ctrl + mouse wheel). Speed and feel come from your profile."));
    ModeDef off = builtinMode("off", "Off", "Sends nothing: moving the cap does nothing. The buttons of your profile keep working. Use it for a program that reads the SpaceMouse itself (such as Blender) or to pause the axes while keeping the buttons.");
    off.v.type = off.h.type = off.twist.type = ChannelActionType::Off;
    v.push_back(off);
    ModeDef tl = builtinMode("timeline", "Timeline", "For video editors. Push or tilt sideways to scrub through the clip (Left/Right arrow keys; the further you push, the faster). Twist to step single frames. Forward/back does nothing.");
    tl.v.type = ChannelActionType::Off;
    tl.h.type = tl.twist.type = ChannelActionType::Keys;
    tl.h.neg = tl.twist.neg = Key::Left;
    tl.h.pos = tl.twist.pos = Key::Right;
    v.push_back(tl);
    for (const BuiltinFile *f = library ? library : builtinModeFiles(); f && f->id; ++f) {
        bool defined = false;  // the code-defined modes above win over a file of the same id
        for (const ModeDef &e : v)
            if (e.id == f->id) defined = true;
        if (defined) continue;
        ModeDef m;
        m.id = f->id;
        if (!parseModeText(f->text, std::string("built-in mode ") + f->id, &m, warnings)) continue;
        if (m.label.empty()) m.label = m.id;
        m.builtin = true;
        v.push_back(m);
    }
    return v;
}

// Parses one mode file's text into *out (id already set). Problems -> warnings; returns false
// only if the whole file must be rejected (too many overrides).
bool parseModeText(const std::string &text, const std::string &file, ModeDef *out,
                   std::vector<std::string> *warnings) {
    auto warn = [&](size_t line, const std::string &msg) {
        if (warnings && warnings->size() < 200)
            warnings->push_back(file + ":" + std::to_string(line) + ": " + msg);
    };
    size_t pos = 0, lineno = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string raw = text.substr(pos, nl - pos);
        pos = nl + 1;
        lineno++;
        if (raw.find('\0') != std::string::npos) { warn(lineno, "line contains a NUL byte - ignored"); continue; }
        if (lineno == 1 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);
        if (raw.size() > kMaxModeLine) { warn(lineno, "line too long - ignored"); continue; }
        std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (!utf8::isValid(line))
            warn(lineno, "invalid UTF-8 at byte " + std::to_string(utf8::firstInvalid(line) + 1) +
                             " - shown as U+FFFD");
        if (line[0] == '[') { warn(lineno, "sections are not supported in mode files - ignored"); continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { warn(lineno, "line without '=' ignored"); continue; }
        const std::string key = upper(trim(line.substr(0, eq)));
        std::string value = line.substr(eq + 1);
        const std::string tv = trim(value);
        if (!tv.empty() && tv[0] == '#') {
            value.clear();
        } else {
            for (size_t p = 1; p < value.size(); p++)
                if (value[p] == '#' && spn::utf8::isSpace(value[p - 1])) {
                    value.resize(p);
                    break;
                }
        }
        value = trim(value);
        if (key.empty()) { warn(lineno, "line without key ignored"); continue; }

        std::string err;
        if (key == "NAME") {
            out->label = cleanText(value, kMaxLabel);
        } else if (key == "DESCRIPTION") {
            out->description = cleanText(value, kMaxDescription);
        } else if (key == "V_ACTION" || key == "H_ACTION" || key == "TWIST_ACTION") {
            ChannelAction a;
            if (!parseChannelAction(value, &a, &err)) { warn(lineno, key + ": " + err + " - line ignored"); continue; }
            (key == "V_ACTION" ? out->v : key == "H_ACTION" ? out->h : out->twist) = a;
        } else if (key == "LIFT_ACTION") {
            ChannelAction a;
            if (!parseLiftAction(value, &a, &err)) { warn(lineno, key + ": " + err + " - line ignored"); continue; }
            out->lift = a;
        } else if (key == "V_PUSH_ACTION" || key == "V_TILT_ACTION" || key == "H_PUSH_ACTION" || key == "H_TILT_ACTION") {
            ChannelAction a;
            if (!parseChannelAction(value, &a, &err)) { warn(lineno, key + ": " + err + " - line ignored"); continue; }
            SplitChannel &sc = key[0] == 'V' ? out->vSplit : out->hSplit;
            if (key[2] == 'P') { sc.push = a; sc.pushSet = true; }
            else { sc.tilt = a; sc.tiltSet = true; }
        } else if (key == "MODE") {
            warn(lineno, "MODE is not allowed in a mode file - line ignored");
        } else if (key.compare(0, 7, "BUTTON_") == 0) {
            const std::string num = key.substr(7);
            if (num == "0") {
                warn(lineno, key + ": buttons are numbered from 1 - line ignored");
                continue;
            }
            if (num.empty() || num.size() > 2 || std::atoi(num.c_str()) < 1 || num.find_first_not_of("0123456789") != std::string::npos ||
                std::atoi(num.c_str()) > kMaxModeButton) {
                warn(lineno, key + ": button number must be 1.." + std::to_string(kMaxModeButton) + " - line ignored");
                continue;
            }
            ButtonBinding b;
            b.button = std::atoi(num.c_str());
            if (!parseButtonAction(value, &b.type, &b.key, &err, &b.mode, &b.mods)) { warn(lineno, key + ": " + err + " - line ignored"); continue; }
            bool replaced = false;
            for (ButtonBinding &e : out->buttons)
                if (e.button == b.button) { e = b; replaced = true; }
            if (!replaced) out->buttons.push_back(b);
        } else if (key == "ENABLED" || key == "BACKEND" || key == "INPUT" || key == "TICK_HZ") {
            // Process-level settings: a mode must not be able to switch the daemon off or change them.
            warn(lineno, key + " is not allowed in a mode file (global setting) - line ignored");
        } else {
            Settings probe;
            if (!applyKey(probe, key, value, &err)) { warn(lineno, key + ": " + err + " - line ignored"); continue; }
            if (out->overrides.size() >= kMaxModeOverrides) {
                warn(lineno, "too many settings overrides (max. " + std::to_string(kMaxModeOverrides) + ") - file ignored");
                return false;
            }
            out->overrides.emplace_back(key, value);
        }
    }
    return true;
}

bool isModifierKey(Key k) { return k == Key::Ctrl || k == Key::Alt || k == Key::Shift || k == Key::Super; }

// Navigation-like keys: Ctrl+<these> is not a destructive shortcut (Ctrl+arrows, Ctrl+PageUp, the zoom keys Ctrl+plus,
// Ctrl+minus and Ctrl+0).
bool isNavKey(Key k) {
    switch (k) {
        case Key::Up: case Key::Down: case Key::Left: case Key::Right: case Key::Space: case Key::Enter:
        case Key::Esc: case Key::Home: case Key::End: case Key::PageUp: case Key::PageDown: case Key::Minus:
        case Key::Plus: case Key::KpPlus: case Key::KpMinus: case Key::N0:
            return true;
        default:
            return false;
    }
}

// Describes how a mode can press shortcut chords (empty = nothing notable). Loaded anyway.
std::string chordRisk(const ModeDef &m) {
    std::vector<std::string> why;
    for (const ButtonBinding &b : m.buttons)
        if (b.type == ButtonActionType::Key && b.mods.n == 0 && isModifierKey(b.key))
            why.push_back("BUTTON_" + std::to_string(b.button) + " presses " + keyName(b.key));
        else if (b.type == ButtonActionType::Key && chordNeedsWarning(b.key, b.mods))
            why.push_back("BUTTON_" + std::to_string(b.button) + " presses " + keyActionText(b.key, b.mods).substr(4));
    bool hasRiskyKeys = false, hasZoom = false;
    for (const ChannelAction *a : {&m.v, &m.h, &m.twist, &m.lift, &m.vSplit.push, &m.vSplit.tilt, &m.hSplit.push, &m.hSplit.tilt}) {
        if (a->type == ChannelActionType::Zoom) hasZoom = true;
        if (!a->usesKeys()) continue;
        const bool hold = a->type == ChannelActionType::Hold;
        const std::string what = hold ? "a hold: channel holds " : a->type == ChannelActionType::Step ? "a step: channel presses " : "a keys: channel presses ";
        const struct { Key key; const ChordMods &mods; bool none; uint8_t mouse; } args[2] = {{a->neg, a->negMods, a->negNone, a->negMouse},
                                                                                                {a->pos, a->posMods, a->posNone, a->posMouse}};
        for (const auto &arg : args) {
            if (arg.none) continue;
            if (arg.mouse) {  // tier B: a mouse click or a held mouse button
                why.push_back(std::string(hold ? "a hold: channel holds the mouse button (" : "a step: channel clicks the mouse (") + mouseSideName(arg.mouse) + ")");
                continue;
            }
            if (hold) {  // tier B: a held key is never harmless (a stuck letter types, a held modifier changes every click)
                std::string name;
                for (uint8_t i = 0; i < arg.mods.n; ++i) name += std::string(keyName(arg.mods.k[i])) + "+";
                why.push_back(what + name + keyName(arg.key));
                continue;
            }
            if (arg.mods.n == 0) {
                if (!isNavKey(arg.key)) hasRiskyKeys = true;
                if (isModifierKey(arg.key)) why.push_back(what + keyName(arg.key));
                continue;
            }
            // Chords: Ctrl and/or Shift with a navigation key is harmless; Alt/Super or any other key is not.
            const unsigned bits = arg.mods.mask();
            if ((bits & 12u) != 0 || !isNavKey(arg.key)) {
                std::string name;
                for (uint8_t i = 0; i < arg.mods.n; ++i) name += std::string(keyName(arg.mods.k[i])) + "+";
                why.push_back(what + name + keyName(arg.key));
            }
        }
    }
    if (hasRiskyKeys && hasZoom) why.push_back("zoom holds Ctrl while a keys: channel fires non-navigation keys");
    std::string out;
    for (const std::string &w : why) out += (out.empty() ? "" : "; ") + w;
    return out;
}

}  // namespace

const ModeDef *ModeSet::find(const std::string &id) const {
    for (const ModeDef &m : modes)
        if (m.id == id) return &m;
    return nullptr;
}

bool validModeId(const std::string &id) {
    // Reserved Windows device names (con, nul, com1, ...) are refused on every OS: "<id>.conf"
    // would resolve to the device on Windows, and mode directories are copied between systems.
    return !id.empty() && id.size() <= 32 &&
           id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-") == std::string::npos &&
           !isReservedDeviceStem(id);
}

bool parseModeCycleList(const std::string &text, std::vector<std::string> *out, std::string *err,
                        std::vector<std::string> *dups) {
    std::vector<std::string> ids;
    std::string cur;
    auto flush = [&]() -> bool {
        if (cur.empty()) return true;
        const std::string id = lower(cur);
        cur.clear();
        if (!validModeId(id)) return fail(err, "mode id expected ([a-z0-9_-], max. 32 characters), got '" + id + "'");
        if (std::find(ids.begin(), ids.end(), id) != ids.end()) {
            if (dups) dups->push_back(id);
            return true;
        }
        if (ids.size() >= kMaxModeCycle) return fail(err, "too many modes (max. " + std::to_string(kMaxModeCycle) + ")");
        ids.push_back(id);
        return true;
    };
    for (char c : text) {
        if (c == ',' || c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!flush()) return false;
        } else {
            cur += c;
            if (cur.size() > 64) return fail(err, "mode id too long");
        }
    }
    if (!flush()) return false;
    if (ids.empty()) return fail(err, "at least one mode id expected");
    if (out) *out = ids;
    return true;
}

bool splitCycleTarget(const std::string &target, bool *forward, std::vector<std::string> *ids) {
    if (ids) ids->clear();
    const std::string dir = target.substr(0, 4);
    if (dir != "next" && dir != "prev") return false;
    if (target.size() == 4) {
        if (forward) *forward = dir == "next";
        return true;
    }
    if (target[4] != ':') return false;
    std::vector<std::string> list;
    if (!parseModeCycleList(target.substr(5), &list, nullptr)) return false;
    if (forward) *forward = dir == "next";
    if (ids) *ids = list;
    return true;
}

// <chord> = [<modifier>+]... <key>: modifiers (ctrl, shift, alt, super) at most once each, then
// exactly one non-modifier key. A single name (also a lone modifier) is a plain key.
static bool parseChord(const std::string &text, Key *key, ChordMods *mods, std::string *err, bool allowModifiersOnly = false) {
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t pos; (pos = text.find('+', start)) != std::string::npos; start = pos + 1)
        parts.push_back(trim(text.substr(start, pos - start)));
    parts.push_back(trim(text.substr(start)));
    ChordMods m;
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        Key mk;
        if (!parseKeyName(parts[i], &mk)) return fail(err, "unknown key '" + parts[i] + "'");
        if (!isModifierKey(mk)) return fail(err, "'" + parts[i] + "' is not a modifier (ctrl, shift, alt, super)");
        for (uint8_t j = 0; j < m.n; ++j)
            if (m.k[j] == mk) return fail(err, "modifier '" + parts[i] + "' appears twice");
        if (m.n >= kMaxChordMods) return fail(err, "a chord has at most 4 modifiers");
        m.k[m.n++] = mk;
    }
    Key last;
    if (!parseKeyName(parts.back(), &last)) return fail(err, "unknown key '" + parts.back() + "'");
    if (m.n > 0 && isModifierKey(last) && !allowModifiersOnly) return fail(err, "a chord must end with a key that is not a modifier");
    if (m.n > 0 && isModifierKey(last)) {  // hold:ctrl+shift: only modifiers, held together
        for (uint8_t j = 0; j < m.n; ++j)
            if (m.k[j] == last) return fail(err, "modifier '" + parts.back() + "' appears twice");
        if (m.n >= kMaxChordMods) return fail(err, "a chord has at most 4 modifiers");
    }
    *key = last;
    *mods = m;
    return true;
}

namespace {
std::string chordWords(Key key, const ChordMods &mods) {
    std::string t;
    for (uint8_t i = 0; i < mods.n; ++i) t += std::string(keyName(mods.k[i])) + "+";
    return t + keyName(key);
}
}  // namespace

namespace {
std::string sideText(bool none, uint8_t mouse, Key key, const ChordMods &mods) {
    return none ? std::string("none") : mouse ? std::string(mouseSideName(mouse)) : chordWords(key, mods);
}
uint8_t mouseSideOf(const std::string &t) {
    return t == "click" ? kMouseLeft : t == "middleclick" ? kMouseMiddle : t == "rightclick" ? kMouseRight : 0;
}
}  // namespace

std::string channelActionText(const ChannelAction &a) {
    switch (a.type) {
        case ChannelActionType::Default: return "default";
        case ChannelActionType::Off: return "none";
        case ChannelActionType::WheelV: return "wheel_v";
        case ChannelActionType::WheelH: return "wheel_h";
        case ChannelActionType::Zoom: return "zoom";
        case ChannelActionType::Step:
        case ChannelActionType::Keys:
        case ChannelActionType::Hold:
            return std::string(a.type == ChannelActionType::Step ? "step:" : a.type == ChannelActionType::Hold ? "hold:" : "keys:") +
                   sideText(a.negNone, a.negMouse, a.neg, a.negMods) + "," + sideText(a.posNone, a.posMouse, a.pos, a.posMods);
    }
    return "default";
}

bool parseChannelAction(const std::string &text, ChannelAction *out, std::string *err) {
    const std::string t = lower(trim(text));
    ChannelAction a;
    if (t == "default") a.type = ChannelActionType::Default;
    else if (t == "none") a.type = ChannelActionType::Off;
    else if (t == "wheel_v") a.type = ChannelActionType::WheelV;
    else if (t == "wheel_h") a.type = ChannelActionType::WheelH;
    else if (t == "zoom") a.type = ChannelActionType::Zoom;
    else if (t.compare(0, 5, "keys:") == 0 || t.compare(0, 5, "step:") == 0 || t.compare(0, 5, "hold:") == 0) {
        const ChannelActionType ty = t[0] == 'k' ? ChannelActionType::Keys : t[0] == 's' ? ChannelActionType::Step : ChannelActionType::Hold;
        const std::string word = t.substr(0, 4);
        const std::string rest = t.substr(5);
        const size_t comma = rest.find(',');
        if (comma == std::string::npos || rest.find(',', comma + 1) != std::string::npos)
            return fail(err, word + ":<neg>,<pos> expects exactly two sides, each a key, a key chord" +
                                 (ty == ChannelActionType::Keys ? std::string(" or none") : std::string(", a mouse button (click, rightclick, middleclick) or none")));
        // Per side: a key or chord, a mouse button (click, rightclick, middleclick; digital only) or "none" (nothing is done in that direction).
        const std::string negText = trim(rest.substr(0, comma)), posText = trim(rest.substr(comma + 1));
        a.negNone = negText == "none";
        a.posNone = posText == "none";
        a.negMouse = mouseSideOf(negText);
        a.posMouse = mouseSideOf(posText);
        if ((a.negMouse || a.posMouse) && ty == ChannelActionType::Keys)
            return fail(err, "a mouse click can only be used digitally, use step: (one click per push) or hold:");
        if ((!a.negNone && !a.negMouse && !parseChord(negText, &a.neg, &a.negMods, err, ty == ChannelActionType::Hold)) ||
            (!a.posNone && !a.posMouse && !parseChord(posText, &a.pos, &a.posMods, err, ty == ChannelActionType::Hold)))
            return false;
        if (a.negNone && a.posNone) return fail(err, word + ": needs a key on at least one side (use none for the whole action)");
        a.type = ty;
        // Tier rule: an analog repeat of a risky key is refused, one press per push (step:) and a hold are allowed with the load warning.
        if (ty == ChannelActionType::Keys) {
            const struct { bool none; Key key; const ChordMods &mods; } sides[2] = {{a.negNone, a.neg, a.negMods}, {a.posNone, a.pos, a.posMods}};
            for (const auto &side : sides)
                if (!side.none && !keyAnalogAllowed(side.key, side.mods)) return fail(err, "this key can only be used digitally, use step:");
        }
    } else {
        return fail(err, "expected default | none | wheel_v | wheel_h | zoom | keys:<neg>,<pos> | step:<neg>,<pos> | hold:<neg>,<pos> (keys may be chords like ctrl+pageup; step: and hold: also take click, rightclick, middleclick)");
    }
    if (out) *out = a;
    return true;
}

bool parseLiftAction(const std::string &text, ChannelAction *out, std::string *err) {
    ChannelAction a;
    if (!parseChannelAction(text, &a, err)) return false;
    if (a.type == ChannelActionType::Keys)
        return fail(err, "an analog keys: action cannot be used on the lift (it fires when the cap is grabbed); use step:<neg>,<pos>");
    if (out) *out = a;
    return true;
}

bool keyAnalogAllowed(Key key, const ChordMods &mods) {
    switch (key) {
        case Key::Up: case Key::Down: case Key::Left: case Key::Right: case Key::PageUp: case Key::PageDown: case Key::Home:
        case Key::End: case Key::Tab: case Key::Plus: case Key::Minus: case Key::KpPlus: case Key::KpMinus:
            return (mods.mask() & 12u) == 0;  // ctrl and shift only: alt and super chords are tier B
        case Key::VolumeUp: case Key::VolumeDown: case Key::MediaNext: case Key::MediaPrev:
            return mods.n == 0;
        default:
            return false;
    }
}

bool chordNeedsWarning(Key key, const ChordMods &mods) { return mods.n > 0 && ((mods.mask() & 12u) != 0 || !isNavKey(key)); }

std::string keyActionText(Key key, const ChordMods &mods) {
    std::string s = "key:";
    for (uint8_t i = 0; i < mods.n; ++i) s += std::string(keyName(mods.k[i])) + "+";
    return s + keyName(key);
}

bool parseButtonAction(const std::string &text, ButtonActionType *type, Key *key, std::string *err,
                       std::string *modeId, ChordMods *mods) {
    if (mods) *mods = ChordMods();
    const std::string t = lower(trim(text));
    ButtonActionType ty;
    Key k = Key::Space;
    if (t == "none") ty = ButtonActionType::Unbound;
    else if (t == "click") ty = ButtonActionType::Click;
    else if (t == "rightclick") ty = ButtonActionType::RightClick;
    else if (t == "middleclick") ty = ButtonActionType::MiddleClick;
    else if (t == "nav") ty = ButtonActionType::Nav;
    else if (t == "toggle:nav") ty = ButtonActionType::ToggleNav;
    else if (t.compare(0, 12, "toggle:mode:") == 0) {
        const std::string id = trim(t.substr(12));
        if (!validModeId(id) || id == "next" || id == "prev")
            return fail(err, "toggle:mode:<id> needs a mode id ([a-z0-9_-], max. 32 characters), not 'next' or 'prev'");
        ty = ButtonActionType::ToggleMode;
        if (modeId) *modeId = id;
    }
    else if (t.compare(0, 10, "hold:mode:") == 0) {
        const std::string id = trim(t.substr(10));
        if (!validModeId(id) || id == "next" || id == "prev")
            return fail(err, "hold:mode:<id> needs a mode id ([a-z0-9_-], max. 32 characters), not 'next' or 'prev'");
        ty = ButtonActionType::HoldMode;
        if (modeId) *modeId = id;
    }
    else if (t.compare(0, 11, "buttonmode:") == 0) {
        const std::string id = trim(t.substr(11));
        if (!validModeId(id)) return fail(err, "buttonmode:<id> needs a button mode id ([a-z0-9_-], max. 32 characters)");
        ty = ButtonActionType::ButtonMode;
        if (modeId) *modeId = id;
    }
    else if (t.compare(0, 4, "key:") == 0) {
        const std::string name = trim(t.substr(4));
        ChordMods m;
        if (name.find('+') == std::string::npos) {
            if (!parseKeyName(name, &k)) return fail(err, "unknown key '" + name + "'");
        } else if (!parseChord(name, &k, &m, err)) {
            return false;
        }
        if (mods) *mods = m;
        ty = ButtonActionType::Key;
    } else if (t.compare(0, 5, "mode:") == 0) {
        std::string id = trim(t.substr(5));
        if (id.compare(0, 5, "next:") == 0 || id.compare(0, 5, "prev:") == 0) {
            std::vector<std::string> list;
            std::string e;
            if (!parseModeCycleList(id.substr(5), &list, &e)) return fail(err, "mode:" + id.substr(0, 4) + ":<ids>: " + e);
            id.resize(5);
            for (size_t i = 0; i < list.size(); i++) id += (i ? "," : "") + list[i];
            if (modeId) *modeId = id;
            if (type) *type = ButtonActionType::ModeSwitch;
            if (key) *key = k;
            return true;
        }
        if (!validModeId(id)) return fail(err, "mode:<id> needs a mode id ([a-z0-9_-], max. 32 characters) or 'next'");
        ty = ButtonActionType::ModeSwitch;
        if (modeId) *modeId = id;
    } else {
        return fail(err, "expected none | click | rightclick | middleclick | nav | key:<name> | key:<mod>+<key> | mode:<id> | mode:next | mode:prev | toggle:nav | toggle:mode:<id> | hold:mode:<id> | buttonmode:<id>");
    }
    if (type) *type = ty;
    if (key) *key = k;
    return true;
}

std::string modesDir() {
    // legacyConfigPath() is "<config base dir>/spnav-scroll.conf"; strip the file name.
    const std::string p = legacyConfigPath();
    const size_t slash = p.rfind('/');
    const std::string base = slash == std::string::npos ? "." : p.substr(0, slash);
    return base + "/dofscroll/modes";
}

ModeSet loadModes(const std::string &dir, std::vector<std::string> *warnings, const BuiltinFile *library) {
    ModeSet set;
    set.modes = builtinModes(warnings, library);
    auto warnAll = [&](const std::string &m) {
        if (warnings && warnings->size() < 200) warnings->push_back(m);
    };

    for (const confdir::Entry &file : confdir::readConfDir(dir, "mode files", warnAll)) {
        const std::string &path = file.path;
        const std::string &text = file.text;
        // A file identical to a shipped example (the built-in text, or the copy an earlier version
        // made on first start) is that built-in, not the user's own file: it is not listed again.
        if (matchesBuiltinText(library ? library : builtinModeFiles(), file.id, text) || matchesBuiltinText(legacyModeFiles(), file.id, text))
            continue;
        ModeDef m;
        m.id = file.id;
        if (!parseModeText(text, path, &m, warnings)) continue;
        if (m.label.empty()) m.label = m.id;
        const std::string risk = chordRisk(m);
        if (!risk.empty()) warnAll("mode " + m.id + " can press shortcut chords (" + risk + ") - read the file before use");
        bool replaced = false;
        for (ModeDef &e : set.modes)
            if (e.id == m.id) { e = m; replaced = true; break; }
        if (!replaced) set.modes.push_back(m);
    }
    // Mode switch targets can only be checked once all files are loaded.
    for (ModeDef &m : set.modes) {
        auto &bs = m.buttons;
        for (size_t i = 0; i < bs.size();) {
            const bool targets = bs[i].type == ButtonActionType::ModeSwitch || bs[i].type == ButtonActionType::ToggleMode ||
                                 bs[i].type == ButtonActionType::HoldMode;
            if (targets && !splitCycleTarget(bs[i].mode, nullptr, nullptr) && !set.find(bs[i].mode)) {
                warnAll("mode " + m.id + ": BUTTON_" + std::to_string(bs[i].button) + ": unknown mode '" +
                        bs[i].mode + "' - binding ignored");
                bs.erase(bs.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }
    }
    return set;
}

std::string modesStamp(const std::string &dir) { return confdir::stamp(dir, "modes"); }

}  // namespace spn
