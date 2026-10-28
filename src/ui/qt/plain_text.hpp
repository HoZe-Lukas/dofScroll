// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Foreign strings (profile names, device names from the USB descriptor, window titles, mode descriptions, daemon
// status text) must be shown literally. Qt renders QLabel and QMessageBox text with Qt::AutoText and detects rich
// text in tooltips, so "<b>x</b>" or "<img src=...>" in the data would become markup. These helpers force plain text.
#pragma once
#include <QAction>
#include <QLabel>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QString>
#include <QTreeWidgetItem>
#include <QWidget>

namespace spn::ui::qt {

// A tooltip that is never taken for markup: the text is escaped and wrapped in a rich-text block that keeps line
// breaks and runs of spaces.
inline QString plainToolTip(const QString &text) {
    if (text.isEmpty()) return text;
    return QStringLiteral("<p style='white-space:pre'>") + text.toHtmlEscaped() + QStringLiteral("</p>");
}

inline void setPlainToolTip(QWidget *w, const QString &text) { w->setToolTip(plainToolTip(text)); }
inline void setPlainToolTip(QAction *a, const QString &text) { a->setToolTip(plainToolTip(text)); }
inline void setPlainToolTip(QListWidgetItem *it, const QString &text) { it->setToolTip(plainToolTip(text)); }
inline void setPlainToolTip(QTreeWidgetItem *it, int column, const QString &text) { it->setToolTip(column, plainToolTip(text)); }

// A label that shows its text literally.
inline void setPlainLabel(QLabel *l, const QString &text) {
    l->setTextFormat(Qt::PlainText);
    l->setText(text);
}

// QMessageBox::question/warning/information with a literal text.
inline QMessageBox::StandardButton plainBox(QMessageBox::Icon icon, QWidget *parent, const QString &title, const QString &text,
                                            QMessageBox::StandardButtons buttons = QMessageBox::NoButton,
                                            QMessageBox::StandardButton def = QMessageBox::NoButton) {
    if (buttons == QMessageBox::NoButton) buttons = icon == QMessageBox::Question ? (QMessageBox::Yes | QMessageBox::No) : QMessageBox::Ok;
    QMessageBox box(icon, title, text, buttons, parent);
    box.setTextFormat(Qt::PlainText);
    if (def != QMessageBox::NoButton) box.setDefaultButton(def);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

}  // namespace spn::ui::qt
