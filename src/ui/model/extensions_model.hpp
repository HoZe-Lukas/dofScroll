// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Extensions: what dofscroll can be extended with, in one list (toolkit-free). An extension is a
// plain file: a joystick mode (modes/<id>.conf) or a button mode (buttons/<id>.conf). Nothing
// executable. The list shows where each one comes from (built in, a file of the user, an example
// that ships with dofscroll but is not installed in the user's folder) and offers the two actions
// that make sense: install an example, remove a file. Built on ModeCatalog and ButtonModeCatalog,
// which do all file access.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "button_mode_catalog.hpp"
#include "mode_catalog.hpp"
#include "platform/fs.hpp"
#include "strings.hpp"

namespace spn::ui {

struct ExtensionEntry {
    enum class Kind { JoystickMode, ButtonMode };
    enum class Origin { BuiltIn, UserFile, ExampleAvailable };
    Kind kind = Kind::JoystickMode;
    Origin origin = Origin::UserFile;
    std::string id, label, description;
    std::string path;      // UserFile: the file; ExampleAvailable: the example file to install
    size_t warnings = 0;   // loader warnings of a user file
    bool replacesBuiltIn = false;  // a mode file with the id of a built-in mode

    bool removable() const { return origin == Origin::UserFile; }
    bool installable() const { return origin == Origin::ExampleAvailable; }
};

inline std::string extensionKindText(ExtensionEntry::Kind k, const StringProvider &s) {
    return k == ExtensionEntry::Kind::JoystickMode ? s.tr("Joystick mode") : s.tr("Button mode");
}
inline std::string extensionOriginText(const ExtensionEntry &e, const StringProvider &s) {
    switch (e.origin) {
        case ExtensionEntry::Origin::BuiltIn: return s.tr("built in");
        case ExtensionEntry::Origin::UserFile:
            return e.replacesBuiltIn ? s.tr("your file, replaces the built-in mode") : s.tr("your file");
        case ExtensionEntry::Origin::ExampleAvailable: return s.tr("example, not installed");
    }
    return std::string();
}

// All joystick and button modes, then the shipped examples that are not installed yet. Order:
// joystick modes (catalog order), button modes, examples. examplesDir may be empty.
inline std::vector<ExtensionEntry> buildExtensions(const ModeCatalog &modes, const ButtonModeCatalog &buttons,
                                                   const std::string &examplesDir) {
    std::vector<ExtensionEntry> out;
    for (const ModeCatalog::ModeEntry &m : modes.modes()) {
        ExtensionEntry e;
        e.kind = ExtensionEntry::Kind::JoystickMode;
        e.id = m.id;
        e.label = m.label;
        e.description = m.description;
        e.path = m.path;
        e.warnings = m.warnings.size();
        e.origin = m.origin == ModeCatalog::Origin::BuiltIn ? ExtensionEntry::Origin::BuiltIn : ExtensionEntry::Origin::UserFile;
        e.replacesBuiltIn = m.origin == ModeCatalog::Origin::BuiltInOverridden;
        out.push_back(e);
    }
    for (const ButtonModeCatalog::Entry &b : buttons.buttonModes()) {
        ExtensionEntry e;
        e.kind = ExtensionEntry::Kind::ButtonMode;
        e.id = b.id;
        e.label = b.label;
        e.description = b.description;
        e.path = b.path;
        e.warnings = b.warnings.size();
        out.push_back(e);
    }
    if (!examplesDir.empty()) {
        const ModeSet exModes = loadModes(examplesDir + "/modes", nullptr);
        for (const ModeDef &m : exModes.modes) {
            if (m.builtin || modes.find(m.id)) continue;
            ExtensionEntry e;
            e.kind = ExtensionEntry::Kind::JoystickMode;
            e.origin = ExtensionEntry::Origin::ExampleAvailable;
            e.id = m.id;
            e.label = m.label.empty() ? m.id : m.label;
            e.description = m.description;
            e.path = examplesDir + "/modes/" + m.id + ".conf";
            out.push_back(e);
        }
        const ButtonModeSet exButtons = loadButtonModes(examplesDir + "/buttons", nullptr);
        for (const ButtonMode &b : exButtons.modes) {
            if (buttons.find(b.id)) continue;
            ExtensionEntry e;
            e.kind = ExtensionEntry::Kind::ButtonMode;
            e.origin = ExtensionEntry::Origin::ExampleAvailable;
            e.id = b.id;
            e.label = b.label;
            e.description = b.description;
            e.path = b.path;
            out.push_back(e);
        }
    }
    return out;
}

struct ExtensionResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;  // loader warnings of an installed example
};

// Copies the example into the user's folder (checked like an import, never overwrites).
inline ExtensionResult installExtension(ModeCatalog &modes, ButtonModeCatalog &buttons, const ExtensionEntry &e) {
    ExtensionResult r;
    if (!e.installable()) {
        r.error = "this is not an example that can be installed";
        return r;
    }
    if (e.kind == ExtensionEntry::Kind::JoystickMode) {
        const ModeCatalog::Result x = modes.importFile(e.path, e.id);
        r.ok = x.ok;
        r.error = x.error;
        r.warnings = x.warnings;
    } else {
        const ButtonModeCatalog::Result x = buttons.importFile(e.path, e.id);
        r.ok = x.ok;
        r.error = x.error;
        r.warnings = x.warnings;
    }
    return r;
}

// Deletes the file of a user extension (a mode file that replaces a built-in mode brings the
// built-in mode back).
inline ExtensionResult removeExtension(ModeCatalog &modes, ButtonModeCatalog &buttons, const ExtensionEntry &e) {
    ExtensionResult r;
    if (!e.removable()) {
        r.error = "only your own files can be removed";
        return r;
    }
    if (e.kind == ExtensionEntry::Kind::JoystickMode) {
        const ModeCatalog::Result x = modes.remove(e.id);
        r.ok = x.ok;
        r.error = x.error;
    } else {
        const ButtonModeCatalog::Result x = buttons.remove(e.id);
        r.ok = x.ok;
        r.error = x.error;
    }
    return r;
}

}  // namespace spn::ui
