// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Registry of the settings pages: the main window builds its top-level navigation from it, in
// registry order (the "Profiles" group follows). A page is added by one line in page_registry.cpp.
#pragma once
#include <vector>

#include "ui_context.hpp"

class QWidget;

struct PageFactory {
    const char *id;                            // stable identifier ("channels", ...)
    const char *title;                         // English navigation title, e.g. "Modes"
    QWidget *(*create)(const UiContext &ctx);  // new page, owned by the caller
};

const std::vector<PageFactory> &pageFactories();
