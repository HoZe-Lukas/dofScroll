// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ModeCatalog: the modes directory as seen by the UI (toolkit-free). Lists the modes with their
// origin and loader warnings, creates, duplicates, imports and removes mode files, and tells the
// view when the folder changed. Built on modes.hpp (loader) and mode_list.hpp; all file access
// goes through platform/fs.hpp.
//
//   ModeCatalog c(modesDir);                 // refresh() runs in the constructor
//   c.modes();                               // ModeEntry: id, label, description, origin, warnings
//   c.createNew("My mode", id-derived)       // commented template, 0600, never overwrites
//   c.duplicate("timeline", "My timeline")   // also from built-ins
//   c.makeEditableCopy("normal")             // file with the same id: overrides the built-in
//   c.inspectImport(path, name)              // check before copying: id, warnings, errors
//   c.importFile(path, name)                 // same checks, then atomic copy, never overwrites
//   c.remove(id)                             // file modes only; removing an override restores the built-in
//   c.stamp() / c.refreshIfChanged()         // folder change detection for the view
// Every operation that changes the folder refreshes the catalog. Operations return a Result with
// ok/error; the path of a new file is meant for the editor (ActionRunner::openPath).
// Safety: a mode file can press any key (including Ctrl/Alt/Super chords) while a button or the
// mouse is moved; importSafetyNote() is the text the view shows before an imported file is kept.
#pragma once
#include <cctype>
#include "utf8.hpp"
#include <string>
#include <vector>

#include "mode_list.hpp"
#include "modes.hpp"
#include "platform/fs.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

class ModeCatalog {
public:
    enum class Origin { BuiltIn, File, BuiltInOverridden };
    struct ModeEntry {
        std::string id, label, description;
        Origin origin = Origin::BuiltIn;
        std::string path;                   // file of a File / BuiltInOverridden mode, else ""
        std::vector<std::string> warnings;  // loader warnings of this mode (incl. the chord warning)
    };
    struct Result {
        bool ok = false;
        std::string id, path;  // of the created/imported mode file
        std::string error;
        std::vector<std::string> warnings;  // import: loader warnings of the checked file
    };

    static constexpr size_t kMaxImportBytes = 64 * 1024;  // as the loader accepts
    static constexpr size_t kMaxImportLines = 1000;
    static constexpr size_t kMaxImportLineBytes = 4096;

    explicit ModeCatalog(const std::string &dir) : dir_(dir) { refresh(); }

    const std::string &dir() const { return dir_; }
    const std::vector<ModeEntry> &modes() const { return entries_; }
    const ModeSet &modeSet() const { return set_; }
    // Loader warnings that belong to no single mode (directory problems, ignored files).
    const std::vector<std::string> &generalWarnings() const { return general_; }
    const ModeEntry *find(const std::string &id) const {
        for (const auto &e : entries_)
            if (e.id == id) return &e;
        return nullptr;
    }

    // Identity of the folder contents ("" if missing), as in modesStamp().
    std::string stamp() const { return modesStamp(dir_); }
    // Reloads if the folder changed since the last refresh; true if it did.
    bool refreshIfChanged() {
        if (stamp() == stamp_) return false;
        refresh();
        return true;
    }
    void refresh() {
        std::vector<std::string> warn;
        stamp_ = stamp();
        set_ = loadModes(dir_, &warn);
        const ModeSet builtin = loadModes("", nullptr);
        entries_.clear();
        general_.clear();
        for (const ModeDef &m : set_.modes) {
            ModeEntry e;
            e.id = m.id;
            e.label = m.label.empty() ? m.id : m.label;
            e.description = m.description;
            const bool isBuiltinId = builtin.find(m.id) != nullptr;
            e.origin = m.builtin ? Origin::BuiltIn : (isBuiltinId ? Origin::BuiltInOverridden : Origin::File);
            if (!m.builtin) e.path = modelist::modeFilePath(dir_, m.id);
            entries_.push_back(e);
        }
        for (const std::string &w : warn) {
            bool taken = false;
            for (ModeEntry &e : entries_) {
                if ((!e.path.empty() && w.rfind(e.path, 0) == 0) || w.rfind("mode " + e.id + " ", 0) == 0 ||
                    w.rfind("mode " + e.id + ":", 0) == 0) {
                    e.warnings.push_back(w);
                    taken = true;
                    break;
                }
            }
            if (!taken) general_.push_back(w);
        }
    }

    // ---- ids ----
    // "My Mode!" -> "my-mode": lower case, [a-z0-9_-] only, at most 32 characters; a reserved
    // device name gets "-mode" appended. "" if nothing usable is left.
    static std::string deriveId(const std::string &name) {
        std::string id;
        bool dash = false;
        for (unsigned char c : name) {
            if (std::isalnum(c) && c < 128) {
                if (dash && !id.empty()) id += '-';
                dash = false;
                id += char(std::tolower(c));
            } else if (c == '_') {
                id += '_';
                dash = false;
            } else {
                dash = true;
            }
        }
        while (!id.empty() && (id.back() == '-' || id.back() == '_')) id.pop_back();
        if (id.size() > 26) id.resize(26);
        while (!id.empty() && (id.back() == '-' || id.back() == '_')) id.pop_back();
        if (id.empty()) return "";
        if (!validModeId(id)) id += "-mode";
        return validModeId(id) ? id : std::string();
    }
    // deriveId, with "-2", "-3", ... appended while the id is taken (for suggestions in the view).
    std::string uniqueId(const std::string &name) const {
        const std::string base = deriveId(name);
        if (base.empty()) return "";
        if (!taken(base)) return base;
        for (int n = 2; n < 1000; ++n) {
            std::string suffix = "-" + std::to_string(n);
            std::string b = base.substr(0, 32 - suffix.size());
            if (!taken(b + suffix)) return b + suffix;
        }
        return "";
    }
    bool taken(const std::string &id) const {
        return find(id) != nullptr || (validModeId(id) && fs::pathExists(modelist::modeFilePath(dir_, id)));
    }
    // Display name for the NAME key: control characters become spaces, trimmed, at most 64 bytes.
    static std::string cleanLabel(const std::string &name) {
        std::string s;
        for (unsigned char c : name) s += (c < 0x20 || c == 0x7f) ? ' ' : char(c);
        const size_t a = s.find_first_not_of(' ');
        if (a == std::string::npos) return "";
        s = s.substr(a, s.find_last_not_of(' ') - a + 1);
        s = utf8::sanitizeDisplay(s, 64);  // invalid bytes -> U+FFFD, cut on a character boundary
        s = utf8::trim(s);
        return s;
    }

    // ---- create / duplicate ----
    // Template text of a new mode: every key documented, nothing active except NAME.
    static std::string newModeTemplate(const std::string &id, const std::string &label) {
        std::string t;
        t += "# Mode \"" + id + "\". Save the file; the daemon reloads it by itself. Documentation: docs/modes.md\n";
        t += "#\n";
        t += "# Channel actions (V = vertical push, H = horizontal push, TWIST = twisting the cap):\n";
        t += "#   default | none | wheel_v | wheel_h | zoom | keys:<neg>,<pos>\n";
        t += "#   keys:left,right presses the first key for one direction, the second for the other.\n";
        t += "# Button actions (buttons are numbered from 1, BUTTON_1 is the first button):\n";
        t += "#   none | click | rightclick | middleclick | nav | key:<name> | mode:<id> | mode:next |\n";
        t += "#   toggle:nav | toggle:mode:<id>\n";
        t += "# Settings overrides: any channel key of the main configuration, e.g. SCROLL_H_SENS = 4,\n";
        t += "#   ZOOM_SIGN = -1, SCROLL_V_CURVE = 1.5. Not allowed here: MODE, ENABLED, BACKEND, TICK_HZ.\n";
        t += "# WARNING: a mode file can press any key. Read files from other people before use.\n\n";
        t += "NAME = " + label + "\n";
        t += "DESCRIPTION = \n\n";
        t += "# V_ACTION = default\n# H_ACTION = default\n# TWIST_ACTION = default\n\n";
        t += "# BUTTON_1 = key:space\n# BUTTON_2 = click\n\n";
        t += "# SCROLL_H_SENS = 4\n";
        return t;
    }

    Result createNew(const std::string &displayName) {
        Result r;
        const std::string label = cleanLabel(displayName);
        const std::string id = deriveId(label);
        if (label.empty() || id.empty()) return fail(r, "the name must contain letters or digits");
        if (taken(id)) return fail(r, "a mode with the id '" + id + "' already exists");
        return write(r, id, newModeTemplate(id, label));
    }
    // Copy of any mode (built-ins included) under a new name; the copy has no file link to the source.
    Result duplicate(const std::string &id, const std::string &newName) {
        Result r;
        const ModeDef *src = set_.find(id);
        if (!src) return fail(r, "no such mode");
        const std::string label = cleanLabel(newName);
        const std::string nid = deriveId(label);
        if (label.empty() || nid.empty()) return fail(r, "the name must contain letters or digits");
        if (taken(nid)) return fail(r, "a mode with the id '" + nid + "' already exists");
        ModeDef copy = *src;
        copy.id = nid;
        copy.label = label;
        copy.builtin = false;
        return write(r, nid, modelist::modeTemplate(copy));
    }
    // Editable file for a mode that has none (a built-in): same id, so the file replaces the built-in.
    Result makeEditableCopy(const std::string &id) {
        Result r;
        const ModeDef *src = set_.find(id);
        if (!src) return fail(r, "no such mode");
        if (!src->builtin) return fail(r, "this mode already has a file");
        if (fs::pathExists(modelist::modeFilePath(dir_, id))) return fail(r, "file already exists");
        return write(r, id, modelist::modeTemplate(*src));
    }

    // ---- import ----
    // Checks a file without touching the modes directory: regular file, at most kMaxImportBytes,
    // at most kMaxImportLines lines of at most kMaxImportLineBytes, no NUL byte, it must parse into a
    // mode, and the id (from `name`, else the file name without ".conf") must be free. The loader
    // warnings (ignored lines, chord warning) are returned in Result::warnings. Nothing is kept.
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
        const std::string id = deriveId(stem);
        if (id.empty()) return fail(r, "cannot derive a mode id from the name");
        if (taken(id)) return fail(r, "a mode with the id '" + id + "' already exists; choose another name");
        r.id = id;

        // Parse with the real loader in a private staging directory next to the modes directory.
        const size_t sep = fs::lastSeparator(dir_);
        char hex[17];
        fs::randomHex16(hex);
        const std::string staging = (sep == std::string::npos ? std::string(".") : dir_.substr(0, sep)) +
                                    "/.dofscroll-import-" + hex;
        std::string e;
        if (!fs::mkdirsPrivate(staging, &e)) return fail(r, "cannot create a staging directory: " + e);
        const std::string sfile = staging + "/" + id + ".conf";
        bool wrote = fs::createFileExclusive(sfile, text);
        std::vector<std::string> warn;
        ModeSet probe;
        if (wrote) probe = loadModes(staging, &warn);
        fs::removeFile(sfile);
        fs::removeDir(staging);
        if (!wrote) return fail(r, "cannot stage the file");
        const ModeDef *m = probe.find(id);
        if (!m || m->builtin) {
            for (const std::string &w : warn) r.warnings.push_back(relocate(w, staging, dir_));
            return fail(r, "the file is not a valid mode file");
        }
        for (const std::string &w : warn) r.warnings.push_back(relocate(w, staging, dir_));
        r.ok = true;
        return r;
    }
    // inspectImport(), then publishes the content as <dir>/<id>.conf (complete or not at all,
    // never overwriting). The result carries the warnings for the view; the user may remove() the
    // mode again afterwards.
    Result importFile(const std::string &path, const std::string &name = std::string()) {
        Result r = inspectImport(path, name);
        if (!r.ok) return r;
        std::string text, e;
        if (!fs::readFileBounded(path, kMaxImportBytes, fs::ReadOptions(), &text).ok()) return fail(r, "cannot read the file");
        if (!fs::mkdirsPrivate(dir_, &e)) return fail(r, "mkdir: " + e);
        const std::string dest = modelist::modeFilePath(dir_, r.id);
        if (fs::pathExists(dest) || !fs::createFileExclusive(dest, text)) return fail(r, "cannot create " + dest);
        r.path = dest;
        refresh();
        return r;
    }
    // Text for the view before an imported mode is kept.
    static std::string importSafetyNote(const StringProvider &s) {
        return s.tr("A mode file can press any key, including shortcut chords (Ctrl, Alt, Super), whenever a button is "
                    "pressed or the mouse is moved. Only keep files from sources you trust and read the file first.");
    }

    // ---- remove ----
    // Deletes the file of a File or BuiltInOverridden mode (the latter restores the built-in).
    // Built-ins without a file cannot be removed.
    Result remove(const std::string &id) {
        Result r;
        const ModeEntry *e = find(id);
        if (!e) return fail(r, "no such mode");
        if (e->origin == Origin::BuiltIn) return fail(r, "built-in modes cannot be removed");
        const std::string p = e->path;
        if (!fs::removeFile(p)) return fail(r, "cannot remove " + p);
        r.id = id;
        r.path = p;
        r.ok = true;
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
        std::string path, err;
        if (!modelist::createModeFile(dir_, id, content, &path, &err)) return fail(r, err);
        r.ok = true;
        r.id = id;
        r.path = path;
        refresh();
        return r;
    }

    std::string dir_, stamp_;
    ModeSet set_;
    std::vector<ModeEntry> entries_;
    std::vector<std::string> general_;
};

}  // namespace spn::ui
