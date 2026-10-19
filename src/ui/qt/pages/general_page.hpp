// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "General" page: start at login, language, read-only status block, config file, version.
#pragma once
#include <QWidget>

#include "ui_context.hpp"

QWidget *createGeneralPage(const UiContext &ctx);
