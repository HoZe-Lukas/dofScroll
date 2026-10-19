// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Registry of the settings window pages. Each page registers itself with one line in
// page_registry.cpp; the main window asks for a page by its navigation title and falls back to a
// placeholder when none is registered.
#pragma once
#include <QString>

class QWidget;

// Returns the page registered for this (already translated) navigation title, or nullptr.
QWidget *createRegisteredPage(const QString &translatedTitle);
