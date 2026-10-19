// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Editor logic for one button binding (BUTTON_<n>): splits the action text into a kind plus an
// argument (key name or mode id) and composes it back, and lists the choices for the view.
// Actions: none, click, rightclick, middleclick, nav, key:<name>, mode:<id>, mode:next.
#pragma once
#include <cctype>
#include <string>
#include <vector>

#include "key_list.hpp"
#include "modes.hpp"
#include "ui/model/settings_model.hpp"

namespace spn::ui {

enum class BindingKind { None, Click, RightClick, MiddleClick, Nav, Key, Mode, ModeNext };

struct BindingKindInfo {
    BindingKind kind;
    const char *prefix;  // action text, or "key" / "mode" when an argument follows
    const char *label;   // English source string for the view
    bool needsKey;       // argument is a key name
    bool needsMode;      // argument is a mode id
    bool advanced;       // plain key/mouse mapping: better done in the vendor driver, shown behind "Advanced"
};

inline const std::vector<BindingKindInfo> &bindingKinds() {
    // Primary: what the vendor driver cannot do (mode switching, navigation mode).
    // Advanced: plain clicks and keys; supported by config and daemon, usually better set in the driver.
    static const std::vector<BindingKindInfo> k = {
        {BindingKind::None, "none", "Nothing", false, false, false},
        {BindingKind::Nav, "nav", "Navigation mode (arrow keys while held)", false, false, false},
        {BindingKind::Mode, "mode", "Switch to mode", false, true, false},
        {BindingKind::ModeNext, "mode:next", "Next mode", false, false, false},
        {BindingKind::Click, "click", "Left click", false, false, true},
        {BindingKind::RightClick, "rightclick", "Right click", false, false, true},
        {BindingKind::MiddleClick, "middleclick", "Middle click", false, false, true},
        {BindingKind::Key, "key", "Press key", true, false, true}};
    return k;
}

// Where the bindings are edited. Global ([global], the default mode): a mapping that is the same
// everywhere is usually better set in the vendor driver, so plain key/click actions sit behind
// "Advanced". Specific (device or application profile): bindings that only apply there are a
// dofscroll feature, so every action is first-class and no hint or warning is shown.
enum class BindingContext { Global, Specific };

inline BindingContext bindingContextOf(size_t section) {
    return section == 0 ? BindingContext::Global : BindingContext::Specific;
}
// The collapsed "Advanced" section (with the vendor-driver hint) exists in the global context only.
inline bool hasAdvancedSection(BindingContext c) { return c == BindingContext::Global; }

inline bool isAdvancedKind(BindingKind kind) {
    for (const BindingKindInfo &k : bindingKinds())
        if (k.kind == kind) return k.advanced;
    return false;
}
inline bool isAdvancedIn(BindingContext c, BindingKind kind) {
    return c == BindingContext::Global && isAdvancedKind(kind);
}

// Kinds to offer: Specific: all. Global: the primary ones, plus the advanced ones when the
// Advanced section is open. Primary kinds always come first.
inline std::vector<BindingKindInfo> bindingKindsFor(BindingContext c, bool advancedOpen) {
    std::vector<BindingKindInfo> out;
    for (const BindingKindInfo &k : bindingKinds())
        if (c == BindingContext::Specific || advancedOpen || !k.advanced) out.push_back(k);
    return out;
}

struct Binding {
    BindingKind kind = BindingKind::None;
    std::string arg;  // key name (Key) or mode id (Mode), else empty
};

inline std::string lowerTrim(const std::string &s) {
    const size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    std::string t = a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
    for (char &c : t) c = char(std::tolower(static_cast<unsigned char>(c)));
    return t;
}

// Parses an action text; false for text that is not a valid action (*out then "none").
inline bool parseBinding(const std::string &text, Binding *out) {
    *out = Binding();
    const std::string t = lowerTrim(text);
    for (const BindingKindInfo &k : bindingKinds()) {
        if (!k.needsKey && !k.needsMode && t == k.prefix) {
            out->kind = k.kind;
            return true;
        }
    }
    if (t.rfind("key:", 0) == 0) {
        Key key;
        if (!parseKeyName(t.substr(4), &key)) return false;
        out->kind = BindingKind::Key;
        out->arg = keyName(key);
        return true;
    }
    if (t.rfind("mode:", 0) == 0) {
        const std::string id = t.substr(5);
        if (!validModeId(id)) return false;
        out->kind = BindingKind::Mode;
        out->arg = id;
        return true;
    }
    return false;
}

// Action text; empty if the binding is incomplete (key/mode without an argument).
inline std::string composeBinding(const Binding &b) {
    for (const BindingKindInfo &k : bindingKinds()) {
        if (k.kind != b.kind) continue;
        if (k.needsKey || k.needsMode)
            return b.arg.empty() ? std::string() : std::string(k.prefix) + ":" + b.arg;
        return k.prefix;
    }
    return std::string();
}

// The binding after the user switches the kind: keeps a still-fitting argument, else a default
// ("space" for keys, the first offered mode for "Switch to mode").
inline Binding withKind(const Binding &cur, BindingKind kind, const std::vector<std::string> &modeIds) {
    Binding b;
    b.kind = kind;
    if (kind == BindingKind::Key) b.arg = cur.kind == BindingKind::Key && !cur.arg.empty() ? cur.arg : "space";
    if (kind == BindingKind::Mode) {
        if (cur.kind == BindingKind::Mode) b.arg = cur.arg;
        if (b.arg.empty() && !modeIds.empty()) b.arg = modeIds.front();
    }
    return b;
}

// Mode ids offered by "Switch to mode": every loaded mode (built-in and files).
inline std::vector<std::string> bindingModeIds(const ModeSet &ms) {
    std::vector<std::string> out;
    for (const ModeDef &m : ms.modes) out.push_back(m.id);
    return out;
}

struct BindingRow {
    int number = 0;
    Binding binding;
    std::string text;         // raw action text
    bool overridden = false;  // set in this section
    bool beyondLimit = false; // bound above the device's button count: shown read-only
    bool valid = true;        // text parsed
};

inline std::vector<BindingRow> bindingRows(const SettingsModel &m, size_t section) {
    std::vector<BindingRow> out;
    for (const SettingsModel::ButtonRow &r : m.buttonRows(section)) {
        BindingRow x;
        x.number = r.number;
        x.text = r.action;
        x.valid = parseBinding(r.action, &x.binding);
        x.overridden = r.overridden;
        x.beyondLimit = r.beyondLimit;
        out.push_back(x);
    }
    return out;
}

// True when any bound row uses an advanced action (the view then starts with that section open).
inline bool anyAdvancedBinding(const std::vector<BindingRow> &rows,
                               BindingContext c = BindingContext::Global) {
    for (const BindingRow &r : rows)
        if (r.valid && isAdvancedIn(c, r.binding.kind)) return true;
    return false;
}

// Applies an edited binding; an incomplete binding is refused with a message.
inline bool applyBinding(SettingsModel &m, size_t section, int number, const Binding &b, std::string *err) {
    const std::string text = composeBinding(b);
    if (text.empty()) {
        if (err) *err = "choose a key or a mode first";
        return false;
    }
    return m.setButton(section, number, text, err);
}

}  // namespace spn::ui
