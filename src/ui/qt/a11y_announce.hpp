// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tells a screen reader about a result that is otherwise only visible: autosave errors, the result of
// an import, take over or export, errors of an edit. The status bar changes silently for assistive
// technology; this raises an Alert event on a one-pixel label of the window whose accessible name is the
// text, so the reader speaks it. Without an active screen reader it does nothing visible.
#pragma once
#include <QAccessible>
#include <QAccessibleEvent>
#include <QApplication>
#include <QLabel>
#include <QString>
#include <QWidget>

namespace spn::ui {

// The label that carries the announced text: one per top-level window, created on first use, empty (draws
// nothing), never focusable.
inline QLabel *announcerOf(QWidget *window) {
    if (!window) return nullptr;
    QLabel *l = window->findChild<QLabel *>(QStringLiteral("a11yAnnouncer"), Qt::FindDirectChildrenOnly);
    if (l) return l;
    l = new QLabel(window);
    l->setObjectName(QStringLiteral("a11yAnnouncer"));
    l->setFocusPolicy(Qt::NoFocus);
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->setGeometry(0, 0, 1, 1);
    l->show();
    return l;
}

// `where`: any widget of the window (null: the active window). Empty text and no window do nothing.
inline void announce(QWidget *where, const QString &text) {
    if (text.isEmpty()) return;
    QWidget *win = where ? where->window() : QApplication::activeWindow();
    QLabel *l = announcerOf(win);
    if (!l) return;
    l->setAccessibleName(text);
    if (!QAccessible::isActive()) return;
    QAccessibleEvent ev(l, QAccessible::Alert);
    QAccessible::updateAccessibility(&ev);
}

}  // namespace spn::ui
