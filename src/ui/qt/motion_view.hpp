// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The one look of "what a joystick mode or a profile does", used by every page that shows it (Joystick modes
// list and detail, General tab of a profile, Overview cards, Button modes page): the models are
// src/ui/model/motion_summary_model.hpp (six motions, one fixed order, one symbol each),
// button_chip_model.hpp (a button as "1 Space") and profile_assignment_model.hpp (the effective assignment).
//
//   motionview::layoutChips / paintChips   chip drawing shared by the list delegate and the widgets
//   MotionStrip   six small chips in a row (wraps), for the Overview cards
//   ButtonChips   the buttons as numbered chips (muted when unassigned, always with the word)
//   MotionGrid    the 3x2 panel of motion cards: name, symbol and action, how, and where it comes from
//
// All lengths come from the font metrics, so the views follow the font size and wrap instead of needing a
// horizontal scroll bar. A muted chip (motion off or not used, button unassigned) has a dashed outline and keeps
// its word: never colour alone. Texts of joystick mode files are drawn as plain text.
#pragma once
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QGridLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QList>
#include <QPainter>
#include <QPen>
#include <QStringList>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>
#include <vector>

#include "ui/model/button_chip_model.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/motion_summary_model.hpp"
#include "ui_i18n.hpp"

namespace motionview {

struct Chip {
    QString text;
    QString tooltip;
    bool muted = false;
};

struct ChipLayout {
    QList<QRect> rects;  // relative to the top left of the chip area
    int height = 0;
};

inline int chipGap(const QFontMetrics &fm) { return std::max(3, fm.height() / 4); }
inline int chipPadX(const QFontMetrics &fm) { return fm.height() / 2; }
inline int chipHeight(const QFontMetrics &fm) { return fm.height() + 2 * std::max(2, fm.height() / 6); }

// Flows the chips left to right and wraps to the next row at `width`; a chip never gets wider than `width`.
// padX < 0: the normal inset; a smaller one (a symbol-only chip in a list row) keeps the chips narrow.
// perRow > 0: a fixed number of chips per line (the list rows show the pushes above the tilts), else the line
// breaks where the width ends. The text gets 2 px of slack: a symbol font may measure a little differently
// when drawn, and an elided symbol ("...") is worse than a chip that is 2 px wider.
inline ChipLayout layoutChips(const QFontMetrics &fm, const QList<Chip> &chips, int width, int padX = -1, int perRow = 0) {
    const int px = padX >= 0 ? padX : chipPadX(fm), gap = chipGap(fm), ch = chipHeight(fm);
    ChipLayout l;
    int x = 0, y = 0, n = 0;
    for (const Chip &c : chips) {
        const int cw = std::min(std::max(width, 1), fm.horizontalAdvance(c.text) + 2 * px + 2);
        if (x > 0 && (perRow > 0 ? n % perRow == 0 : x + cw > width)) {
            x = 0;
            y += ch + gap;
        }
        l.rects.append(QRect(x, y, cw, ch));
        x += cw + gap;
        ++n;
    }
    l.height = chips.isEmpty() ? 0 : y + ch;
    return l;
}

// Draws the chips laid out by layoutChips at `origin`. `textColor` for active chips, `mutedColor` for muted ones.
inline void paintChips(QPainter *p, const QFontMetrics &fm, const QList<Chip> &chips, const ChipLayout &layout, const QPoint &origin,
                       const QColor &textColor, const QColor &mutedColor, int padX = -1) {
    p->save();
    p->setRenderHint(QPainter::Antialiasing, true);
    const int px = padX >= 0 ? padX : chipPadX(fm);
    for (int i = 0; i < chips.size() && i < layout.rects.size(); ++i) {
        const bool muted = chips[i].muted;
        const QRect r = layout.rects[i].translated(origin);
        p->setPen(QPen(muted ? mutedColor : textColor, 1, muted ? Qt::DashLine : Qt::SolidLine));
        p->setBrush(Qt::NoBrush);
        p->drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), fm.height() / 3.0, fm.height() / 3.0);
        p->setPen(muted ? mutedColor : textColor);
        p->drawText(r.adjusted(px, 0, -px, 0), Qt::AlignLeft | Qt::AlignVCenter, fm.elidedText(chips[i].text, Qt::ElideRight, r.width() - 2 * px));
    }
    p->restore();
}

inline Chip chipOf(const spn::ui::MotionRow &r) {
    Chip c;
    c.text = qs8(spn::ui::motionChipText(r));
    c.tooltip = qs8(r.name + " (" + r.how + "): " + r.action);
    c.muted = r.muted();
    return c;
}

inline Chip chipOf(const spn::ui::ButtonChip &b) {
    Chip c;
    c.text = qs8(b.text);
    c.tooltip = qs8(b.tooltip);
    c.muted = b.muted;
    return c;
}

// A row of chips that wraps to the width it gets (height for width). Tooltip per chip; the accessible
// description lists all chips in words.
class ChipFlow : public QWidget {
public:
    explicit ChipFlow(QWidget *parent = nullptr) : QWidget(parent) {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    }
    void setChips(const QList<Chip> &chips, const QString &emptyText = QString()) {
        chips_ = chips;
        emptyText_ = emptyText;
        QStringList words;
        for (const Chip &c : chips) words << (c.tooltip.isEmpty() ? c.text : c.tooltip);
        setAccessibleDescription(words.isEmpty() ? emptyText : words.join(QStringLiteral("; ")));
        updateGeometry();
        update();
    }
    const QList<Chip> &chips() const { return chips_; }
    QStringList chipTexts() const {
        QStringList out;
        for (const Chip &c : chips_) out << c.text;
        return out;
    }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int w) const override {
        const QFontMetrics fm(font());
        if (chips_.isEmpty()) return emptyText_.isEmpty() ? 0 : fm.height();
        return layoutChips(fm, chips_, w).height;
    }
    QSize minimumSizeHint() const override {
        const QFontMetrics fm(font());
        int widest = fm.horizontalAdvance(QStringLiteral("WWWWWW"));
        for (const Chip &c : chips_) widest = std::max(widest, std::min(fm.horizontalAdvance(c.text) + 2 * chipPadX(fm), fm.horizontalAdvance(QStringLiteral("W")) * 12));
        // one row: the real height comes from heightForWidth; a minimum that is as tall as the narrowest
        // wrapping would make a card (or any parent) look too small at its real, wider size
        return QSize(widest, heightForWidth(1 << 20));
    }
    QSize sizeHint() const override {
        const QFontMetrics fm(font());
        int w = 0;
        for (const Chip &c : chips_) w += fm.horizontalAdvance(c.text) + 2 * chipPadX(fm) + chipGap(fm);
        w = std::max(w, minimumSizeHint().width());
        return QSize(w, heightForWidth(w));
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        const QFontMetrics fm(font());
        if (chips_.isEmpty()) {
            p.setPen(palette().color(QPalette::PlaceholderText));
            p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap, emptyText_);
            return;
        }
        paintChips(&p, fm, chips_, layoutChips(fm, chips_, width()), QPoint(0, 0), palette().color(QPalette::WindowText),
                   palette().color(QPalette::PlaceholderText));
    }
    bool event(QEvent *e) override {
        if (e->type() == QEvent::ToolTip) {
            const auto *he = static_cast<QHelpEvent *>(e);
            const QFontMetrics fm(font());
            const ChipLayout l = layoutChips(fm, chips_, width());
            for (int i = 0; i < l.rects.size(); ++i)
                if (l.rects[i].contains(he->pos())) {
                    QToolTip::showText(he->globalPos(), chips_[i].tooltip, this);
                    return true;
                }
            QToolTip::hideText();
            e->ignore();
            return true;
        }
        return QWidget::event(e);
    }

private:
    QList<Chip> chips_;
    QString emptyText_;
};

// The six motions as six small chips, always in the same order (the Overview cards).
class MotionStrip : public ChipFlow {
public:
    explicit MotionStrip(QWidget *parent = nullptr) : ChipFlow(parent) {}
    void setMotions(const std::vector<spn::ui::MotionRow> &rows) {
        QList<Chip> chips;
        for (const spn::ui::MotionRow &r : rows) chips.append(chipOf(r));
        setChips(chips);
        setAccessibleName(T("What each motion does"));
    }
};

// The buttons as numbered chips ("1 Space"); `emptyText` is shown when none is assigned.
class ButtonChips : public ChipFlow {
public:
    explicit ButtonChips(QWidget *parent = nullptr) : ChipFlow(parent) {}
    void setButtons(const std::vector<spn::ui::ButtonChip> &buttons, const QString &emptyText) {
        QList<Chip> chips;
        for (const spn::ui::ButtonChip &b : buttons) chips.append(chipOf(b));
        setChips(chips, emptyText);
        setAccessibleName(T("Buttons"));
    }
};

// The 3x2 panel of the six motions: three columns (vertical, horizontal, twist and lift/press), push above
// tilt. Every card shows the name, the symbol with the action in bold, how the cap moves and, when given,
// where the value comes from. A muted card (off, not used) keeps its words.
class MotionGrid : public QWidget {
public:
    // columns 3: vertical, horizontal, twist/lift side by side (wide panes); columns 2: push and tilt of a channel
    // side by side, three rows (narrow panes such as the profile tab, wider fonts of Windows and macOS)
    explicit MotionGrid(QWidget *parent = nullptr, int columns = 3) : QWidget(parent) {
        auto *grid = new QGridLayout(this);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(spn::ui::tokens::kSpaceXs);  // three cards side by side: tight gaps keep the page narrow
        for (int i = 0; i < 6; ++i) {
            Card c;
            c.frame = new QFrame(this);
            c.frame->setProperty("card", true);  // rounded inset card (theme.cpp)
            c.frame->setFrameShape(QFrame::StyledPanel);
            auto *cl = new QVBoxLayout(c.frame);
            cl->setContentsMargins(spn::ui::tokens::kSpaceMd, spn::ui::tokens::kSpaceSm, spn::ui::tokens::kSpaceMd,
                                   spn::ui::tokens::kSpaceSm);  // three cards in a row: a small inset keeps the panel narrow
            cl->setSpacing(spn::ui::tokens::kSpaceXs);
            c.name = label(c.frame, true);
            c.action = label(c.frame, false);
            QFont f = c.action->font();
            f.setBold(true);
            c.action->setFont(f);
            c.how = label(c.frame, true);
            c.source = label(c.frame, true);
            QFont s = c.source->font();
            s.setItalic(true);
            c.source->setFont(s);
            cl->addWidget(c.name);
            cl->addWidget(c.action);
            cl->addWidget(c.how);
            cl->addWidget(c.source);
            cl->addStretch(1);
            const int row = columns == 2 ? i / 2 : i % 2, col = columns == 2 ? i % 2 : i / 2;
            grid->addWidget(c.frame, row, col);
            grid->setColumnStretch(col, 1);
            cards_.push_back(c);
        }
        setAccessibleName(T("What each motion does"));
    }
    void setMotions(const std::vector<spn::ui::MotionRow> &rows) {
        for (size_t i = 0; i < cards_.size(); ++i) {
            Card &c = cards_[i];
            c.frame->setVisible(i < rows.size());
            if (i >= rows.size()) continue;
            const spn::ui::MotionRow &r = rows[i];
            c.name->setText(qs8(r.name));
            c.action->setText(qs8(spn::ui::motionMarker(r.id) + " " + spn::ui::motionGlyph(r.symbol)) + QStringLiteral("  ") + qs8(r.action));
            c.how->setText(qs8(r.how));
            c.source->setText(qs8(r.source));
            c.source->setVisible(!r.source.empty());
            // muted: off or not used; the words "Off" and "Not used" say it as well
            c.action->setForegroundRole(r.muted() ? QPalette::PlaceholderText : QPalette::WindowText);
            c.frame->setAccessibleName(qs8(r.name + ": " + r.action));
            c.frame->setAccessibleDescription(qs8(r.how + (r.source.empty() ? std::string() : ", " + r.source)));
            c.frame->setToolTip(qs8(r.how));
        }
    }
    // "Vertical push: Scroll", ... of the visible cards (accessible names; self-test)
    QStringList cardTexts() const {
        QStringList out;
        for (const Card &c : cards_)
            if (c.frame->isVisibleTo(this)) out << c.frame->accessibleName();
        return out;
    }

private:
    struct Card {
        QFrame *frame = nullptr;
        QLabel *name = nullptr, *action = nullptr, *how = nullptr, *source = nullptr;
    };
    static QLabel *label(QWidget *parent, bool muted) {
        auto *l = new QLabel(parent);
        l->setWordWrap(true);
        l->setTextFormat(Qt::PlainText);
        if (muted) l->setForegroundRole(QPalette::PlaceholderText);
        return l;
    }
    std::vector<Card> cards_;
};

}  // namespace motionview
