// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// A button of a dialog that has no main action: no button looks like the default button (Qt makes the first auto-default button
// the default, painted in the accent colour), and Enter still activates the button that has the focus. Qt itself lets Enter
// click only a default or auto-default button, so without this a button that is no auto-default would only react to Space.
#pragma once
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QPushButton>

class EnterActivates : public QObject {
public:
    explicit EnterActivates(QPushButton *button) : QObject(button), button_(button) {}

protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (o != button_ || e->type() != QEvent::KeyPress) return false;
        const auto *k = static_cast<QKeyEvent *>(e);
        if ((k->key() != Qt::Key_Return && k->key() != Qt::Key_Enter) || (k->modifiers() & ~Qt::KeypadModifier) != Qt::NoModifier) return false;
        if (button_->isEnabled()) button_->click();
        return true;
    }

private:
    QPushButton *button_;
};

// Not the default, not an auto-default; Enter on the focused button clicks it.
inline void enterOnlyButton(QPushButton *b) {
    b->setAutoDefault(false);
    b->setDefault(false);
    b->installEventFilter(new EnterActivates(b));
}
