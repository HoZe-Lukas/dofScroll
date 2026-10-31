// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Translation helper: English source strings are the lookup keys (empty context), matching
// po/de.po. The catalog is converted to .qm at build time (see docs/ui-options.md); without a
// loaded catalog the English text is returned.
#pragma once
#include <QCoreApplication>
#include <QString>
#include <string>

#include "ui/model/status_text.hpp"

inline QString T(const char *english) { return QCoreApplication::translate("", english); }

// The name of a key as the German keyboard shows it ("Delete" is "Entf" on the key and "Löschen" on a button): msgctxt "key" in the catalog
// (lconvert makes it the disambiguation comment of the entry). Only the displayed name; what files hold stays English.
inline QString TK(const char *english) { return QCoreApplication::translate("", english, "key"); }

// std::string in the models is always UTF-8.
inline QString qs8(const std::string &s) { return QString::fromUtf8(s.data(), qsizetype(s.size())); }

// A status message with its symbol (error, warning or success): never colour alone.
inline QString mark(spn::ui::StatusKind kind, const QString &text) {
    return text.isEmpty() ? text : qs8(spn::ui::statusMark(kind)) + QLatin1Char(' ') + text;
}
