// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Page registry of the settings window: a short list of (id, title, factory). The main window
// builds its navigation from it (top-level entries, in this order, followed by the "Profiles"
// group). A page is added by one line in page_registry.cpp.
#pragma once
#include <QString>
#include <string>

#include "ui/model/action_runner.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/strings.hpp"
#include "ui_i18n.hpp"

class QWidget;

// StringProvider for the toolkit-free models: translates through the Qt catalog.
class QtStringProvider : public spn::ui::StringProvider {
public:
    std::string tr(const char *source) const override { return T(source).toUtf8().toStdString(); }
};

// What every page gets from the main window (all owned by it, outliving the pages).
struct PageContext {
    spn::ui::SettingsModel &settings;    // the configuration; edit through it, then save()
    spn::ui::ActionRunner &actions;      // open files, autostart, reload
    const spn::ui::StatusModel &status;  // daemon status (updated by the main window)
    const spn::ui::StringProvider &strings;
    QString configPath;                  // file the settings model was loaded from
};

using PageFactory = QWidget *(*)(PageContext &);

struct PageInfo {
    const char *id;     // stable identifier ("channels", "general", ...)
    const char *title;  // English source string of the navigation entry
    PageFactory create;
};

// Pages in navigation order.
const PageInfo *pageRegistry(int *count);
