// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Is the keyboard focus in a field with an undo of its own? Then Ctrl+Z (Cmd+Z) belongs to that field and does not
// trigger the page's Undo (src/ui/model/undo_rules.hpp).
#pragma once
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

inline bool focusInTextInput() {
    QWidget *w = QApplication::focusWidget();
    if (!w) return false;
    if (qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w) || qobject_cast<QTextEdit *>(w) ||
        qobject_cast<QPlainTextEdit *>(w))
        return true;
    auto *c = qobject_cast<QComboBox *>(w);
    return c && c->isEditable();
}
