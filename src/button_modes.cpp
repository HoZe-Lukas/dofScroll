// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "button_modes.hpp"

#include "builtin_library.hpp"
#include "conf_dir.hpp"
#include "utf8.hpp"

namespace spn {

namespace {

constexpr size_t kMaxLine = 4096;
constexpr size_t kMaxLabel = 64;
constexpr size_t kMaxDescription = 256;

std::string trim(const std::string &s) { return utf8::trim(s); }

std::string joinIds(const std::vector<std::string> &ids) {
    std::string r;
    for (size_t i = 0; i < ids.size(); i++) r += (i ? "," : "") + ids[i];
    return r;
}

}  // namespace

const ButtonMode *ButtonModeSet::find(const std::string &id) const {
    for (const ButtonMode &m : modes)
        if (m.id == id) return &m;
    return nullptr;
}

std::string buttonModesDir() {
    const std::string p = modesDir();  // <base>/dofscroll/modes
    const size_t slash = p.rfind('/');
    return (slash == std::string::npos ? std::string(".") : p.substr(0, slash)) + "/buttons";
}

bool parseButtonModeText(const std::string &text, ButtonMode *out, std::vector<std::string> *warnings) {
    auto warn = [&](size_t line, const std::string &msg) {
        if (warnings && warnings->size() < 200)
            warnings->push_back(out->path + ":" + std::to_string(line) + ": " + msg);
    };
    bool haveAction = false;
    size_t cycleLine = 0;
    std::vector<std::string> cycle;
    size_t pos = 0, lineno = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string raw = text.substr(pos, nl - pos);
        pos = nl + 1;
        lineno++;
        if (raw.find('\0') != std::string::npos) { warn(lineno, "line contains a NUL byte - ignored"); continue; }
        if (lineno == 1 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);
        if (raw.size() > kMaxLine) { warn(lineno, "line too long - ignored"); continue; }
        const std::string line = trim(raw);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line[0] == '[') { warn(lineno, "sections are not supported in button mode files - ignored"); continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { warn(lineno, "line without '=' ignored"); continue; }
        const std::string key = utf8::upperAscii(trim(line.substr(0, eq)));
        std::string value = line.substr(eq + 1);
        const std::string tv = trim(value);
        if (!tv.empty() && tv[0] == '#') {
            value.clear();
        } else {
            for (size_t p = 1; p < value.size(); p++)
                if (value[p] == '#' && utf8::isSpace(value[p - 1])) {
                    value.resize(p);
                    break;
                }
        }
        value = trim(value);
        if (key == "NAME") {
            out->label = utf8::sanitizeDisplay(value, kMaxLabel);
        } else if (key == "DESCRIPTION") {
            out->description = utf8::sanitizeDisplay(value, kMaxDescription);
        } else if (key == "ACTION") {
            std::string err, target;
            ButtonActionType t;
            Key k;
            if (!parseButtonAction(value, &t, &k, &err, &target)) { warn(lineno, "ACTION: " + err + " - line ignored"); continue; }
            if (t == ButtonActionType::ButtonMode) {
                warn(lineno, "ACTION: a button mode cannot refer to another button mode - line ignored");
                continue;
            }
            out->type = t;
            out->key = k;
            out->target = target;
            out->action = utf8::lowerAscii(value);
            haveAction = true;
        } else if (key == "CYCLE") {
            std::string err;
            std::vector<std::string> list, dups;
            if (!parseModeCycleList(value, &list, &err, &dups)) { warn(lineno, "CYCLE: " + err + " - line ignored"); continue; }
            for (const std::string &d : dups) warn(lineno, "CYCLE: duplicate '" + d + "' dropped");
            cycle = list;
            cycleLine = lineno;
        } else {
            warn(lineno, key + ": not allowed in a button mode file (NAME, DESCRIPTION, ACTION, CYCLE) - line ignored");
        }
    }
    if (!haveAction) {
        if (warnings && warnings->size() < 200) warnings->push_back(out->path + ": no valid ACTION - file ignored");
        return false;
    }
    if (!cycle.empty()) {
        bool fwd = true;
        if (out->type == ButtonActionType::ModeSwitch && splitCycleTarget(out->target, &fwd, nullptr) &&
            out->target.size() == 4) {
            out->cycle = cycle;
            out->target = (fwd ? "next:" : "prev:") + joinIds(cycle);
            out->action = "mode:" + out->target;
        } else {
            warn(cycleLine, "CYCLE needs ACTION = mode:next or mode:prev - ignored");
        }
    }
    if (out->label.empty()) out->label = out->id;
    return true;
}

ButtonModeSet loadButtonModes(const std::string &dir, std::vector<std::string> *warnings) {
    ButtonModeSet set;
    auto warnAll = [&](const std::string &m) {
        if (warnings && warnings->size() < 200) warnings->push_back(m);
    };
    for (const BuiltinFile *f = builtinButtonModeFiles(); f && f->id; ++f) {
        ButtonMode b;
        b.id = f->id;
        b.path = std::string("built-in button mode ") + f->id;  // only for warnings, cleared below
        const bool ok = parseButtonModeText(f->text, &b, warnings);
        b.path.clear();
        b.builtin = true;
        if (ok) set.modes.push_back(b);
    }
    for (const confdir::Entry &e : confdir::readConfDir(dir, "button mode files", warnAll)) {
        if (matchesBuiltinText(builtinButtonModeFiles(), e.id, e.text)) continue;  // identical to the built-in
        ButtonMode b;
        b.id = e.id;
        b.path = e.path;
        if (!parseButtonModeText(e.text, &b, warnings)) continue;
        bool replaced = false;
        for (ButtonMode &x : set.modes)
            if (x.id == b.id) { x = b; replaced = true; break; }
        if (!replaced) set.modes.push_back(b);
    }
    return set;
}

std::string buttonModesStamp(const std::string &dir) { return confdir::stamp(dir, "buttons"); }

void expandSettings(Settings &s, const ButtonModeSet &set, std::set<std::string> *missing) {
    for (auto &kv : s.buttons) {
        ButtonActionType t;
        Key k;
        std::string id;
        if (!parseButtonAction(kv.second, &t, &k, nullptr, &id) || t != ButtonActionType::ButtonMode) continue;
        const ButtonMode *b = set.find(id);
        if (b) {
            kv.second = b->action;
        } else {
            kv.second = "none";
            if (missing) missing->insert(id);
        }
    }
}

ModeDef expandMode(const ModeDef &m, const ButtonModeSet &set, std::set<std::string> *missing) {
    ModeDef out = m;
    for (ButtonBinding &b : out.buttons) {
        if (b.type != ButtonActionType::ButtonMode) continue;
        const ButtonMode *bm = set.find(b.mode);
        if (bm) {
            b.type = bm->type;
            b.key = bm->key;
            b.mode = bm->target;
        } else {
            if (missing) missing->insert(b.mode);
            b.type = ButtonActionType::Unbound;
            b.mode.clear();
        }
    }
    return out;
}

}  // namespace spn
