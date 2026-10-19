// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Translation helper: English source strings are the lookup keys (empty context), matching
// po/de.po. The catalog is converted to .qm at build time (see docs/ui-options.md); without a
// loaded catalog the English text is returned.
#pragma once
#include <QCoreApplication>
#include <QString>

inline QString T(const char *english) { return QCoreApplication::translate("", english); }
