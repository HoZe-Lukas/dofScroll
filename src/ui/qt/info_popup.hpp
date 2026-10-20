// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The popover of an "i" info button: a small frame with a wrapped text (name, origin, description, what
// a joystick mode does) that opens below the button and closes on the next click or Esc. Plain text
// only (the descriptions come from files).
#pragma once
#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QPoint>
#include <QString>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include "ui/model/design_tokens.hpp"
#include "ui_i18n.hpp"

class InfoPopup : public QFrame {
public:
    explicit InfoPopup(QWidget *parent) : QFrame(parent, Qt::Popup) {
        setObjectName(QStringLiteral("infoPopup"));
        setAccessibleName(T("Information"));
        setFrameShape(QFrame::StyledPanel);
        setAutoFillBackground(true);
        auto *l = new QVBoxLayout(this);
        namespace tk = spn::ui::tokens;
        l->setContentsMargins(tk::kCardPadding, tk::kSpaceMd, tk::kCardPadding, tk::kSpaceMd);
        label_ = new QLabel(this);
        label_->setWordWrap(true);
        label_->setTextFormat(Qt::PlainText);
        label_->setMaximumWidth(460);
        label_->setMinimumWidth(320);
        l->addWidget(label_);
    }
    QString text() const { return label_->text(); }

    // Opens the popover with `text` below `anchor`. The popover is owned by the anchor and deleted when it closes.
    static InfoPopup *open(QWidget *anchor, const QString &text) {
        auto *p = new InfoPopup(anchor);
        p->setAttribute(Qt::WA_DeleteOnClose);
        p->label_->setText(text);
        p->adjustSize();
        p->move(anchor->mapToGlobal(QPoint(0, anchor->height() + 4)));
        p->show();
        return p;
    }
    // The text of the popover that is open now ("" if none); for the self-test.
    static QString openText() {
        for (QWidget *w : QApplication::topLevelWidgets())
            if (w->objectName() == QLatin1String("infoPopup")) return static_cast<InfoPopup *>(w)->text();
        return QString();
    }

private:
    QLabel *label_ = nullptr;
};

// The "i" button: small, round look from the style sheet, an accessible name and a tooltip.
inline QToolButton *makeInfoButton(QWidget *parent, const QString &accessibleName, const QString &tip) {
    auto *b = new QToolButton(parent);
    b->setText(QStringLiteral("i"));
    b->setObjectName(QStringLiteral("infoButton"));
    b->setAccessibleName(accessibleName);
    b->setToolTip(tip);
    b->setAutoRaise(false);
    b->setFixedWidth(spn::ui::tokens::kControlHeight);
    return b;
}
