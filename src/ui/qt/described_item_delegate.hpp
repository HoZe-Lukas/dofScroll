// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// List rows with a second line: the name of a joystick mode or button mode and, below it in the muted
// colour and wrapped to the width of the list, its short description ("No description" when it has
// none). The description is the item data kRoleDescription; the name is the display text.
#pragma once
#include <QApplication>
#include <QFontMetrics>
#include <QListWidget>
#include <QPainter>
#include <QStyle>
#include <QStyledItemDelegate>

#include "ui/model/design_tokens.hpp"
#include "theme.hpp"

enum { kRoleDescription = Qt::UserRole + 1, kRoleOrigin = Qt::UserRole + 2 };

class DescribedItemDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const int width = textWidth(opt);
        const QFontMetrics fm(opt.font);
        const QString desc = index.data(kRoleDescription).toString();
        const int descHeight = desc.isEmpty() ? 0 : descriptionRect(fm, width, desc).height();
        const int pad = spn::ui::tokens::kSpaceSm;
        return QSize(opt.rect.width(), std::max(spn::ui::tokens::kRowHeight, fm.height() + descHeight + 2 * pad));
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString title = opt.text;
        opt.text.clear();
        const QWidget *w = opt.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);  // background, selection, icon
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, w);
        const int pad = spn::ui::tokens::kSpaceSm;
        const QFontMetrics fm(opt.font);
        painter->save();
        painter->setFont(opt.font);
        const bool selected = opt.state & QStyle::State_Selected;
        auto nameColor = [&] { return selected ? selectedTextColor(false) : opt.palette.color(QPalette::Text); };
        auto mutedColor = [&] { return selected ? selectedTextColor(true) : opt.palette.color(QPalette::PlaceholderText); };
        painter->setPen(nameColor());
        // the origin ("Built in", "Your file") sits at the right end of the name line, muted
        // (a long origin such as "Your file, replaces the built-in ..." is cut to a third of the row and never covers the name)
        const QString fullOrigin = index.data(kRoleOrigin).toString();
        const QString origin = fm.elidedText(fullOrigin, Qt::ElideRight, textRect.width() / 3);
        const int originWidth = origin.isEmpty() ? 0 : fm.horizontalAdvance(origin) + spn::ui::tokens::kSpaceMd;
        painter->drawText(QRect(textRect.left(), textRect.top() + pad, textRect.width() - originWidth, fm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(title, Qt::ElideRight, textRect.width() - originWidth));
        if (!origin.isEmpty()) {
            painter->setPen(mutedColor());
            painter->drawText(QRect(textRect.left(), textRect.top() + pad, textRect.width(), fm.height()), Qt::AlignRight | Qt::AlignVCenter, origin);
            painter->setPen(nameColor());
        }
        const QString desc = index.data(kRoleDescription).toString();
        if (!desc.isEmpty()) {
            painter->setPen(mutedColor());
            painter->drawText(QRect(textRect.left(), textRect.top() + pad + fm.height(), textRect.width(), textRect.height()),
                              Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, desc);
        }
        painter->restore();
    }

private:
    static int textWidth(const QStyleOptionViewItem &opt) {
        const QWidget *w = opt.widget;
        int width = w ? w->width() : opt.rect.width();
        if (const auto *view = qobject_cast<const QAbstractItemView *>(w)) width = view->viewport()->width();
        return std::max(120, width - 2 * spn::ui::tokens::kSpaceMd - (opt.icon.isNull() ? 0 : 28));
    }
    static QRect descriptionRect(const QFontMetrics &fm, int width, const QString &desc) {
        return fm.boundingRect(QRect(0, 0, width, 10000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, desc);
    }
};
