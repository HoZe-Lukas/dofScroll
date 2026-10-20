// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ModeEditor: form model for editing a joystick mode file in the interface (toolkit-free). The form
// holds name, description, the three channel actions, the button bindings and the settings
// overrides of one mode file. Saving rewrites the file line by line: the lines of the keys the
// form owns are updated in place, new ones are appended, cleared ones are removed; comments,
// unknown lines and the SAFETY header stay untouched. The result is checked with the real loader
// before it replaces the file (atomically); the loader's warnings, including the chord warning,
// are returned to the view.
//
//   ModeEditor ed;
//   ed.open(path, *modeSet.find(id), &err);   // form from a loaded mode
//   ed.form().h = {ChannelActionType::Keys, "ctrl+pageup", "ctrl+pagedown"};
//   ed.validate();                            // problems per field, shown inline
//   ed.save();                                // atomic; refused when the loader would drop a line
#pragma once
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "mode_list.hpp"
#include "modes.hpp"
#include "platform/fs.hpp"
#include "utf8.hpp"

namespace spn::ui {

struct ChannelForm {
    ChannelActionType type = ChannelActionType::Default;
    std::string neg = "up", pos = "down";  // keys or chords ("ctrl+pageup"); only for Keys and Step

    bool usesKeys() const { return type == ChannelActionType::Keys || type == ChannelActionType::Step; }
    // The action text of the file, as parseChannelAction reads it.
    std::string text() const {
        switch (type) {
            case ChannelActionType::Default: return "default";
            case ChannelActionType::Off: return "none";
            case ChannelActionType::WheelV: return "wheel_v";
            case ChannelActionType::WheelH: return "wheel_h";
            case ChannelActionType::Zoom: return "zoom";
            case ChannelActionType::Keys: return "keys:" + neg + "," + pos;
            case ChannelActionType::Step: return "step:" + neg + "," + pos;
        }
        return "default";
    }
    bool operator==(const ChannelForm &o) const {
        return type == o.type && (!usesKeys() || (neg == o.neg && pos == o.pos));
    }
};

struct ModeForm {
    std::string id, name, description;
    ChannelForm v, h, twist;
    std::vector<std::pair<int, std::string>> buttons;          // button number (1..32), action text; sorted by number
    std::vector<std::pair<std::string, std::string>> overrides;  // settings key, value; in file order

    bool operator==(const ModeForm &o) const {
        return id == o.id && name == o.name && description == o.description && v == o.v && h == o.h &&
               twist == o.twist && buttons == o.buttons && overrides == o.overrides;
    }
    const std::string *button(int n) const {
        for (const auto &b : buttons)
            if (b.first == n) return &b.second;
        return nullptr;
    }
    // Sets (action != "none") or clears (action empty or "none") the binding of button n.
    void setButton(int n, const std::string &action) {
        for (size_t i = 0; i < buttons.size(); ++i)
            if (buttons[i].first == n) {
                if (action.empty() || action == "none") buttons.erase(buttons.begin() + long(i));
                else buttons[i].second = action;
                return;
            }
        if (action.empty() || action == "none") return;
        buttons.emplace_back(n, action);
        std::sort(buttons.begin(), buttons.end());
    }
};

struct EditProblem {
    std::string field;    // "name", "v", "h", "twist", "button:<n>", "override:<KEY>"
    std::string message;  // English source string
};

inline ChannelForm channelFormOf(const ChannelAction &a) {
    ChannelForm f;
    f.type = a.type;
    if (a.usesKeys()) {
        f.neg = modelist::chordText(a.neg, a.negMods);
        f.pos = modelist::chordText(a.pos, a.posMods);
    }
    return f;
}

inline ModeForm formFromMode(const ModeDef &m) {
    ModeForm f;
    f.id = m.id;
    f.name = m.label == m.id ? std::string() : m.label;
    f.description = m.description;
    f.v = channelFormOf(m.v);
    f.h = channelFormOf(m.h);
    f.twist = channelFormOf(m.twist);
    for (const ButtonBinding &b : m.buttons)
        if (b.type != ButtonActionType::Unbound) f.buttons.emplace_back(b.button, modelist::buttonActionText(b));
    std::sort(f.buttons.begin(), f.buttons.end());
    f.overrides = m.overrides;
    return f;
}

class ModeEditor {
public:
    struct SaveResult {
        bool ok = false;
        std::string error;
        std::vector<std::string> warnings;  // loader warnings of the saved file
        std::vector<EditProblem> problems;  // validation problems when the save was refused
        bool changedOnDisk = false;         // refused: the file changed since it was opened (save(true) overwrites)
    };

    // Keys the daemon accepts as overrides in a mode file: channel keys and the shared detail
    // settings; not MODE, ENABLED, BACKEND, TICK_HZ, the button keys and the live feed.
    static std::vector<std::string> overrideKeys() {
        std::vector<std::string> out;
        for (const KV &kv : allKeys(Settings())) {
            const std::string &k = kv.first;
            if (k == "MODE" || k == "ENABLED" || k == "BACKEND" || k == "TICK_HZ" || k == "LIVE_FEED" ||
                k.rfind("BUTTON_", 0) == 0 || k.rfind("NAV_", 0) == 0 || k.rfind("CLICK_", 0) == 0)
                continue;
            out.push_back(k);
        }
        return out;
    }
    std::vector<std::string> addableOverrideKeys() const {
        std::vector<std::string> out;
        for (const std::string &k : overrideKeys()) {
            bool used = false;
            for (const auto &o : form_.overrides) used = used || upper(o.first) == k;
            if (!used) out.push_back(k);
        }
        return out;
    }

    // Opens the file `path` for the loaded mode `def` (id = file name without ".conf").
    bool open(const std::string &path, const ModeDef &def, std::string *err) {
        std::string text;
        const fs::ReadResult rr = fs::readFileBounded(path, kMaxBytes, fs::ReadOptions(), &text);
        if (!rr.ok()) {
            if (err) *err = "cannot read " + path;
            return false;
        }
        path_ = path;
        text_ = text;
        form_ = original_ = formFromMode(def);
        stampFile();
        return true;
    }
    bool isOpen() const { return !path_.empty(); }
    const std::string &path() const { return path_; }
    ModeForm &form() { return form_; }
    const ModeForm &form() const { return form_; }
    bool dirty() const { return !(form_ == original_); }
    // The file's current mtime/size/identity differs from when it was opened or last saved.
    bool changedOnDisk() const {
        fs::FileInfo fi;
        if (!fs::statPath(path_, &fi)) return true;
        return fi.mtimeNs != mtime_ || fi.size != size_ || fi.fileId != fileId_;
    }
    // Discards the edits and reads the mode again (the mode set is loaded from dir).
    bool revert(const std::string &dir, std::string *err) {
        const ModeSet ms = loadModes(dir, nullptr);
        const ModeDef *m = ms.find(form_.id);
        if (!m) {
            if (err) *err = "the mode is no longer loaded";
            return false;
        }
        return open(path_, *m, err);
    }

    // ---- validation ----
    std::vector<EditProblem> validate() const {
        std::vector<EditProblem> out;
        std::string err;
        if (form_.name.size() > kMaxName) out.push_back({"name", "the name is too long (at most 64 bytes)"});
        if (form_.description.size() > kMaxDescription) out.push_back({"description", "the description is too long"});
        const std::pair<const char *, const ChannelForm *> ch[] = {{"v", &form_.v}, {"h", &form_.h}, {"twist", &form_.twist}};
        for (const auto &c : ch) {
            ChannelAction a;
            if (!parseChannelAction(c.second->text(), &a, &err)) out.push_back({c.first, err});
        }
        for (size_t i = 0; i < form_.buttons.size(); ++i) {
            const int n = form_.buttons[i].first;
            const std::string field = "button:" + std::to_string(n);
            if (n < 1 || n > kMaxModeButton) {
                out.push_back({field, "button numbers go from 1 to 32"});
                continue;
            }
            for (size_t j = 0; j < i; ++j)
                if (form_.buttons[j].first == n) out.push_back({field, "this button is bound twice"});
            ButtonActionType t;
            Key k;
            if (!parseButtonAction(form_.buttons[i].second, &t, &k, &err)) out.push_back({field, err});
        }
        const std::vector<std::string> allowed = overrideKeys();
        for (size_t i = 0; i < form_.overrides.size(); ++i) {
            const std::string key = upper(form_.overrides[i].first);
            const std::string field = "override:" + key;
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) {
                out.push_back({field, "this setting cannot be overridden in a mode"});
                continue;
            }
            for (size_t j = 0; j < i; ++j)
                if (upper(form_.overrides[j].first) == key) out.push_back({field, "this setting is listed twice"});
            Settings probe;
            if (!applyKey(probe, key, form_.overrides[i].second, &err)) out.push_back({field, err});
        }
        if (form_.overrides.size() > kMaxOverrides) out.push_back({"overrides", "too many overrides (at most 128)"});
        return out;
    }

    // ---- composing and saving ----
    // The new file text: the original lines with the owned keys updated, removed or appended.
    std::string compose() const {
        std::vector<std::pair<std::string, std::string>> want;  // key, value (file order for the appended ones)
        if (!form_.name.empty()) want.emplace_back("NAME", form_.name);
        if (!form_.description.empty()) want.emplace_back("DESCRIPTION", form_.description);
        if (form_.v.type != ChannelActionType::Default) want.emplace_back("V_ACTION", form_.v.text());
        if (form_.h.type != ChannelActionType::Default) want.emplace_back("H_ACTION", form_.h.text());
        if (form_.twist.type != ChannelActionType::Default) want.emplace_back("TWIST_ACTION", form_.twist.text());
        for (const auto &b : form_.buttons) want.emplace_back("BUTTON_" + std::to_string(b.first), b.second);
        for (const auto &o : form_.overrides) want.emplace_back(upper(o.first), o.second);
        std::vector<bool> done(want.size(), false);
        const std::vector<std::string> known = overrideKeys();

        std::string out;
        size_t pos = 0;
        while (pos < text_.size()) {
            size_t nl = text_.find('\n', pos);
            const bool hasNl = nl != std::string::npos;
            if (!hasNl) nl = text_.size();
            std::string line = text_.substr(pos, nl - pos);
            pos = nl + 1;
            std::string eol = hasNl ? "\n" : "";
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
                eol = hasNl ? "\r\n" : "";
            }
            const std::string t = utf8::trim(line);
            const size_t eq = t.find('=');
            if (t.empty() || t[0] == '#' || t[0] == ';' || t[0] == '[' || eq == std::string::npos) {
                out += line + eol;
                continue;
            }
            const std::string key = upper(utf8::trim(t.substr(0, eq)));
            size_t idx = want.size();
            for (size_t i = 0; i < want.size(); ++i)
                if (want[i].first == key) idx = i;
            const bool structural = key == "NAME" || key == "DESCRIPTION" || key == "V_ACTION" || key == "H_ACTION" ||
                                    key == "TWIST_ACTION" || key.rfind("BUTTON_", 0) == 0;
            const bool owned = structural || idx < want.size() || std::find(known.begin(), known.end(), key) != known.end();
            if (!owned) {
                out += line + eol;  // unknown line: untouched
                continue;
            }
            if (idx == want.size() || done[idx]) continue;  // cleared by the form, or a duplicate
            done[idx] = true;
            const std::string valuePart = t.substr(eq + 1);
            const std::string comment = inlineComment(valuePart);
            const std::string oldValue = utf8::trim(valuePart.substr(0, valuePart.size() - comment.size()));
            if (oldValue == want[idx].second) out += line + eol;  // unchanged: the line stays exactly as it was
            else out += want[idx].first + " = " + want[idx].second + comment + eol;
        }
        bool first = true;
        for (size_t i = 0; i < want.size(); ++i) {
            if (done[i]) continue;
            if (first && !out.empty() && out.back() != '\n') out += "\n";
            first = false;
            out += want[i].first + " = " + want[i].second + "\n";
        }
        return out;
    }

    // Validates, checks the result with the loader and replaces the file atomically. The id never
    // changes. If the file changed on disk since it was opened, nothing is written unless
    // overwriteChanged (the view asks the user). Refused, with the reason, when the loader would
    // drop one of the lines the form wrote.
    SaveResult save(bool overwriteChanged = false) {
        SaveResult r;
        r.problems = validate();
        if (!r.problems.empty()) {
            r.error = r.problems.front().message;
            return r;
        }
        if (!overwriteChanged && changedOnDisk()) {
            r.changedOnDisk = true;
            r.error = "the file changed on disk";
            return r;
        }
        const std::string out = compose();
        std::vector<std::string> warnings;
        if (!stageCheck(out, &warnings, &r.error)) return r;
        std::string err;
        if (!fs::writeFileReplace(path_, out, &err)) {
            r.error = "cannot write " + path_ + ": " + err;
            return r;
        }
        text_ = out;
        original_ = form_;
        stampFile();
        r.warnings = warnings;
        r.ok = true;
        return r;
    }

private:
    static constexpr size_t kMaxBytes = 64 * 1024;
    static constexpr size_t kMaxName = 64;
    static constexpr size_t kMaxDescription = 256;
    static constexpr size_t kMaxOverrides = 128;
    static constexpr int kMaxModeButton = 32;

    static std::string upper(const std::string &s) { return utf8::upperAscii(s); }

    // " # comment" after the value of a line, kept when the value is replaced.
    static std::string inlineComment(const std::string &valuePart) {
        for (size_t p = 1; p < valuePart.size(); ++p)
            if (valuePart[p] == '#' && utf8::isSpace(valuePart[p - 1])) {
                size_t start = p;
                while (start > 0 && utf8::isSpace(valuePart[start - 1])) --start;
                return valuePart.substr(start);  // with the whitespace before the "#", as written
            }
        return std::string();
    }

    void stampFile() {
        fs::FileInfo fi;
        if (fs::statPath(path_, &fi)) {
            mtime_ = fi.mtimeNs;
            size_ = fi.size;
            fileId_ = fi.fileId;
        }
    }

    // Runs the real loader on the composed text in a private staging directory.
    bool stageCheck(const std::string &text, std::vector<std::string> *warnings, std::string *error) const {
        const size_t sep = fs::lastSeparator(path_);
        const std::string dir = sep == std::string::npos ? std::string(".") : path_.substr(0, sep);
        const size_t psep = fs::lastSeparator(dir);
        char hex[17];
        fs::randomHex16(hex);
        const std::string staging = (psep == std::string::npos ? std::string(".") : dir.substr(0, psep)) +
                                    "/.dofscroll-edit-" + hex;
        std::string e;
        if (!fs::mkdirsPrivate(staging, &e)) {
            *error = "cannot create a staging directory: " + e;
            return false;
        }
        const std::string sfile = staging + "/" + form_.id + ".conf";
        const bool wrote = fs::createFileExclusive(sfile, text);
        std::vector<std::string> warn;
        ModeSet probe;
        if (wrote) probe = loadModes(staging, &warn);
        fs::removeFile(sfile);
        fs::removeDir(staging);
        if (!wrote) {
            *error = "cannot stage the file";
            return false;
        }
        for (std::string &w : warn)
            if (w.rfind(staging, 0) == 0) w = dir + w.substr(staging.size());
        const ModeDef *m = probe.find(form_.id);
        if (!m) {
            *error = "the file would not load as a mode";
            return false;
        }
        // Refuse when the loader drops a line this form wrote (not for foreign lines that were already ignored).
        std::vector<std::string> mine = {"V_ACTION", "H_ACTION", "TWIST_ACTION"};
        for (const auto &b : form_.buttons) mine.push_back("BUTTON_" + std::to_string(b.first));
        for (const auto &o : form_.overrides) mine.push_back(upper(o.first));
        for (const std::string &w : warn)
            if (w.find("ignored") != std::string::npos)
                for (const std::string &k : mine)
                    if (w.find(": " + k + ":") != std::string::npos) {
                        *error = w;
                        return false;
                    }
        *warnings = warn;
        return true;
    }

    std::string path_, text_;
    ModeForm form_, original_;
    int64_t mtime_ = 0, size_ = 0;
    uint64_t fileId_ = 0;
};

}  // namespace spn::ui
