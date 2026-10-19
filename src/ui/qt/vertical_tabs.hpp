// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Style for a QTabBar on the west side that keeps the text horizontal. Qt rotates the label of a
// vertical tab by 90 degrees; this proxy lays the tab out with swapped width and height and draws
// the label as if the tab were on top, so the left tabs read like a normal list of short names.
#pragma once
#include <QProxyStyle>
#include <QStyleOptionTab>

class WestTabStyle : public QProxyStyle {
public:
    using QProxyStyle::QProxyStyle;

    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget) const override {
        QSize s = QProxyStyle::sizeFromContents(type, option, size, widget);
        if (type == CT_TabBarTab) s.transpose();
        return s;
    }

    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget) const override {
        if (element == CE_TabBarTabLabel) {
            if (const auto *tab = qstyleoption_cast<const QStyleOptionTab *>(option)) {
                QStyleOptionTab horizontal(*tab);
                horizontal.shape = QTabBar::RoundedNorth;
                QProxyStyle::drawControl(element, &horizontal, painter, widget);
                return;
            }
        }
        QProxyStyle::drawControl(element, option, painter, widget);
    }
};
