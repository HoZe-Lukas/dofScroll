// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// A list | editor splitter with a column of buttons beside it. While the page is wide enough the buttons stay in one column
// at the side; below that (a small window, wide glyphs of a Windows font, a long translation) the column goes under the
// splitter and its buttons are laid out in rows, so the splitter keeps the whole width and the page needs no horizontal
// scroll bar. The decision uses the width the splitter asks for (`splitNeed`), not a pixel number.
#pragma once
#include <QBoxLayout>
#include <QEvent>
#include <QGridLayout>
#include <QObject>
#include <QPushButton>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <vector>

namespace spn::ui {

// Buttons in a grid with a settable number of columns (one = the usual column).
class ButtonGrid : public QWidget {
public:
    explicit ButtonGrid(QWidget *parent = nullptr) : QWidget(parent), grid_(new QGridLayout(this)) { grid_->setContentsMargins(0, 0, 0, 0); }
    QPushButton *add(QPushButton *b) {
        b->setParent(this);
        buttons_.push_back(b);
        place();
        return b;
    }
    // the width of the widest button: one column needs this much
    int columnWidth() const {
        int w = 0;
        for (const QPushButton *b : buttons_) w = std::max(w, b->sizeHint().width());
        return w;
    }
    void setColumns(int columns) {
        columns = std::max(1, columns);
        if (columns == columns_) return;
        columns_ = columns;
        place();
    }
    int gap() const { return grid_->horizontalSpacing() >= 0 ? grid_->horizontalSpacing() : 6; }

private:
    void place() {
        for (QPushButton *b : buttons_) grid_->removeWidget(b);
        for (size_t i = 0; i < buttons_.size(); ++i) grid_->addWidget(buttons_[i], int(i) / columns_, int(i) % columns_);
        // in the column layout the last row (a stretch) pushes the buttons to the top
        grid_->setRowStretch(int((buttons_.size() + size_t(columns_) - 1) / size_t(columns_)), 1);
    }
    QGridLayout *grid_;
    std::vector<QPushButton *> buttons_;
    int columns_ = 1;
};

// Switches `row` (splitter, then the button grid) between side by side and one under the other when `page` is resized.
class NarrowRow : public QObject {
public:
    NarrowRow(QWidget *page, QBoxLayout *row, ButtonGrid *buttons, std::function<int()> splitNeed)
        : QObject(page), page_(page), row_(row), buttons_(buttons), need_(std::move(splitNeed)) {
        page->installEventFilter(this);
    }
    void update() {
        const QMargins m = page_->layout() ? page_->layout()->contentsMargins() : QMargins();
        const int avail = page_->width() - m.left() - m.right();
        const int side = buttons_->columnWidth() + row_->spacing();
        const bool wide = avail >= need_() + side;
        row_->setDirection(wide ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
        // under the splitter: as many columns as fit
        buttons_->setColumns(wide ? 1 : std::max(1, (avail + buttons_->gap()) / (buttons_->columnWidth() + buttons_->gap())));
    }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (obj == page_ && (ev->type() == QEvent::Resize || ev->type() == QEvent::FontChange)) update();
        return QObject::eventFilter(obj, ev);
    }

private:
    QWidget *page_;
    QBoxLayout *row_;
    ButtonGrid *buttons_;
    std::function<int()> need_;
};

}  // namespace spn::ui
