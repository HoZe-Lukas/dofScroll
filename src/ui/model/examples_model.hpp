// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Examples that ship with dofscroll and are not installed in the user's folder yet: the choices of
// "New from example..." on the Joystick modes and Button modes pages, and the origin texts of the
// list entries ("Built in", "Your file", "Example, not installed yet"). Built on ModeCatalog and
// ButtonModeCatalog, which do all file access; an example is copied like an import (checked by the
// loader, never overwrites). Toolkit-free.
#pragma once
#include <algorithm>
#include <string>
#include <vector>

#include "button_mode_catalog.hpp"
#include "mode_catalog.hpp"
#include "platform/fs.hpp"
#include "strings.hpp"

namespace spn::ui {

struct Example {
    std::string id, label, description;
    std::string path;  // the example file to copy
};

// Joystick mode examples (<examplesDir>/modes/*.conf) whose id is not loaded yet (neither built in nor
// a file of the user). examplesDir may be empty.
inline std::vector<Example> availableModeExamples(const ModeCatalog &modes, const std::string &examplesDir) {
    std::vector<Example> out;
    if (examplesDir.empty()) return out;
    const ModeSet ex = loadModes(examplesDir + "/modes", nullptr);
    for (const ModeDef &m : ex.modes) {
        if (m.builtin || modes.find(m.id)) continue;
        out.push_back({m.id, m.label.empty() ? m.id : m.label, m.description, examplesDir + "/modes/" + m.id + ".conf"});
    }
    return out;
}

// Button mode examples (<examplesDir>/buttons/*.conf) whose id is not installed yet.
inline std::vector<Example> availableButtonModeExamples(const ButtonModeCatalog &buttons, const std::string &examplesDir) {
    std::vector<Example> out;
    if (examplesDir.empty()) return out;
    const ButtonModeSet ex = loadButtonModes(examplesDir + "/buttons", nullptr);
    for (const ButtonMode &b : ex.modes) {
        if (buttons.find(b.id)) continue;
        out.push_back({b.id, b.label.empty() ? b.id : b.label, b.description, b.path});
    }
    return out;
}

struct ExampleResult {
    bool ok = false;
    std::string error;
    std::string id;                     // the installed mode
    std::vector<std::string> warnings;  // loader warnings of the installed file
};

// Copies the example into the user's folder (checked like an import, never overwrites).
inline ExampleResult installModeExample(ModeCatalog &modes, const Example &e) {
    ExampleResult r;
    const ModeCatalog::Result x = modes.importFile(e.path, e.id);
    r.ok = x.ok;
    r.error = x.error;
    r.id = x.id;
    r.warnings = x.warnings;
    return r;
}
inline ExampleResult installButtonModeExample(ButtonModeCatalog &buttons, const Example &e) {
    ExampleResult r;
    const ButtonModeCatalog::Result x = buttons.importFile(e.path, e.id);
    r.ok = x.ok;
    r.error = x.error;
    r.id = x.id;
    r.warnings = x.warnings;
    return r;
}

// Origin text of an entry of the joystick mode list.
inline std::string modeOriginText(ModeCatalog::Origin o, const StringProvider &s) {
    switch (o) {
        case ModeCatalog::Origin::BuiltIn: return s.tr("Built in");
        case ModeCatalog::Origin::File: return s.tr("Your file");
        case ModeCatalog::Origin::BuiltInOverridden: return s.tr("Your file, replaces the built-in joystick mode");
    }
    return std::string();
}
// Button modes are always files of the user.
inline std::string buttonModeOriginText(const StringProvider &s) { return s.tr("Your file"); }
// Origin of an example that is offered but not installed.
inline std::string exampleOriginText(const StringProvider &s) { return s.tr("Example, not installed yet"); }

}  // namespace spn::ui
