// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Registry of the settings pages: the main window builds its tabs from it (the category picks the
// left tab, the registry order gives the order of the top tabs inside it). A page is added by one
// line in page_registry.cpp.
#pragma once
#include <vector>

#include "ui_context.hpp"

class QWidget;

struct PageFactory {
    const char *id;                            // stable identifier ("channels", ...)
    const char *title;                         // English tab title, e.g. "Modes"
    const char *category;                      // navigation category ("profiles", "modes", "device", "settings"), see
                                               // src/ui/model/navigation_model.hpp
    QWidget *(*create)(const UiContext &ctx);  // new page, owned by the caller
};

const std::vector<PageFactory> &pageFactories();
