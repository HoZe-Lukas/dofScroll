// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Translation helper: English source strings are the lookup keys (empty context), matching
// po/de.po. The catalog is converted to .qm at build time (see docs/ui-options.md); without a
// loaded catalog the English text is returned.
#pragma once
#include <QCoreApplication>
#include <QString>
#include <string>

inline QString T(const char *english) { return QCoreApplication::translate("", english); }

// std::string in the models is always UTF-8.
inline QString qs8(const std::string &s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }
