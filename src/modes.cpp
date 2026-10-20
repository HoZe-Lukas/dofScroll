// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Mode files: parser, action grammar, built-in modes and the bounded directory loader.
// See modes.hpp for the file format and the contract.
#include "modes.hpp"
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

std::vector<ModeDef> builtinModes() {
    std::vector<ModeDef> v;
    v.push_back(builtinMode("normal", "Normal", "Scroll, zoom and navigation as configured"));
    ModeDef off = builtinMode("off", "Off", "Sends nothing");
    off.v.type = off.h.type = off.twist.type = ChannelActionType::Off;
    v.push_back(off);
    ModeDef tl = builtinMode("timeline", "Timeline", "Sideways = scrub, twist = single frames");
    tl.v.type = ChannelActionType::Off;
    tl.h.type = tl.twist.type = ChannelActionType::Keys;
    tl.h.neg = tl.twist.neg = Key::Left;
    tl.h.pos = tl.twist.pos = Key::Right;
    v.push_back(tl);
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
            if (!parseButtonAction(value, &b.type, &b.key, &err, &b.mode)) { warn(lineno, key + ": " + err + " - line ignored"); continue; }
            bool replaced = false;
            for (ButtonBinding &e : out->buttons)
                if (e.button == b.button) { e = b; replaced = true; }
            if (!replaced) out->buttons.push_back(b);
        } else if (key == "ENABLED" || key == "BACKEND" || key == "TICK_HZ") {
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

// Navigation-like keys: Ctrl+<these> is not a destructive shortcut (Ctrl+arrows, Ctrl+PageUp, Ctrl+plus).
bool isNavKey(Key k) {
    switch (k) {
        case Key::Up: case Key::Down: case Key::Left: case Key::Right: case Key::Space: case Key::Enter:
        case Key::Esc: case Key::Home: case Key::End: case Key::PageUp: case Key::PageDown: case Key::Minus:
        case Key::Plus: case Key::KpPlus: case Key::KpMinus:
            return true;
        default:
            return false;
    }
}

// Describes how a mode can press shortcut chords (empty = nothing notable). Loaded anyway.
std::string chordRisk(const ModeDef &m) {
    std::vector<std::string> why;
    for (const ButtonBinding &b : m.buttons)
        if (b.type == ButtonActionType::Key && isModifierKey(b.key))
            why.push_back("BUTTON_" + std::to_string(b.button) + " presses " + keyName(b.key));
    bool hasRiskyKeys = false, hasZoom = false;
    for (const ChannelAction *a : {&m.v, &m.h, &m.twist}) {
        if (a->type == ChannelActionType::Zoom) hasZoom = true;
        if (!a->usesKeys()) continue;
        const std::string what = a->type == ChannelActionType::Step ? "a step: channel presses " : "a keys: channel presses ";
        const struct { Key key; const ChordMods &mods; } args[2] = {{a->neg, a->negMods}, {a->pos, a->posMods}};
        for (const auto &arg : args) {
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

// <chord> = [<modifier>+]... <key>: modifiers (ctrl, shift, alt, super) at most once each, then
// exactly one non-modifier key. A single name (also a lone modifier) is a plain key.
static bool parseChord(const std::string &text, Key *key, ChordMods *mods, std::string *err) {
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
    if (m.n > 0 && isModifierKey(last)) return fail(err, "a chord must end with a key that is not a modifier");
    *key = last;
    *mods = m;
    return true;
}

bool parseChannelAction(const std::string &text, ChannelAction *out, std::string *err) {
    const std::string t = lower(trim(text));
    ChannelAction a;
    if (t == "default") a.type = ChannelActionType::Default;
    else if (t == "none") a.type = ChannelActionType::Off;
    else if (t == "wheel_v") a.type = ChannelActionType::WheelV;
    else if (t == "wheel_h") a.type = ChannelActionType::WheelH;
    else if (t == "zoom") a.type = ChannelActionType::Zoom;
    else if (t.compare(0, 5, "keys:") == 0 || t.compare(0, 5, "step:") == 0) {
        const bool step = t[0] == 's';
        const std::string rest = t.substr(5);
        const size_t comma = rest.find(',');
        if (comma == std::string::npos || rest.find(',', comma + 1) != std::string::npos)
            return fail(err, std::string(step ? "step" : "keys") + ":<neg>,<pos> expects exactly two keys or key chords");
        if (!parseChord(trim(rest.substr(0, comma)), &a.neg, &a.negMods, err) ||
            !parseChord(trim(rest.substr(comma + 1)), &a.pos, &a.posMods, err))
            return false;
        a.type = step ? ChannelActionType::Step : ChannelActionType::Keys;
    } else {
        return fail(err, "expected default | none | wheel_v | wheel_h | zoom | keys:<neg>,<pos> | step:<neg>,<pos> (keys may be chords like ctrl+pageup)");
    }
    if (out) *out = a;
    return true;
}

bool parseButtonAction(const std::string &text, ButtonActionType *type, Key *key, std::string *err,
                       std::string *modeId) {
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
        if (!parseKeyName(name, &k)) return fail(err, "unknown key '" + name + "'");
        ty = ButtonActionType::Key;
    } else if (t.compare(0, 5, "mode:") == 0) {
        const std::string id = trim(t.substr(5));
        if (!validModeId(id)) return fail(err, "mode:<id> needs a mode id ([a-z0-9_-], max. 32 characters) or 'next'");
        ty = ButtonActionType::ModeSwitch;
        if (modeId) *modeId = id;
    } else {
        return fail(err, "expected none | click | rightclick | middleclick | nav | key:<name> | mode:<id> | mode:next | mode:prev | toggle:nav | toggle:mode:<id> | hold:mode:<id> | buttonmode:<id>");
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

ModeSet loadModes(const std::string &dir, std::vector<std::string> *warnings) {
    ModeSet set;
    set.modes = builtinModes();
    auto warnAll = [&](const std::string &m) {
        if (warnings && warnings->size() < 200) warnings->push_back(m);
    };

    for (const confdir::Entry &file : confdir::readConfDir(dir, "mode files", warnAll)) {
        const std::string &path = file.path;
        const std::string &text = file.text;
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
            if (targets && bs[i].mode != "next" && bs[i].mode != "prev" && !set.find(bs[i].mode)) {
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
