// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Style for a QTabBar on the west side that keeps the text horizontal. Qt rotates the label of a
// vertical tab by 90 degrees; this proxy lays the tab out with swapped width and height and draws
// the label as if the tab were on top, so the left tabs read like a normal list of short names.
#pragma once
#include <QPainter>
#include <QProxyStyle>
#include <QStyleOptionTab>

class WestTabStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

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
