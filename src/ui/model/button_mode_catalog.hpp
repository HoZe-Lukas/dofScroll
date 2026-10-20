// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ButtonModeCatalog: the buttons directory as seen by the UI (toolkit-free), the sister of
// ModeCatalog. Lists the button modes with their loader warnings, creates, duplicates, saves,
// imports and removes button mode files, finds where a button mode is still referenced and tells
// the view when the folder changed. Built on button_modes.hpp (loader); file access goes through
// platform/fs.hpp.
//
//   ButtonModeCatalog c(buttonsDir);          // refresh() runs in the constructor
//   c.createNew("Latch")                      // commented template with ACTION = none, 0600, never overwrites
//   c.duplicate("latch", "Latch 2")
//   c.save("latch", label, description, "toggle:nav")   // rewrites the file atomically, id unchanged
//   c.save(id, label, desc, "mode:next", cycleIds)       // with a CYCLE list (mode_cycle_model.hpp); empty = no CYCLE line
//   c.inspectImport(path, name) / c.importFile(path, name)
//   c.referencedBy("latch", settings, modes)  // where buttonmode:latch is still used
//   c.remove("latch")
#pragma once
#include <string>
#include <vector>

#include "button_modes.hpp"
#include "binding_editor_model.hpp"
#include "mode_catalog.hpp"
#include "mode_cycle_model.hpp"
#include "platform/fs.hpp"
#include "settings_model.hpp"
#include "strings.hpp"

namespace spn::ui {

class ButtonModeCatalog {
public:
    struct Entry {
        std::string id, label, description, action, path;
        std::vector<std::string> cycle;     // CYCLE ids in order (action is then plain mode:next / mode:prev)
        bool builtin = false;           // from the built-in library, no file (path is empty)
        bool overridesBuiltin = false;  // a file that replaces a built-in button mode (removing it restores it)
        std::vector<std::string> warnings;  // loader warnings of this file
    };
    struct Result {
        bool ok = false;
        std::string id, path, error;
        std::vector<std::string> warnings;  // import: loader warnings of the checked file
    };

    static constexpr size_t kMaxImportBytes = ModeCatalog::kMaxImportBytes;
    static constexpr size_t kMaxImportLines = ModeCatalog::kMaxImportLines;
    static constexpr size_t kMaxImportLineBytes = ModeCatalog::kMaxImportLineBytes;

    explicit ButtonModeCatalog(const std::string &dir) : dir_(dir) { refresh(); }

    const std::string &dir() const { return dir_; }
    const std::vector<Entry> &buttonModes() const { return entries_; }
    const ButtonModeSet &set() const { return set_; }
    const std::vector<std::string> &generalWarnings() const { return general_; }
    const Entry *find(const std::string &id) const {
        for (const Entry &e : entries_)
            if (e.id == id) return &e;
        return nullptr;
    }

    std::string stamp() const { return buttonModesStamp(dir_); }
    bool refreshIfChanged() {
        if (stamp() == stamp_) return false;
        refresh();
        return true;
    }
    void refresh() {
        std::vector<std::string> warn;
        stamp_ = stamp();
        set_ = loadButtonModes(dir_, &warn);
        const ButtonModeSet library = loadButtonModes("", nullptr);
        entries_.clear();
        general_.clear();
        for (const ButtonMode &b : set_.modes) {
            Entry e;
            e.id = b.id;
            e.label = b.label;
            e.description = b.description;
            e.cycle = b.cycle;
            e.action = b.cycle.empty() ? b.action : (b.target.compare(0, 4, "prev") == 0 ? "mode:prev" : "mode:next");
            e.path = b.path;
            e.builtin = b.builtin;
            e.overridesBuiltin = !b.builtin && library.find(b.id) != nullptr;
            entries_.push_back(e);
        }
        for (const std::string &w : warn) {
            bool taken = false;
            for (Entry &e : entries_)
                if (!e.path.empty() && w.rfind(e.path, 0) == 0) {
                    e.warnings.push_back(w);
                    taken = true;
                    break;
                }
            if (!taken) general_.push_back(w);
        }
    }

    // ---- ids and names (shared with ModeCatalog) ----
    std::string uniqueId(const std::string &name) const {
        const std::string base = ModeCatalog::deriveId(name);
        if (base.empty()) return "";
        if (!taken(base)) return base;
        for (int n = 2; n < 1000; ++n) {
            const std::string suffix = "-" + std::to_string(n);
            const std::string b = base.substr(0, 32 - suffix.size());
            if (!taken(b + suffix)) return b + suffix;
        }
        return "";
    }
    bool taken(const std::string &id) const {
        return find(id) != nullptr || (validModeId(id) && fs::pathExists(filePath(id)));
    }
    std::string filePath(const std::string &id) const { return validModeId(id) ? dir_ + "/" + id + ".conf" : std::string(); }

    // ---- content ----
    static std::string newTemplate(const std::string &id, const std::string &label, const std::string &description,
                                   const std::string &action,
                                   const std::vector<std::string> &cycle = std::vector<std::string>()) {
        std::string t;
        t += "# Button mode \"" + id + "\". Save the file; the daemon reloads it by itself. Documentation: docs/button-modes.md\n";
        t += "#\n";
        t += "# ACTION is one button action: none | click | rightclick | middleclick | nav | key:<name> |\n";
        t += "#   mode:<id> | mode:next | mode:prev | toggle:nav | toggle:mode:<id>. Not allowed: buttonmode:<id>.\n";
        t += "# CYCLE = id, id, ... (only with mode:next / mode:prev) cycles through exactly these modes.\n";
        t += "# Use it with BUTTON_<n> = buttonmode:" + id + " or pick it in the interface.\n";
        t += "# WARNING: a button mode can press a key or click. Read files from other people before use.\n";
        t += "# An action that is the same everywhere and always belongs in the vendor driver.\n\n";
        t += "NAME = " + label + "\n";
        t += "DESCRIPTION = " + description + "\n";
        t += "ACTION = " + action + "\n";
        if (!cycle.empty()) t += "CYCLE = " + ModeCycleList(cycle).toText() + "\n";
        return t;
    }

    // Validates an action for a button mode: a button action, no nesting. Returns the canonical
    // (lower case, trimmed) text; "" + *err on failure.
    static std::string checkAction(const std::string &text, std::string *err) {
        ButtonActionType t;
        Key k;
        std::string e;
        if (!parseButtonAction(text, &t, &k, &e)) {
            if (err) *err = e;
            return "";
        }
        if (t == ButtonActionType::ButtonMode) {
            if (err) *err = "a button mode cannot refer to another button mode";
            return "";
        }
        std::string s = utf8::trim(text);
        for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    Result createNew(const std::string &displayName) {
        Result r;
        const std::string label = ModeCatalog::cleanLabel(displayName);
        const std::string id = ModeCatalog::deriveId(label);
        if (label.empty() || id.empty()) return fail(r, "the name must contain letters or digits");
        if (taken(id)) return fail(r, "a button mode with the id '" + id + "' already exists");
        return write(r, id, newTemplate(id, label, "", "none"));
    }
    Result duplicate(const std::string &id, const std::string &newName) {
        Result r;
        const Entry *src = find(id);
        if (!src) return fail(r, "no such button mode");
        const std::string label = ModeCatalog::cleanLabel(newName);
        const std::string nid = ModeCatalog::deriveId(label);
        if (label.empty() || nid.empty()) return fail(r, "the name must contain letters or digits");
        if (taken(nid)) return fail(r, "a button mode with the id '" + nid + "' already exists");
        return write(r, nid, newTemplate(nid, label, src->description, src->action, src->cycle));
    }

    // File for a built-in button mode: same id, so the file replaces the built-in one.
    Result makeEditableCopy(const std::string &id) {
        Result r;
        const Entry *src = find(id);
        if (!src) return fail(r, "no such button mode");
        if (!src->builtin) return fail(r, "this button mode already has a file");
        if (fs::pathExists(filePath(id))) return fail(r, "file already exists");
        return write(r, id, newTemplate(id, src->label, src->description, src->action));
    }

    // Rewrites the file of `id`: the leading comment block stays, then NAME, DESCRIPTION (if not
    // empty) and ACTION. The id (file name) never changes. Atomic.
    Result save(const std::string &id, const std::string &name, const std::string &description,
                const std::string &action,
                const std::vector<std::string> &cycle = std::vector<std::string>()) {
        Result r;
        const Entry *e = find(id);
        if (!e) return fail(r, "no such button mode");
        if (e->builtin) return fail(r, "a built-in button mode has no file; create an editable copy first");
        std::string err;
        const std::string act = checkAction(action, &err);
        if (act.empty()) return fail(r, err);
        if (!cycle.empty() && act != "mode:next" && act != "mode:prev")
            return fail(r, "a cycle list needs the action mode:next or mode:prev");
        if (cycle.size() > kMaxModeCycle) return fail(r, "too many modes in the cycle list");
        for (const std::string &c : cycle)
            if (!validModeId(c)) return fail(r, "invalid mode id in the cycle list");
        const std::string label = ModeCatalog::cleanLabel(name);
        const std::string desc = ModeCatalog::cleanLabel(description);
        std::string old;
        fs::readFileBounded(e->path, kMaxImportBytes, fs::ReadOptions(), &old);
        std::string out;
        size_t pos = 0;
        while (pos < old.size()) {  // leading block: blank lines and comments
            size_t nl = old.find('\n', pos);
            if (nl == std::string::npos) nl = old.size();
            const std::string line = utf8::trim(old.substr(pos, nl - pos));
            if (!line.empty() && line[0] != '#' && line[0] != ';') break;
            out += old.substr(pos, nl - pos) + "\n";
            pos = nl + 1;
        }
        if (!out.empty() && out.back() == '\n' && out.size() > 1 && out[out.size() - 2] != '\n') out += "\n";
        if (!label.empty()) out += "NAME = " + label + "\n";
        if (!desc.empty()) out += "DESCRIPTION = " + desc + "\n";
        out += "ACTION = " + act + "\n";
        if (!cycle.empty()) out += "CYCLE = " + ModeCycleList(cycle).toText() + "\n";
        if (!fs::writeFileReplace(e->path, out, &err)) return fail(r, "cannot write " + e->path + ": " + err);
        r.ok = true;
        r.id = id;
        r.path = e->path;
        refresh();
        return r;
    }

    // ---- import ----
    Result inspectImport(const std::string &path, const std::string &name = std::string()) const {
        Result r;
        fs::FileInfo fi;
        if (!fs::statPath(path, &fi) || !fi.isFile) return fail(r, "not a regular file");
        if (fi.size < 0 || size_t(fi.size) > kMaxImportBytes) return fail(r, "file too large (at most 64 KiB)");
        std::string text;
        const fs::ReadResult rr = fs::readFileBounded(path, kMaxImportBytes, fs::ReadOptions(), &text);
        if (!rr.ok()) return fail(r, "cannot read the file" + (rr.sysError.empty() ? "" : ": " + rr.sysError));
        if (text.find('\0') != std::string::npos) return fail(r, "the file contains a NUL byte");
        size_t lines = 0, cur = 0;
        for (char c : text) {
            if (c == '\n') {
                ++lines;
                cur = 0;
            } else if (++cur > kMaxImportLineBytes) {
                return fail(r, "a line is longer than 4096 bytes");
            }
        }
        if (cur) ++lines;
        if (lines > kMaxImportLines) return fail(r, "too many lines (at most 1000)");

        std::string stem = name;
        if (stem.empty()) {
            const size_t sep = fs::lastSeparator(path);
            stem = sep == std::string::npos ? path : path.substr(sep + 1);
            if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, ".conf") == 0) stem.resize(stem.size() - 5);
        }
        const std::string id = ModeCatalog::deriveId(stem);
        if (id.empty()) return fail(r, "cannot derive a button mode id from the name");
        if (taken(id)) return fail(r, "a button mode with the id '" + id + "' already exists; choose another name");
        r.id = id;

        const size_t sep = fs::lastSeparator(dir_);
        char hex[17];
        fs::randomHex16(hex);
        const std::string staging = (sep == std::string::npos ? std::string(".") : dir_.substr(0, sep)) +
                                    "/.dofscroll-import-" + hex;
        std::string e;
        if (!fs::mkdirsPrivate(staging, &e)) return fail(r, "cannot create a staging directory: " + e);
        const std::string sfile = staging + "/" + id + ".conf";
        const bool wrote = fs::createFileExclusive(sfile, text);
        std::vector<std::string> warn;
        ButtonModeSet probe;
        if (wrote) probe = loadButtonModes(staging, &warn);
        fs::removeFile(sfile);
        fs::removeDir(staging);
        if (!wrote) return fail(r, "cannot stage the file");
        for (const std::string &w : warn) r.warnings.push_back(relocate(w, staging, dir_));
        if (!probe.find(id)) return fail(r, "the file is not a valid button mode file (it needs a valid ACTION)");
        r.ok = true;
        return r;
    }
    Result importFile(const std::string &path, const std::string &name = std::string()) {
        Result r = inspectImport(path, name);
        if (!r.ok) return r;
        std::string text, e;
        if (!fs::readFileBounded(path, kMaxImportBytes, fs::ReadOptions(), &text).ok()) return fail(r, "cannot read the file");
        if (!fs::mkdirsPrivate(dir_, &e)) return fail(r, "mkdir: " + e);
        const std::string dest = filePath(r.id);
        if (fs::pathExists(dest) || !fs::createFileExclusive(dest, text)) return fail(r, "cannot create " + dest);
        r.path = dest;
        refresh();
        return r;
    }
    static std::string importSafetyNote(const StringProvider &s) {
        return s.tr("A button mode can press a key or click whenever its button is pressed. Only keep files from "
                    "sources you trust and read the file first.");
    }

    // ---- remove ----
    // Where buttonmode:<id> is used: BUTTON_<n> lines of the profile sections ("[app:x] BUTTON_3") and
    // bindings of mode files ("mode timeline: BUTTON_1"). The removal itself is still allowed; the
    // references then show as "missing: <id>".
    std::vector<std::string> referencedBy(const std::string &id, const SettingsModel &settings,
                                          const ModeSet &modes) const {
        std::vector<std::string> out;
        for (const SettingsModel::SectionInfo &s : settings.sections())
            for (const SettingsModel::ButtonRow &r : settings.buttonRows(s.index))
                if (r.overridden && lowerTrim(r.action) == "buttonmode:" + id)
                    out.push_back(s.title + " BUTTON_" + std::to_string(r.number));
        for (const ModeDef &m : modes.modes)
            for (const ButtonBinding &b : m.buttons)
                if (b.type == ButtonActionType::ButtonMode && b.mode == id)
                    out.push_back("mode " + m.id + ": BUTTON_" + std::to_string(b.button));
        return out;
    }
    Result remove(const std::string &id) {
        Result r;
        const Entry *e = find(id);
        if (!e) return fail(r, "no such button mode");
        if (e->builtin) return fail(r, "built-in button modes cannot be removed");
        const std::string p = e->path;
        if (!fs::removeFile(p)) return fail(r, "cannot remove " + p);
        r.ok = true;
        r.id = id;
        r.path = p;
        refresh();
        return r;
    }

private:
    static Result &fail(Result &r, const std::string &m) {
        r.ok = false;
        r.error = m;
        return r;
    }
    static std::string relocate(const std::string &w, const std::string &from, const std::string &to) {
        return w.rfind(from, 0) == 0 ? to + w.substr(from.size()) : w;
    }
    Result &write(Result &r, const std::string &id, const std::string &content) {
        std::string e;
        if (!fs::mkdirsPrivate(dir_, &e)) return fail(r, "mkdir: " + e);
        const std::string p = filePath(id);
        if (p.empty() || fs::pathExists(p)) return fail(r, "file already exists");
        if (!fs::createFileExclusive(p, content)) return fail(r, "cannot create file");
        r.ok = true;
        r.id = id;
        r.path = p;
        refresh();
        return r;
    }

    std::string dir_, stamp_;
    ButtonModeSet set_;
    std::vector<Entry> entries_;
    std::vector<std::string> general_;
};

}  // namespace spn::ui
