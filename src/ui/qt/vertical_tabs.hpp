// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Style for a QTabBar on the west side that keeps the text horizontal. Qt rotates the label of a
// vertical tab by 90 degrees; this proxy lays the tab out with swapped width and height and draws
// the label as if the tab were on top, so the left tabs read like a normal list of short names.
#pragma once
#include <QPainter>
#include <QKeyEvent>
#include <QProxyStyle>
#include <QTabBar>
#include <QStyleOptionTab>

#include "ui/model/theme_colors.hpp"

// The tab bar of the left categories: besides the Left and Right keys of every QTabBar, the Up and Down
// keys (and Home and End) move between the entries, as a vertical list is expected to behave.
class WestTabBar : public QTabBar {
public:
    using QTabBar::QTabBar;

protected:
    void keyPressEvent(QKeyEvent *e) override {
        int target = -1;
        switch (e->key()) {
            case Qt::Key_Down: target = currentIndex() + 1; break;
            case Qt::Key_Up: target = currentIndex() - 1; break;
            case Qt::Key_Home: target = 0; break;
            case Qt::Key_End: target = count() - 1; break;
            default: QTabBar::keyPressEvent(e); return;
        }
        // skip a disabled entry, stay at the ends
        const int step = target >= currentIndex() ? 1 : -1;
        while (target >= 0 && target < count() && !isTabEnabled(target)) target += step;
        if (target >= 0 && target < count()) setCurrentIndex(target);
        e->accept();
    }
};

class WestTabStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    // The colour of the keyboard focus ring of the current tab: the accent when it has 3:1 against the
    // window, else the text colour (which the palettes keep at 7:1), like the 2 px ring of the other controls.
    static QColor focusRingColor(const QPalette &pal) {
        const auto rgb = [](const QColor &c) { return spn::ui::Rgb{uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue())}; };
        const QColor accent = pal.color(QPalette::Highlight), window = pal.color(QPalette::Window);
        return spn::ui::contrastRatio(rgb(accent), rgb(window)) >= 3.0 ? accent : pal.color(QPalette::WindowText);
    }

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget) const override {
        QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_TabBarTab) {
            s.transpose();
            s.rwidth() += 24;   // room around icon and text, and a wider sidebar
            s.rheight() += 14;  // a tall, easy to hit entry
        }
        return s;
    }

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override {
        if (element == CE_TabBarTabShape) {
            // A flat rounded pill instead of a raised tab: accent tint when selected, soft grey on hover.
            if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
                const bool selected = tab->state & State_Selected;
                const bool hover = tab->state & State_MouseOver;
                if (selected || hover) {
                    QColor c = selected ? tab->palette.color(QPalette::Highlight) : tab->palette.color(QPalette::WindowText);
                    c.setAlpha(selected ? 40 : 18);
                    painter->save();
                    painter->setRenderHint(QPainter::Antialiasing);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(c);
                    painter->drawRoundedRect(QRectF(tab->rect).adjusted(0, 2, 0, -2), 9, 9);
                    painter->restore();
                }
                // keyboard focus: a 2 px ring around the current tab, never left out
                if (selected && (tab->state & State_HasFocus)) {
                    painter->save();
                    painter->setRenderHint(QPainter::Antialiasing);
                    painter->setBrush(Qt::NoBrush);
                    painter->setPen(QPen(focusRingColor(tab->palette), 2));
                    painter->drawRoundedRect(QRectF(tab->rect).adjusted(1, 3, -1, -3), 9, 9);
                    painter->restore();
                }
                return;
            }
        }
        if (element == CE_TabBarTabLabel) {
            // Icon and text aligned to the left edge, so that all entries line up.
            if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
                const bool selected = tab->state & State_Selected;
                QRect r = tab->rect.adjusted(14, 0, -10, 0);
                if (!tab->icon.isNull()) {
                    const QSize isz = tab->iconSize.isValid() ? tab->iconSize : QSize(20, 20);
                    const QPixmap pm = tab->icon.pixmap(isz, selected ? QIcon::Selected : QIcon::Normal);
                    painter->drawPixmap(QRect(QPoint(r.left(), r.center().y() - isz.height() / 2 + 1), isz), pm);
                    r.setLeft(r.left() + isz.width() + 12);
                }
                painter->save();
                QFont f = painter->font();
                f.setWeight(selected ? QFont::DemiBold : QFont::Normal);
                painter->setFont(f);
                painter->setPen(tab->palette.color(QPalette::WindowText));
                painter->drawText(r, Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, tab->text);
                painter->restore();
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};
