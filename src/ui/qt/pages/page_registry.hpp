// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Registry of the settings pages: the main window looks a page up by its English navigation
// title (the same string the window shows, translated there) and falls back to a placeholder.
#pragma once
#include <vector>

#include "ui_context.hpp"

class QWidget;

struct PageFactory {
    const char *title;                         // English navigation title, e.g. "Modes"
    QWidget *(*create)(const UiContext &ctx);  // new page, owned by the caller
};

const std::vector<PageFactory> &pageFactories();
