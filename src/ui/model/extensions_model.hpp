// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The User modes page (issue #129, called Extensions in the code): your mode files in one list. These are the joystick
// mode files of the user (`modes/*.conf`) and the button mode files (`buttons/*.conf`). Nothing here is
// executed: an extension is a plain file that the daemon's own loaders read, with their checks and
// warnings. A file can be switched off without losing it: "<id>.conf" becomes "<id>.conf.disabled" (the
// loaders only read names that end in ".conf"), and switching it on renames it back. A switched-off file
// that has the id of a built-in mode lets the built-in mode apply again. Toolkit-free; all file access
// goes through platform/fs.hpp.
//
//   listExtensions(modesDir, buttonModesDir, usedBy)   // both kinds, switched-on and switched-off files
//   setExtensionEnabled(entry, on, &err)               // the rename; never overwrites
//   removeExtension(entry, &err)                       // deletes the file
#pragma once
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

#include "builtin_library.hpp"
#include "modes.hpp"
#include "platform/fs.hpp"
#include "utf8.hpp"

namespace spn::ui {

enum class ExtensionKind { JoystickMode, ButtonMode };

struct ExtensionEntry {
    ExtensionKind kind = ExtensionKind::JoystickMode;
    std::string id;           // the file name without ".conf" / ".conf.disabled"
    std::string label;        // NAME of the file, or the id
    std::string description;  // DESCRIPTION of the file ("" = none)
    std::string path;         // the file as it is now
    std::string dir;          // the folder it lives in
    bool enabled = true;
    // The file is byte-identical to a shipped example (a built-in text, or for a joystick mode a text that
    // an earlier version copied on first start): the loader ignores it and the built-in applies, so it
    // changes nothing. Only set for a switched-on file ("<id>.conf"); the page says so in words.
    bool copyOfShipped = false;
    // Where it is used now (profile sections, buttons): the page says so before it is switched off.
    std::vector<std::string> usedBy;
};

namespace ext_detail {

inline bool endsWith(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// NAME and DESCRIPTION of a mode file: the first value of each key, comments and other keys skipped.
inline void readLabels(const std::string &path, std::string *name, std::string *description) {
    std::string text;
    if (!fs::readFileBounded(path, 16 * 1024, fs::ReadOptions(), &text).ok()) return;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = utf8::trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = utf8::upperAscii(utf8::trim(line.substr(0, eq)));
        const std::string value = utf8::trim(line.substr(eq + 1));
        if (key == "NAME" && name->empty()) *name = value;
        if (key == "DESCRIPTION" && description->empty()) *description = value;
    }
}

// True if the file has exactly the text of a shipped example with this id, the same rule the loaders
// use (loadModes, loadButtonModes) to leave the file out.
inline bool isShippedCopy(ExtensionKind kind, const std::string &id, const std::string &path) {
    std::string text;
    if (!fs::readFileBounded(path, 64 * 1024, fs::ReadOptions(), &text).ok()) return false;
    if (kind == ExtensionKind::ButtonMode) return matchesBuiltinText(builtinButtonModeFiles(), id, text);
    return matchesBuiltinText(builtinModeFiles(), id, text) || matchesBuiltinText(legacyModeFiles(), id, text);
}

}  // namespace ext_detail

using UsedByFn = std::function<std::vector<std::string>(ExtensionKind, const std::string &id)>;

inline constexpr const char *kDisabledSuffix = ".disabled";

// The files of one folder, in name order: "<id>.conf" (switched on) and "<id>.conf.disabled" (switched
// off) with a valid id. Files with other names are not extensions and are not listed.
inline void listExtensionDir(ExtensionKind kind, const std::string &dir, const UsedByFn &usedBy, std::vector<ExtensionEntry> *out) {
    if (dir.empty()) return;
    std::vector<std::string> names;
    bool capped = false;
    if (!fs::listDir(dir, 512, &names, &capped)) return;
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        const bool off = ext_detail::endsWith(n, std::string(".conf") + kDisabledSuffix);
        const bool on = !off && ext_detail::endsWith(n, ".conf");
        if (!on && !off) continue;
        const std::string id = n.substr(0, n.size() - (off ? 5 + std::string(kDisabledSuffix).size() : 5));
        if (!validModeId(id)) continue;
        ExtensionEntry e;
        e.kind = kind;
        e.id = id;
        e.dir = dir;
        e.path = dir + "/" + n;
        e.enabled = on;
        ext_detail::readLabels(e.path, &e.label, &e.description);
        if (on) e.copyOfShipped = ext_detail::isShippedCopy(kind, id, e.path);
        if (e.label.empty()) e.label = id;
        if (usedBy) e.usedBy = usedBy(kind, id);
        out->push_back(e);
    }
}

// Both kinds: the joystick mode files first, then the button mode files.
inline std::vector<ExtensionEntry> listExtensions(const std::string &modesDir, const std::string &buttonModesDir, const UsedByFn &usedBy = nullptr) {
    std::vector<ExtensionEntry> out;
    listExtensionDir(ExtensionKind::JoystickMode, modesDir, usedBy, &out);
    listExtensionDir(ExtensionKind::ButtonMode, buttonModesDir, usedBy, &out);
    return out;
}

// The path the file has when it is switched on / off.
inline std::string extensionPath(const ExtensionEntry &e, bool enabled) {
    return e.dir + "/" + e.id + ".conf" + (enabled ? "" : kDisabledSuffix);
}

struct ExtensionResult {
    bool ok = false;
    std::string error;  // English source text
    std::string path;   // the file after the change
};

// Switches the file on or off by renaming it. Refuses when the other name is already taken (two files
// for one id): nothing is overwritten.
inline ExtensionResult setExtensionEnabled(const ExtensionEntry &e, bool enabled) {
    ExtensionResult r;
    r.path = e.path;
    if (e.enabled == enabled) {
        r.ok = true;
        return r;
    }
    const std::string target = extensionPath(e, enabled);
    if (fs::pathExists(target)) {
        r.error = enabled ? "a file with this name is already switched on" : "a switched-off file with this name already exists";
        return r;
    }
    if (!fs::renamePath(e.path, target)) {
        r.error = "the file could not be renamed";
        return r;
    }
    r.ok = true;
    r.path = target;
    return r;
}

// Deletes the file (switched on or off).
inline ExtensionResult removeExtension(const ExtensionEntry &e) {
    ExtensionResult r;
    if (!fs::removeFile(e.path)) {
        r.error = "the file could not be deleted";
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace spn::ui
