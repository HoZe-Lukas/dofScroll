// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Proportional first sizes for a list | editor splitter. Absolute pixel sizes are squeezed or stretched
// by the window; this sets the list side to a share of the width (but at least a minimum) on every
// resize until the user moves the handle (a first resize happens before the layout has its real width).
#pragma once
#include <QEvent>
#include <QList>
#include <QObject>
#include <QResizeEvent>
#include <QSplitter>

#include <algorithm>
#include <functional>

namespace spn::ui {

// The width of the list side for a splitter of the given width: listPercent of it, at least listMin, but never
// more than 35% of it: the editor side always keeps at least 65%, also in a small window (a larger minimum would
// squeeze the editor to a sliver there; the splitter's own minimum sizes still apply).
inline int listSideWidth(int total, int listPercent, int listMin) {
    return std::min(std::max(listMin, total * listPercent / 100), total * 35 / 100);
}

// The list side never takes more than what the editor can spare: with wide glyphs (Windows fonts are about 1.6 times
// wider than Linux ones) the editor needs more than 65% of a small window, and the list then gives up width down to
// its floor (the user's own handle position still wins). The need is the width the editor asks for (0 = no wish).
class SplitDefaults : public QObject {
public:
    SplitDefaults(QSplitter *split, int listPercent, int listMin) : QObject(split), split_(split), percent_(listPercent), min_(listMin) {
        split->installEventFilter(this);
        connect(split, &QSplitter::splitterMoved, this, [this] { moved_ = true; });  // the user's choice wins
    }

    void setRightNeed(std::function<int()> need, int listFloor) {
        need_ = std::move(need);
        floor_ = listFloor;
    }
    // Sets the sizes again (the editor's need changed, for example after its content was rebuilt with another font).
    void fit() {
        if (!moved_) apply(split_->width());
    }

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (obj == split_ && !moved_ && ev->type() == QEvent::Resize) apply(static_cast<QResizeEvent *>(ev)->size().width());
        return QObject::eventFilter(obj, ev);
    }

private:
    void apply(int total) {
        if (total <= 100 || split_->count() != 2) return;
        int list = listSideWidth(total, percent_, min_);
        if (need_) {
            const int need = need_();
            if (need > 0) list = std::min(list, std::max(floor_, total - need - split_->handleWidth()));
        }
        split_->setSizes({list, total - list});
    }

    QSplitter *split_;
    int percent_, min_;
    int floor_ = 0;
    std::function<int()> need_;
    bool moved_ = false;
};

}  // namespace spn::ui
