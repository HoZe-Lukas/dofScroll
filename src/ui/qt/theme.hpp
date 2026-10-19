// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Qt side of the theme: turns the toolkit-free colours (src/ui/model/theme_colors.hpp) into a
// QPalette, applies a theme to the whole application at once, and draws the SVG icons in the
// colour of the current palette (the SVG files are drawn in black and tinted at run time, so one
// file serves both themes and every contrast).
#pragma once
#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QString>

#include "ui/model/theme_colors.hpp"
#include "ui/model/ui_settings.hpp"

// The palette of a theme (all roles that the widgets use, the disabled group included).
QPalette paletteFor(const spn::ui::ThemeColors &colors);

// Applies a theme to the application: Light and Dark switch to the Fusion style (the native styles
// of Windows and macOS ignore a custom palette) with the dofscroll palette; System restores the
// style and the palette the application started with. Safe to call repeatedly.
void applyTheme(spn::ui::Theme theme);

// An icon from an SVG resource, tinted with `color`, drawn at `size` device-independent pixels
// (with a 2x image for high-DPI screens). A null icon if the SVG module is not available or the
// resource is not valid.
QIcon themedIcon(const QString &resource, const QColor &color, int size);
