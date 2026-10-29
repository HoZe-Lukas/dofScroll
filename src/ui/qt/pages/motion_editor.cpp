// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/motion_editor.hpp"

#include <QAccessible>
#include <QApplication>
#include <QDrag>
#include <QMimeData>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

#include "info_popup.hpp"
#include "key_capture.hpp"
#include "side_picker.hpp"
#include "key_combo.hpp"
#include "key_engine_box.hpp"
#include "ui/model/action_preset_model.hpp"
#include "ui/model/axes_model.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/motion_edit_model.hpp"
#include "ui/model/motion_help_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui/model/ui_progress.hpp"
#include "ui_i18n.hpp"
#include "plain_text.hpp"

using namespace spn;
using namespace spn::ui;

// ---------------------------------------------------------------------------------------------------------
// The picture

namespace {
constexpr int kMinTarget = 24;  // px, the smallest item

int itemPad(const QFontMetrics &fm) { return std::max(4, fm.height() / 3); }
QFont glyphFont(const QFont &f) {
    QFont g = f;
    if (g.pointSizeF() > 0) g.setPointSizeF(g.pointSizeF() * 1.7);
    else g.setPixelSize(int(g.pixelSize() * 1.7));
    return g;
}
}  // namespace

MotionPicture::MotionPicture(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
    setAcceptDrops(true);
    setAccessibleName(T("Picture of the cap: its motions and the buttons"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
}

int MotionPicture::indexOf(const QString &id) const {
    for (int i = 0; i < items_.size(); ++i)
        if (items_[i].id == id) return i;
    return -1;
}

void MotionPicture::setItems(const QList<Item> &items, LayoutKind kind) {
    items_ = items;
    kind_ = kind;
    if (indexOf(current_) < 0) current_ = items_.isEmpty() ? QString() : items_.first().id;
    layoutItems();
    updateAccessible();
    updateGeometry();
    update();
}

void MotionPicture::setCurrent(const QString &id) {
    if (indexOf(id) < 0 || id == current_) return;
    current_ = id;
    updateAccessible();
    update();
    emit selected(id);
}

QRect MotionPicture::itemRect(const QString &id) const {
    const int i = indexOf(id);
    return i < 0 ? QRect() : items_[i].rect;
}

int MotionPicture::minimumWidth() const {
    const QFontMetrics fm(font());
    QFont boldFont = font();
    boldFont.setBold(true);
    const QFontMetrics bold(boldFont);
    const int pad = itemPad(fm), gap = std::max(6, fm.height() / 2);
    bool anyMotion = false;
    for (const Item &it : items_) anyMotion = anyMotion || it.id.startsWith(QLatin1String("m:"));
    if (!anyMotion) {  // only buttons (the Button modes page): two columns of buttons are enough
        int bw = kMinTarget;
        for (const Item &it : items_) bw = std::max(bw, std::min(bold.horizontalAdvance(it.text) + 2 * pad, 9 * fm.height()));
        return 2 * bw + gap;
    }
    int cw = 0;
    for (const Item &it : items_)
        if (it.id.startsWith(QLatin1String("m:")))
            cw = std::max({cw, fm.horizontalAdvance(it.title) + 2 * pad, std::min(bold.horizontalAdvance(it.text) + 2 * pad + fm.height() / 2, 12 * fm.height())});
    cw = std::max(cw, 8 * fm.height());
    return 2 * cw + 2 * gap + 5 * fm.height();
}

MotionPicture::Plan MotionPicture::plan(int width) const {
    const QFontMetrics fm(font());
    QFont boldFont = font();
    boldFont.setBold(true);
    const QFontMetrics bold(boldFont);
    const int em = fm.height(), pad = itemPad(fm), gap = std::max(6, em / 2);
    Plan p;
    p.rects.resize(items_.size());
    // Six motion chips in two columns around the cap.
    int cw = 8 * em;
    for (const Item &it : items_)
        if (it.id.startsWith(QLatin1String("m:")))
            cw = std::max({cw, fm.horizontalAdvance(it.title) + 2 * pad, std::min(bold.horizontalAdvance(it.text) + 2 * pad + em / 2, 12 * em)});
    const int ch = std::max(kMinTarget, em + QFontMetrics(glyphFont(font())).height() + em + 2 * pad);
    static const char *const left[3] = {"m:v_push", "m:h_push", "m:twist"};
    static const char *const right[3] = {"m:v_tilt", "m:h_tilt", "m:lift"};
    const int w = std::max(width, minimumWidth());
    for (int r = 0; r < 3; ++r) {
        const int li = indexOf(QLatin1String(left[r])), ri = indexOf(QLatin1String(right[r]));
        if (li >= 0) p.rects[li] = QRect(0, r * (ch + gap), cw, ch);
        if (ri >= 0) p.rects[ri] = QRect(w - cw, r * (ch + gap), cw, ch);
    }
    bool anyMotion = false;
    for (const Item &it : items_) anyMotion = anyMotion || it.id.startsWith(QLatin1String("m:"));
    if (anyMotion) p.cap = QRect(cw + gap, 0, w - 2 * cw - 2 * gap, 3 * ch + 2 * gap);
    int y = anyMotion ? 3 * ch + 2 * gap + 2 * gap : 0;
    // The buttons below, with a heading line.
    QList<int> btn;
    for (int i = 0; i < items_.size(); ++i)
        if (items_[i].id.startsWith(QLatin1String("b:"))) btn << i;
    p.buttonsTop = y;
    if (!btn.isEmpty()) {
        y += em + gap;  // heading
        int bw = kMinTarget;
        for (int i : btn) bw = std::max(bw, std::min(bold.horizontalAdvance(items_[i].text) + 2 * pad, 9 * em));
        bw = std::max(bw, fm.horizontalAdvance(QStringLiteral("00")) + 2 * pad);
        const int bh = std::max(kMinTarget, 2 * em + 2 * pad);
        auto flow = [&](const QList<int> &which, int x0, int avail, int *yOut) {
            const int cols = std::max(1, (avail + gap) / (bw + gap));
            for (int k = 0; k < which.size(); ++k)
                p.rects[which[k]] = QRect(x0 + (k % cols) * (bw + gap), y + (k / cols) * (bh + gap), bw, bh);
            *yOut = y + ((which.size() + cols - 1) / cols) * (bh + gap);
        };
        const bool sides = kind_ == LayoutKind::TwoSides || kind_ == LayoutKind::Pro;
        QList<int> l, r;
        std::set<double> lx, rx, ys;
        if (sides)
            for (int i : btn) {
                (items_[i].ux < 0.5 ? l : r) << i;
                (items_[i].ux < 0.5 ? lx : rx).insert(items_[i].ux);
                ys.insert(items_[i].uy);
            }
        const int need = int(lx.size() + rx.size()) * (bw + gap) + 2 * gap;
        if (sides && need <= w) {
            auto rank = [](const std::set<double> &s, double v) { return int(std::distance(s.begin(), s.find(v))); };
            for (int i : l) {
                const Item &it = items_[i];
                p.rects[i] = QRect(rank(lx, it.ux) * (bw + gap), y + rank(ys, it.uy) * (bh + gap), bw, bh);
            }
            for (int i : r) {
                const Item &it = items_[i];
                p.rects[i] = QRect(w - int(rx.size() - rank(rx, it.ux)) * (bw + gap) + gap, y + rank(ys, it.uy) * (bh + gap), bw, bh);
            }
            y += int(ys.size()) * (bh + gap);
        } else {
            const int cols = std::min(int(btn.size()), std::max(1, (w + gap) / (bw + gap)));
            const int rowW = cols * (bw + gap) - gap;
            flow(btn, (w - rowW) / 2, w, &y);
        }
    }
    p.height = anyMotion ? std::max(y - (btn.isEmpty() ? 2 * gap : 0), 3 * ch + 2 * gap) : std::max(0, y - gap);
    return p;
}

void MotionPicture::layoutItems() {
    const Plan p = plan(width());
    for (int i = 0; i < items_.size(); ++i) items_[i].rect = p.rects[i];
    capRect_ = p.cap;
    buttonsTop_ = p.buttonsTop;
}

int MotionPicture::heightForWidth(int w) const { return plan(w).height; }

QSize MotionPicture::sizeHint() const {
    const int w = std::max(minimumWidth(), 40 * fontMetrics().height());
    return QSize(w, plan(w).height);
}

QSize MotionPicture::minimumSizeHint() const {
    const int w = minimumWidth();
    return QSize(w, plan(w).height);
}

void MotionPicture::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    layoutItems();
    // the layouts around do not ask for a height that depends on the width: say how tall the picture needs to be
    const int h = plan(width()).height;
    if (minimumHeight() != h) setMinimumHeight(h);
}

void MotionPicture::changeEvent(QEvent *e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::FontChange || e->type() == QEvent::StyleChange) {
        layoutItems();
        updateGeometry();
        update();
    }
}

void MotionPicture::updateAccessible() {
    QStringList parts;
    for (const Item &it : items_) parts << it.title + QStringLiteral(": ") + it.text;
    QString d = parts.join(QStringLiteral("; "));
    const int i = indexOf(current_);
    if (i >= 0) d = T("Selected: %1").arg(items_[i].title + QStringLiteral(": ") + items_[i].text) + QStringLiteral(". ") + d;
    setAccessibleDescription(d);
}

void MotionPicture::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QFontMetrics fm(font());
    const int pad = itemPad(fm), em = fm.height();
    const QColor text = palette().color(QPalette::WindowText), mutedText = palette().color(QPalette::PlaceholderText);
    const QColor line = palette().color(QPalette::Mid), base = palette().color(QPalette::Base), hi = palette().color(QPalette::Highlight);

    // The schematic cap: a disc with a smaller top, and a thin line from every motion to it.
    const bool drawCap = !capRect_.isNull();
    const QPoint c = capRect_.center();
    const int radius = std::max(2 * em, std::min(capRect_.width(), capRect_.height()) / 2 - em / 2);
    p.setPen(QPen(line, 1));
    for (const Item &it : items_) {
        if (!drawCap || !it.id.startsWith(QLatin1String("m:"))) continue;
        const bool leftSide = it.rect.center().x() < c.x();
        const QPoint from(leftSide ? it.rect.right() : it.rect.left(), it.rect.center().y());
        p.drawLine(from, QPoint(c.x() + (leftSide ? -radius / 2 : radius / 2), c.y() + (it.rect.center().y() - c.y()) / 3));
    }
    if (drawCap) {
        p.setBrush(base);
        p.setPen(QPen(line, 2));
        p.drawEllipse(c, radius, radius);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, radius * 3 / 5, radius * 3 / 5);
        p.setPen(mutedText);
        p.drawText(QRect(c.x() - radius, c.y() - em / 2, 2 * radius, em), Qt::AlignCenter, T("Cap"));
    }

    if (buttonsTop_ > 0) {
        bool any = false;
        for (const Item &it : items_) any = any || it.id.startsWith(QLatin1String("b:"));
        if (any) {
            p.setPen(text);
            QFont f = font();
            f.setBold(true);
            p.setFont(f);
            p.drawText(QRect(0, buttonsTop_, width(), em), Qt::AlignLeft | Qt::AlignVCenter, T("Buttons"));
            p.setFont(font());
        }
    }

    for (const Item &it : items_) {
        const bool cur = it.id == current_;
        QPen pen(cur ? hi : line, cur ? 3 : 1);
        if (it.muted && !cur) pen.setStyle(Qt::DashLine);  // a muted item is dashed and says so in words
        p.setPen(pen);
        QColor fill = base;
        if (cur) {
            fill = hi;
            fill.setAlphaF(0.14);  // the selected key is tinted as well as outlined
            p.setBrush(base);
            p.drawRoundedRect(QRectF(it.rect).adjusted(1.5, 1.5, -1.5, -1.5), em / 3.0, em / 3.0);
        }
        p.setBrush(fill);
        p.drawRoundedRect(QRectF(it.rect).adjusted(1.5, 1.5, -1.5, -1.5), em / 3.0, em / 3.0);
        if (it.id == dropTarget_) {  // the item a drop would land on: a thick dashed frame
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(hi, 3, Qt::DashLine));
            p.drawRoundedRect(QRectF(it.rect).adjusted(-2, -2, 2, 2), em / 3.0 + 2, em / 3.0 + 2);
        }
        if (cur && hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(palette().color(QPalette::WindowText), 2, Qt::DotLine));
            p.drawRoundedRect(QRectF(it.rect).adjusted(-3, -3, 3, 3), em / 3.0 + 2, em / 3.0 + 2);
        }
        const QRect inner = it.rect.adjusted(pad, pad / 2, -pad, -pad / 2);
        QFont bold = font();
        bold.setBold(true);
        if (it.id.startsWith(QLatin1String("m:"))) {
            // a key of the cap: what it is (quiet), a big symbol, what it does (bold)
            p.setFont(font());
            p.setPen(mutedText);
            p.drawText(QRect(inner.left(), inner.top(), inner.width(), em), Qt::AlignHCenter | Qt::AlignTop, fm.elidedText(it.title, Qt::ElideRight, inner.width()));
            const QFont gf = glyphFont(font());
            p.setFont(gf);
            p.setPen(it.muted ? mutedText : text);
            p.drawText(QRect(inner.left(), inner.top() + em, inner.width(), QFontMetrics(gf).height()), Qt::AlignCenter, it.glyph);
            p.setFont(bold);
            p.drawText(QRect(inner.left(), inner.bottom() - em + 1, inner.width(), em), Qt::AlignHCenter | Qt::AlignBottom,
                       QFontMetrics(bold).elidedText(it.text, Qt::ElideRight, inner.width()));
        } else {
            // a button: its number (big, bold) and what it does
            p.setFont(bold);
            p.setPen(it.muted ? mutedText : text);
            p.drawText(inner.adjusted(0, 0, 0, -em), Qt::AlignHCenter | Qt::AlignTop, QFontMetrics(bold).elidedText(it.title, Qt::ElideRight, inner.width()));
            p.setFont(font());
            p.drawText(QRect(inner.left(), inner.bottom() - em + 1, inner.width(), em), Qt::AlignHCenter | Qt::AlignBottom,
                       fm.elidedText(it.text, Qt::ElideRight, inner.width()));
        }
        p.setFont(font());
    }
}

bool MotionPicture::event(QEvent *e) {
    if (e->type() == QEvent::ToolTip) {
        const QPoint pos = static_cast<QHelpEvent *>(e)->pos();
        for (const Item &it : items_)
            if (it.rect.contains(pos)) {
                QToolTip::showText(static_cast<QHelpEvent *>(e)->globalPos(), it.tooltip, this, it.rect);
                return true;
            }
        QToolTip::hideText();
    }
    return QWidget::event(e);
}

void MotionPicture::mousePressEvent(QMouseEvent *e) {
    setFocus(Qt::MouseFocusReason);
    for (const Item &it : items_)
        if (it.rect.contains(e->pos())) {
            setCurrent(it.id);
            pressPos_ = e->pos();
            pressId_ = canDrag(it.id) ? it.id : QString();
            return;
        }
    pressId_.clear();
    QWidget::mousePressEvent(e);
}

void MotionPicture::mouseReleaseEvent(QMouseEvent *e) {
    pressId_.clear();
    QWidget::mouseReleaseEvent(e);
}

// The item of a motion under `pos` that can take a drop from `from` (never the dragged one itself).
QString MotionPicture::dropItemAt(const QPoint &pos, const QString &from) const {
    for (const Item &it : items_)
        if (it.rect.contains(pos)) return canDrag(it.id) && it.id != from ? it.id : QString();
    return QString();
}

void MotionPicture::mouseMoveEvent(QMouseEvent *e) {
    if (pressId_.isEmpty() || !(e->buttons() & Qt::LeftButton) || (e->pos() - pressPos_).manhattanLength() < QApplication::startDragDistance()) {
        QWidget::mouseMoveEvent(e);
        return;
    }
    const QString from = pressId_;
    pressId_.clear();
    auto *mime = new QMimeData;
    mime->setData(QStringLiteral("application/x-dofscroll-motion"), from.toUtf8());
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    const QRect r = itemRect(from);
    QPixmap pm(r.size() * devicePixelRatioF());
    pm.setDevicePixelRatio(devicePixelRatioF());
    pm.fill(Qt::transparent);
    render(&pm, QPoint(), QRegion(r));
    drag->setPixmap(pm);
    drag->setHotSpot(pressPos_ - r.topLeft());
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
    dropTarget_.clear();
    update();
}

static bool motionDrag(const QMimeData *m) { return m && m->hasFormat(QStringLiteral("application/x-dofscroll-motion")); }

void MotionPicture::dragEnterEvent(QDragEnterEvent *e) {
    if (motionDrag(e->mimeData())) e->acceptProposedAction();
}

void MotionPicture::dragMoveEvent(QDragMoveEvent *e) {
    if (!motionDrag(e->mimeData())) return;
    const QString from = QString::fromUtf8(e->mimeData()->data(QStringLiteral("application/x-dofscroll-motion")));
    const QString target = dropItemAt(e->position().toPoint(), from);
    if (target != dropTarget_) {
        dropTarget_ = target;
        update();
    }
    if (target.isEmpty()) {
        e->ignore();
        return;
    }
    // Ctrl (Cmd on macOS) copies, otherwise the two swap; Qt maps the modifier to the proposed action
    e->setDropAction(e->modifiers() & Qt::ControlModifier ? Qt::CopyAction : Qt::MoveAction);
    e->accept();
}

void MotionPicture::dragLeaveEvent(QDragLeaveEvent *e) {
    dropTarget_.clear();
    update();
    QWidget::dragLeaveEvent(e);
}

void MotionPicture::dropEvent(QDropEvent *e) {
    dropTarget_.clear();
    update();
    if (!motionDrag(e->mimeData())) return;
    const QString from = QString::fromUtf8(e->mimeData()->data(QStringLiteral("application/x-dofscroll-motion")));
    const QString to = dropItemAt(e->position().toPoint(), from);
    if (to.isEmpty()) return;
    e->setDropAction(e->dropAction() == Qt::CopyAction ? Qt::CopyAction : Qt::MoveAction);
    e->accept();
    dropOn(from, to, e->dropAction() == Qt::CopyAction);
}

void MotionPicture::dropOn(const QString &from, const QString &to, bool copy) {
    if (!canDrag(from) || !canDrag(to) || from == to) return;
    emit dropped(from, to, copy);
}

void MotionPicture::focusInEvent(QFocusEvent *e) {
    QWidget::focusInEvent(e);
    update();
}
void MotionPicture::focusOutEvent(QFocusEvent *e) {
    QWidget::focusOutEvent(e);
    update();
}

// The nearest item in a direction (dx, dy in {-1, 0, 1}), or -1.
int MotionPicture::neighbour(int from, int dx, int dy) const {
    if (from < 0 || from >= items_.size()) return -1;
    const QRect fr = items_[from].rect;
    const QPoint c = fr.center();
    // First the nearest item in the same row (left, right) or column (up, down); only if there is none, the
    // nearest in that direction with a penalty for being off to the side.
    for (int pass = 0; pass < 2; ++pass) {
        int best = -1;
        double bestScore = 0;
        for (int i = 0; i < items_.size(); ++i) {
            if (i == from) continue;
            const QRect r = items_[i].rect;
            const QPoint d = r.center() - c;
            const int along = d.x() * dx + d.y() * dy;
            if (along <= 0) continue;
            const bool sameLine = dx ? (r.top() <= fr.bottom() && r.bottom() >= fr.top()) : (r.left() <= fr.right() && r.right() >= fr.left());
            if (pass == 0 && !sameLine) continue;
            const int across = std::abs(dx ? d.y() : d.x());
            const double score = pass == 0 ? along : along + 2.0 * across;
            if (best < 0 || score < bestScore) {
                best = i;
                bestScore = score;
            }
        }
        if (best >= 0) return best;
    }
    return -1;
}

void MotionPicture::keyPressEvent(QKeyEvent *e) {
    int dx = 0, dy = 0;
    switch (e->key()) {
        case Qt::Key_Left: dx = -1; break;
        case Qt::Key_Right: dx = 1; break;
        case Qt::Key_Up: dy = -1; break;
        case Qt::Key_Down: dy = 1; break;
        case Qt::Key_Return:
        case Qt::Key_Enter:
        case Qt::Key_Space:
            if (!current_.isEmpty()) emit activated(current_);
            return;
        default: QWidget::keyPressEvent(e); return;
    }
    const int n = neighbour(indexOf(current_), dx, dy);
    if (n >= 0) setCurrent(items_[n].id);
}

// ---------------------------------------------------------------------------------------------------------
// The editor: device choice, picture, live sentence, inspector

namespace {

struct DeviceChoice {
    QString label;
    std::string name;
    int reported;
};

struct KindCard {
    ChannelActionType type;
    const char *label;
    const char *tip;
};
const KindCard kCards[] = {
    {ChannelActionType::Default, "Default behavior", "As in the profile"},
    {ChannelActionType::Off, "Do nothing", "This motion has no effect in this joystick mode"},
    {ChannelActionType::WheelV, "Scroll up and down", "Vertical mouse wheel"},
    {ChannelActionType::WheelH, "Scroll sideways", "Horizontal mouse wheel"},
    {ChannelActionType::Zoom, "Zoom", "Ctrl + wheel"},
    {ChannelActionType::PointerX, "Move the pointer left and right", "The mouse pointer follows the motion; the speed is MOUSE_SPEED"},
    {ChannelActionType::PointerY, "Move the pointer up and down", "The mouse pointer follows the motion; the speed is MOUSE_SPEED"},
    {ChannelActionType::Keys, "Keys: a rate of key presses", "Press a key for each direction; the harder you push, the faster it repeats"},
    {ChannelActionType::Step, "One key tap per push", "One key press for each push or tilt"},
};

// SCROLL_V_, SCROLL_V_PUSH_, ZOOM_ ... : the prefix of the tuning keys of a motion.
std::string tuningPrefix(const ModeForm &form, const Settings &base, Motion m) {
    const MotionPlace p = placeOf(form, base, m);
    if (!p.found) return std::string();
    if (p.channel == MotionChannel::Twist) return "ZOOM_";
    const std::string ch = p.channel == MotionChannel::V ? "SCROLL_V_" : "SCROLL_H_";
    return p.split && p.part ? ch + (isTilt(m) ? "TILT_" : "PUSH_") : ch;
}

}  // namespace

QString MotionEditor::T(const char *s) const { return ::T(s); }

MotionEditor::MotionEditor(const MotionEditorEnv &env, const QString &selected, QWidget *parent) : QWidget(parent), env_(env) {
    auto *root = new QBoxLayout(QBoxLayout::LeftToRight, this);
    root_ = root;
    root->setContentsMargins(0, 0, 0, 0);
    // Left: device choice, tools, tour, picture, live sentence, hints.
    leftBox_ = new QWidget(this);
    auto *left = new QVBoxLayout(leftBox_);
    left->setContentsMargins(0, 0, 0, 0);
    auto *devRow = new QHBoxLayout;
    auto *devLabel = new QLabel(T("Device:"), this);
    deviceCombo_ = new QComboBox(this);
    deviceCombo_->setAccessibleName(T("Device whose buttons are shown"));
    deviceCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    deviceCombo_->setMinimumContentsLength(6);
    devLabel->setBuddy(deviceCombo_);
    devRow->addWidget(devLabel);
    devRow->addWidget(deviceCombo_, 1);
    devRow->addWidget(helpButton(this, QStringLiteral("picture")));
    left->addLayout(devRow);
    auto *toolRow = new QHBoxLayout;
    buildTop(left, toolRow);
    left->addLayout(toolRow);
    picture_ = new MotionPicture(this);
    left->addWidget(picture_);
    sentence_ = new QLabel(this);
    sentence_->setWordWrap(true);
    sentence_->setTextFormat(Qt::PlainText);
    sentence_->setAccessibleName(T("What the selected item does"));
    left->addWidget(sentence_);
    hintBox_ = new QWidget(this);
    new QVBoxLayout(hintBox_);
    hintBox_->layout()->setContentsMargins(0, 0, 0, 0);
    left->addWidget(hintBox_);
    left->addStretch(1);
    root->addWidget(leftBox_, 3);
    // Right: the inspector.
    inspectorBox_ = new QGroupBox(T("Selected"), this);
    inspectorCol_ = new QVBoxLayout(inspectorBox_);
    root->addWidget(inspectorBox_, 2);

    // The device choices: the attached one first, the known models, and a generic device.
    deviceCombo_->addItem(env_.deviceName.empty() ? T("No device attached: generic") : qs8(env_.deviceName), 0);
    const struct { const char *label; const char *name; } known[] = {
        {"SpaceMouse Compact (2 buttons)", "SpaceMouse Compact"},
        {"SpaceMouse Pro (15 buttons)", "SpaceMouse Pro"},
        {"SpaceMouse Enterprise (31 buttons)", "SpaceMouse Enterprise"}};
    for (const auto &k : known) deviceCombo_->addItem(T(k.label), qs8(k.name));
    deviceCombo_->setCurrentIndex(0);
    connect(deviceCombo_, &QComboBox::activated, this, [this](int i) {
        deviceChoice_ = i;
        rebuildPicture();
        refreshHints();
    });
    connect(picture_, &MotionPicture::selected, this, [this](const QString &id) {
        rebuildInspector();
        if (!fromDetect_) tourEvent(id.startsWith(QLatin1String("b:")) ? TourEvent::ButtonClicked : TourEvent::MotionClicked);
        emit selectionChanged(id);
    });
    connect(picture_, &MotionPicture::dropped, this, [this](const QString &from, const QString &to, bool copy) {
        Motion a, b;
        if (!motionFromId(from.mid(2).toStdString(), &a) || !motionFromId(to.mid(2).toStdString(), &b)) return;
        if (dropMotion(*env_.form, env_.base, a, b, copy).ok && env_.changed) env_.changed();
    });
    connect(picture_, &MotionPicture::activated, this, [this] {
        for (QWidget *w : inspectorBox_->findChildren<QWidget *>())
            if (w->isVisibleTo(inspectorBox_) && w->focusPolicy() != Qt::NoFocus && w->isEnabled()) {
                w->setFocus(Qt::TabFocusReason);
                return;
            }
    });
    rebuildPicture();
    if (!selected.isEmpty()) picture_->setCurrent(selected);
    rebuildInspector();
    refreshHints();
    tryingFrom_ = env_.tryingFrom;
    refreshTop();
    refreshTour();
    if (env_.detect) setDetect(true);
}

bool MotionEditor::select(const QString &id) {
    for (const MotionPicture::Item &it : picture_->items())
        if (it.id == id) {
            picture_->setCurrent(id);
            return true;
        }
    return false;
}

// Side by side when both parts fit; else the inspector goes below the picture (no horizontal scroll bar).
void MotionEditor::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    const int needed = leftBox_->minimumSizeHint().width() + inspectorBox_->minimumSizeHint().width() + root_->spacing();
    const QBoxLayout::Direction dir = width() >= needed ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;
    if (root_->direction() != dir) root_->setDirection(dir);
}

QString MotionEditor::inspectorTitle() const { return inspectorTitle_ ? inspectorTitle_->text() : QString(); }

void MotionEditor::rebuildPicture() {
    const StringProvider &s = *env_.strings;
    QList<MotionPicture::Item> items;
    for (const MotionLabel &l : motionLabels(*env_.form, env_.base, s)) {
        MotionPicture::Item it;
        it.id = QStringLiteral("m:") + QString::fromLatin1(motionId(l.motion));
        it.title = qs8(l.name);
        it.glyph = qs8(motionGlyph(l.symbol));
        it.text = qs8(l.tile) + (l.reversed ? QStringLiteral(", ") + T("reversed") : QString());
        it.muted = l.kind != MotionKind::Active;
        it.tooltip = qs8(l.name) + QStringLiteral(" (") + qs8(l.how) + QStringLiteral("): ") + it.text +
                     (l.badge.empty() ? QString() : QStringLiteral(". ") + qs8(l.badge));
        if (l.motion != Motion::Lift) it.tooltip += QStringLiteral("\n") + T("Drag onto another motion to swap them, with Ctrl to copy.");
        items << it;
    }
    // Buttons: the device's layout, plus bound buttons the layout does not have.
    std::string name = env_.deviceName;
    int reported = env_.reportedButtons;
    if (deviceChoice_ > 0) {
        name = deviceCombo_->itemData(deviceChoice_).toString().toStdString();
        reported = 0;
    }
    DeviceLayout layout = deviceLayout(name, reported);
    int highest = 0;
    for (const auto &b : env_.form->buttons) highest = std::max(highest, b.first);
    std::set<int> have;
    for (const ButtonSlot &slot : layout.buttons) have.insert(slot.number);
    auto addButton = [&](int n, double ux, double uy) {
        const std::string *act = env_.form->button(n);
        const ButtonChip chip = buttonChip(n, act ? *act : std::string(), std::string(), s);
        MotionPicture::Item it;
        it.id = QStringLiteral("b:") + QString::number(n);
        it.title = QString::number(n);
        it.text = act ? qs8(chip.action) : T("Inherited");
        it.muted = !act;
        it.ux = ux;
        it.uy = uy;
        it.tooltip = act ? qs8(chip.tooltip) : T("Button %1: not bound in this joystick mode, the profile's binding applies").arg(n);
        items << it;
    };
    for (const ButtonSlot &slot : layout.buttons) addButton(slot.number, slot.x, slot.y);
    for (int n = 1; n <= std::min(highest, 32); ++n)
        if (!have.count(n)) addButton(n, 0.5, 1.0);
    picture_->setItems(items, layout.kind);
}

void MotionEditor::rebuildInspector() {
    clearLayoutNow(inspectorCol_);
    inspectorTitle_ = nullptr;
    liveBar_ = nullptr;
    const QString id = picture_->current();
    if (id.startsWith(QLatin1String("m:"))) {
        Motion m;
        if (motionFromId(id.mid(2).toStdString(), &m)) {
            if (m == Motion::Lift) buildLiftInspector(inspectorCol_);
            else buildMotionInspector(inspectorCol_, m);
        }
    } else if (id.startsWith(QLatin1String("b:"))) {
        buildButtonInspector(inspectorCol_, id.mid(2).toInt());
    }
    inspectorCol_->addStretch(1);
    refreshSentence();
    refreshDetect();
}

void MotionEditor::buildMotionInspector(QVBoxLayout *col, Motion m) {
    const MotionLabel l = motionLabel(*env_.form, env_.base, m, *env_.strings);
    inspectorTitle_ = new QLabel(QStringLiteral("<b>") + qs8(l.name).toHtmlEscaped() + QStringLiteral("</b>"), inspectorBox_);
    inspectorTitle_->setTextFormat(Qt::RichText);
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(inspectorTitle_, 1);
    titleRow->addWidget(helpButton(inspectorBox_, QStringLiteral("motion:") + QString::fromLatin1(motionId(m))));
    col->addLayout(titleRow);
    auto *how = new QLabel(qs8(l.how), inspectorBox_);
    col->addWidget(how);
    liveBar_ = new QProgressBar(inspectorBox_);
    liveBar_->setRange(0, int(kFullDeflection));
    liveBar_->setFormat(T("Live: %v"));
    liveBar_->setAccessibleName(T("Live value"));
    liveBar_->setVisible(detectOn_);
    col->addWidget(liveBar_);

    const MotionPlace place = placeOf(*env_.form, env_.base, m);
    if (!l.badge.empty()) {
        auto *badge = new QLabel(mark(StatusKind::Warning, qs8(l.badge) + QStringLiteral(": ") +
                                                              T("the channel mix of this motion is written by hand, so it is left as it is.")),
                                 inspectorBox_);
        badge->setWordWrap(true);
        badge->setProperty("warning", true);
        col->addWidget(badge);
        auto *show = new QPushButton(T("Show in Settings tab"), inspectorBox_);
        connect(show, &QPushButton::clicked, this, [this] {
            if (env_.showTab) env_.showTab(QStringLiteral("overrides"));
        });
        col->addWidget(show);
    }
    if (!place.found) return;  // nothing can be assigned to a motion that no channel uses

    // Action tiles: big, one glance each (action_preset_model.hpp); the lit one is what the motion does now.
    const ChannelForm cur = actionOf(*env_.form, env_.base, m);
    {
        auto *actions = new QGroupBox(T("What it does"), inspectorBox_);
        auto *grid = new QGridLayout(actions);
        const std::string active = activePreset(m, cur);
        int i = 0;
        for (const ActionPreset &p : actionPresets(*env_.strings)) {
            auto *tile = new QToolButton(actions);
            tile->setText(qs8(p.glyph) + QStringLiteral("\n") + qs8(p.label));
            tile->setToolTip(qs8(p.tip));
            tile->setAccessibleName(qs8(p.label));
            tile->setCheckable(true);
            tile->setProperty("actionTile", true);
            tile->setChecked(p.id == active);
            tile->setAutoRaise(false);
            tile->setMinimumHeight(3 * tile->fontMetrics().height() + 16);
            tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            grid->addWidget(tile, i / 2, i % 2);
            ++i;
            const std::string id = p.id;
            connect(tile, &QToolButton::clicked, this, [this, m, id] {
                const ChannelForm now = actionOf(*env_.form, env_.base, m);
                if (!assign(*env_.form, env_.base, m, presetForm(id, m, now)).ok) return;
                tourEvent(TourEvent::ActionAssigned);
                if (env_.changed) env_.changed();
            });
        }
        col->addWidget(actions);
    }

    if (cur.usesKeys())
        if (ChannelForm *ref = actionRef(*env_.form, env_.base, m)) {
            auto *keys = new QGroupBox(T("Keys"), inspectorBox_);
            auto *kg = new QGridLayout(keys);
            auto committed = [this] {
                if (env_.committed) env_.committed();
            };
            // per direction: the picker (modifiers and a key list) and, below it, "Press a key..." which takes the key you press
            auto *note = new QLabel(keys);
            note->setWordWrap(true);
            note->setTextFormat(Qt::PlainText);
            note->setProperty("warning", true);
            note->hide();
            auto refused = [note](const QString &text) {
                note->setText(mark(StatusKind::Warning, text));
                note->show();
            };
            struct Dir {
                const char *label;
                std::string ChannelForm::*chord;
                const char *accessible;
            };
            const Dir dirs[2] = {{"Negative:", &ChannelForm::neg, "negative"}, {"Positive:", &ChannelForm::pos, "positive"}};
            int row = 0;
            // step: and hold: take a mouse click per side as well (side_picker.hpp); the analog keys: do not
            const bool mouseSides = cur.type != ChannelActionType::Keys;
            for (const Dir &d : dirs) {
                auto *dirLabel = new QLabel(T(d.label), keys);
                kg->addWidget(dirLabel, row, 0, Qt::AlignRight);
                const QString accessible = qs8(l.name) + QStringLiteral(" ") + T(d.accessible);
                QWidget *picker = nullptr;
                KeyTreePicker *kc = nullptr;
                picker = sidePicker(keys, this, &(ref->*d.chord), accessible, committed, &kc, mouseSides);
                dirLabel->setBuddy(kc);
                kg->addWidget(picker, row, 1);
                auto *capture = new KeyCaptureButton(qs8(l.name) + QStringLiteral(" ") + T(d.accessible) + QStringLiteral(": ") + T("Press a key..."),
                                                     [this, ref, d](const std::string &chord) {
                                                         ref->*d.chord = chord;
                                                         if (env_.changed) env_.changed();
                                                     },
                                                     refused, keys);
                kg->addWidget(capture, row + 1, 1, Qt::AlignLeft);
                row += 2;
            }
            kg->addWidget(note, row, 0, 1, 2);
            kg->setColumnStretch(1, 1);
            if (QWidget *engine = keyEngineBox(keys, this, keyEngineEnv(ref, m), *env_.strings)) kg->addWidget(engine, row + 1, 0, 1, 2);
            col->addWidget(keys);
        }

    // Direction
    auto *rev = new QCheckBox(T("Reverse direction"), inspectorBox_);
    rev->setChecked(l.reversed);
    rev->setAccessibleName(qs8(l.name) + QStringLiteral(": ") + T("Reverse direction"));
    rev->setToolTip(T("Reverses scroll, zoom and keys alike (written as a sign of -1)"));
    connect(rev, &QCheckBox::clicked, this, [this, m](bool on) {
        if (!setReversed(*env_.form, env_.base, m, on).ok) return;
        if (env_.changed) env_.changed();
    });
    col->addWidget(rev);

    // More options, closed until wanted: copy or swap with another motion, tuning.
    bool anyTuning = false;
    const std::string prefix = tuningPrefix(*env_.form, env_.base, m);
    for (const char *f : {"SENS", "DEADZONE", "CURVE", "SMOOTH_MS"}) anyTuning = anyTuning || (!prefix.empty() && env_.form->overrideValue(prefix + f));
    auto *more = new QGroupBox(T("More options"), inspectorBox_);
    more->setCheckable(true);
    more->setChecked(anyTuning);
    auto *ml = new QVBoxLayout(more);
    auto *copyRow = new QHBoxLayout;
    auto *same = new QComboBox(more);
    auto *swap = new QComboBox(more);
    same->setAccessibleName(qs8(l.name) + QStringLiteral(": ") + T("Same as ..."));
    swap->setAccessibleName(qs8(l.name) + QStringLiteral(": ") + T("Swap with ..."));
    same->addItem(T("Same as ..."), QString());
    swap->addItem(T("Swap with ..."), QString());
    for (Motion o : allMotions()) {
        if (o == m || o == Motion::Lift || !placeOf(*env_.form, env_.base, o).found) continue;
        const QString name = qs8(motionLabel(*env_.form, env_.base, o, *env_.strings).name);
        same->addItem(name, QString::fromLatin1(motionId(o)));
        swap->addItem(name, QString::fromLatin1(motionId(o)));
    }
    copyRow->addWidget(same, 1);
    copyRow->addWidget(swap, 1);
    ml->addLayout(copyRow);
    if (env_.takeOverMotion) {
        auto *take = new QPushButton(T("Take over from ..."), more);
        take->setObjectName(QStringLiteral("modeTakeOverMotion"));
        take->setToolTip(T("Copy what this motion does in a profile or another joystick mode into this joystick mode"));
        connect(take, &QPushButton::clicked, this, [this, m] { env_.takeOverMotion(m); });
        ml->addWidget(take, 0, Qt::AlignLeft);
    }
    connect(same, &QComboBox::activated, this, [this, m, same](int i) {
        Motion o;
        if (i <= 0 || !motionFromId(same->itemData(i).toString().toStdString(), &o)) return;
        if (dropMotion(*env_.form, env_.base, o, m, true).ok && env_.changed) env_.changed();
    });
    connect(swap, &QComboBox::activated, this, [this, m, swap](int i) {
        Motion o;
        if (i <= 0 || !motionFromId(swap->itemData(i).toString().toStdString(), &o)) return;
        if (dropMotion(*env_.form, env_.base, m, o, false).ok && env_.changed) env_.changed();
    });
    buildTuning(ml, m);
    for (QWidget *w : more->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) w->setVisible(anyTuning);
    connect(more, &QGroupBox::toggled, this, [more](bool on) {
        for (QWidget *w : more->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) w->setVisible(on);
    });
    col->addWidget(more);
}

// The environment of the analog / digital box (key_engine_box.hpp) for the key function `ref` of motion m: the rate and trigger
// keys are overrides of this joystick mode. A tilt of an unsplit channel runs as the push part, so it uses the push keys.
KeyEngineEnv MotionEditor::keyEngineEnv(ChannelForm *ref, Motion m) {
    KeyEngineEnv env;
    std::string id = motionId(m);
    if (isTilt(m) && !placeOf(*env_.form, env_.base, m).split) id = m == Motion::VTilt ? "v_push" : "h_push";
    env.motionId = id;
    env.motionName = qs8(motionLabel(*env_.form, env_.base, m, *env_.strings).name);
    env.form = ref;
    env.effective = effectiveSettings(*env_.form, env_.base);
    ModeForm *form = env_.form;
    env.get = [form](const std::string &key) {
        const std::string *v = form->overrideValue(key);
        return v ? *v : std::string();
    };
    env.set = [form](const std::string &key, const std::string &value, std::string *) {
        form->setOverride(key, value);
        return true;
    };
    env.commit = [this] {
        if (env_.changed) env_.changed();
    };
    env.typed = [this] {
        if (env_.typed) env_.typed();
    };
    env.flush = [this] {
        if (env_.flush) env_.flush();
    };
    return env;
}

void MotionEditor::buildTuning(QVBoxLayout *col, Motion m) {
    const std::string prefix = tuningPrefix(*env_.form, env_.base, m);
    if (prefix.empty()) return;
    static const char *const fields[][2] = {{"SENS", "Sensitivity"}, {"DEADZONE", "Dead zone"}, {"CURVE", "Curve"}, {"SMOOTH_MS", "Smoothing (ms)"}};
    bool anySet = false;
    for (const auto &f : fields) anySet = anySet || env_.form->overrideValue(prefix + f[0]);
    auto *box = new QGroupBox(T("Tune this motion"), inspectorBox_);
    box->setToolTip(T("Optional: values that apply while this joystick mode is active"));
    auto *fl = new QFormLayout(box);
    for (const auto &f : fields) {
        const std::string key = prefix + f[0];
        auto *e = new QLineEdit(box);
        const std::string *cur = env_.form->overrideValue(key);
        e->setText(cur ? qs8(*cur) : QString());
        e->setPlaceholderText(T("As the profile"));
        e->setAccessibleName(T(f[1]));
        e->setToolTip(qs8(key));
        fl->addRow(T(f[1]) + QLatin1Char(':'), e);
        connect(e, &QLineEdit::textEdited, this, [this, key](const QString &text) {
            env_.form->setOverride(key, text.trimmed().toUtf8().toStdString());
            if (env_.typed) env_.typed();
        });
        connect(e, &QLineEdit::editingFinished, this, [this] {
            if (env_.flush) env_.flush();
        });
    }
    (void)anySet;
    col->addWidget(box);
}

void MotionEditor::buildLiftInspector(QVBoxLayout *col) {
    const MotionLabel l = motionLabel(*env_.form, env_.base, Motion::Lift, *env_.strings);
    inspectorTitle_ = new QLabel(QStringLiteral("<b>") + qs8(l.name).toHtmlEscaped() + QStringLiteral("</b>"), inspectorBox_);
    inspectorTitle_->setTextFormat(Qt::RichText);
    auto *liftTitle = new QHBoxLayout;
    liftTitle->addWidget(inspectorTitle_, 1);
    liftTitle->addWidget(helpButton(inspectorBox_, QStringLiteral("motion:lift")));
    col->addLayout(liftTitle);
    col->addWidget(new QLabel(qs8(l.how), inspectorBox_));
    auto *warn = new QLabel(mark(StatusKind::Warning, T("Lift / press is prone to false triggers when the cap is grabbed or released. "
                                                       "No built-in joystick mode uses it.")),
                            inspectorBox_);
    warn->setWordWrap(true);
    warn->setProperty("warning", true);
    col->addWidget(warn);
    auto *own = new QLabel(T("Lift / press has its own action (below). It can also be added to another motion (further down)."), inspectorBox_);
    own->setWordWrap(true);
    col->addWidget(own);
    {   // what it does: the same choices as the profile's Motions page (liftActionChoices: nothing, scroll, zoom, one key tap per push)
        auto *what = new QGroupBox(T("What it does"), inspectorBox_);
        auto *wl = new QVBoxLayout(what);
        auto *kindLabel = new QLabel(T("What lift / press does:"), what);
        auto *kind = new QComboBox(what);
        kind->setObjectName(QStringLiteral("liftAction"));
        kind->setAccessibleName(T("What lift / press does in this joystick mode"));
        kind->setAccessibleDescription(T("Action for lift / press"));  // a combo box reports its current text as its name
        kindLabel->setBuddy(kind);
        kind->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        kind->setMinimumContentsLength(12);
        kind->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        kind->setMinimumWidth(120);
        ChannelForm &lift = env_.form->lift;
        const ChannelActionType shown = lift.type == ChannelActionType::Off ? ChannelActionType::Default : lift.type;
        int current = 0;
        for (const MotionActionChoice &c : liftActionChoices()) {
            if (c.key() == choiceIdOf(ChannelForm{shown, lift.neg, lift.pos})) current = kind->count();
            kind->addItem(T(c.label), c.key());
        }
        kind->setCurrentIndex(current);
        wl->addWidget(kindLabel);
        wl->addWidget(kind);
        if (lift.usesKeys()) {
            auto committed = [this] {
                if (env_.committed) env_.committed();
            };
            const bool holds = lift.type == ChannelActionType::Hold;  // lift never takes the analog keys: every side may be a click
            for (int side = 0; side < 2; ++side) {
                const QString text = side == 0 ? (holds ? T("Key or mouse button held for the negative direction") : T("Key or mouse click for the negative direction"))
                                                : (holds ? T("Key or mouse button held for the positive direction") : T("Key or mouse click for the positive direction"));
                auto *label = new QLabel(text, what);
                label->setWordWrap(true);
                wl->addWidget(label);
                KeyTreePicker *kc = nullptr;
                wl->addWidget(sidePicker(what, this, side == 0 ? &lift.neg : &lift.pos,
                                         T("Lift / press") + QStringLiteral(": ") + text, committed, &kc));
                label->setBuddy(kc);
            }
            if (QWidget *engine = keyEngineBox(what, this, keyEngineEnv(&lift, Motion::Lift), *env_.strings)) wl->addWidget(engine);
        }
        connect(kind, &QComboBox::activated, this, [this, kind](int idx) {
            ChannelForm &f = env_.form->lift;
            applyChoice(&f, kind->itemData(idx).toInt());
            if (f.usesKeys() && f.type != ChannelActionType::Hold && !f.anyMouse() && (f.neg == "none" || f.pos == "none")) {
                f.neg = "up";
                f.pos = "down";
            }
            if (env_.changed) env_.changed();
        });
        col->addWidget(what);
    }

    // where it is added now and with which weight
    const Settings eff = effectiveSettings(*env_.form, env_.base);
    const AxisRoles liftRoles = deriveAxisRoles(env_.base);
    int where = 0;
    double weight = 1.0;
    const MotionChannel order[3] = {MotionChannel::V, MotionChannel::H, MotionChannel::Twist};
    for (int i = 0; i < 3; ++i) {
        const Mix &mix = motion_map_detail::mixOf(eff, order[i]);
        for (const Term &t : mix.terms)
            if (liftRoles.has(Motion::Lift) && t.axis == liftRoles.axis(Motion::Lift) && t.weight != 0.0) {
                where = i + 1;
                weight = t.weight;
            }
    }
    auto *combo = new QComboBox(inspectorBox_);
    combo->setAccessibleName(T("Add lift / press to"));
    combo->addItem(T("Not used"), -1);
    combo->addItem(T("Push forward / back (vertical channel)"), 0);
    combo->addItem(T("Push sideways (horizontal channel)"), 1);
    combo->addItem(T("Twist"), 2);
    combo->setCurrentIndex(where);
    auto *spin = new QDoubleSpinBox(inspectorBox_);
    spin->setRange(-100.0, 100.0);
    spin->setDecimals(2);
    spin->setSingleStep(0.25);
    spin->setValue(weight);
    spin->setAccessibleName(T("Weight of lift / press"));
    spin->setEnabled(where > 0);
    auto *row = new QFormLayout;
    row->addRow(T("Adds to:"), combo);
    row->addRow(T("Weight:"), spin);
    col->addLayout(row);
    auto apply = [this, combo, spin] {
        const int target = combo->currentData().toInt();
        const double w = target < 0 ? 0.0 : spin->value();
        if (!setLift(*env_.form, env_.base, MotionChannel(std::max(target, 0)), w).ok) return;
        if (env_.changed) env_.changed();
    };
    connect(combo, &QComboBox::activated, this, [apply](int) { apply(); });
    connect(spin, &QDoubleSpinBox::editingFinished, this, [apply, spin] {
        if (spin->isEnabled()) apply();
    });
    auto *show = new QPushButton(T("Show in Settings tab"), inspectorBox_);
    connect(show, &QPushButton::clicked, this, [this] {
        if (env_.showTab) env_.showTab(QStringLiteral("overrides"));
    });
    col->addWidget(show);
}

void MotionEditor::buildButtonInspector(QVBoxLayout *col, int n) {
    inspectorTitle_ = new QLabel(QStringLiteral("<b>") + T("Button %1 in this joystick mode").arg(n).toHtmlEscaped() + QStringLiteral("</b>"), inspectorBox_);
    inspectorTitle_->setTextFormat(Qt::RichText);
    auto *titleRow = new QHBoxLayout;
    titleRow->addWidget(inspectorTitle_, 1);
    titleRow->addWidget(helpButton(inspectorBox_, QStringLiteral("button")));
    col->addLayout(titleRow);
    auto *scope = new QLabel(T("Leave it unbound and the binding of the profile applies."), inspectorBox_);
    scope->setWordWrap(true);
    col->addWidget(scope);
    auto *grid = new QGridLayout;
    grid->setColumnStretch(2, 1);
    col->addLayout(grid);
    if (env_.addButtonRow) env_.addButtonRow(inspectorBox_, grid, 0, n);
    if (env_.takeOverButton) {
        auto *take = new QPushButton(T("Take over from ..."), inspectorBox_);
        take->setObjectName(QStringLiteral("modeTakeOverButton"));
        take->setToolTip(T("Copy the binding of this button in a profile, another joystick mode or a button mode into this joystick mode"));
        connect(take, &QPushButton::clicked, this, [this, n] { env_.takeOverButton(n); });
        col->addWidget(take, 0, Qt::AlignLeft);
    }
}

// ---------------------------------------------------------------------------------------------------------
// Help, templates, hints, tour, Detect by moving, Try

QToolButton *MotionEditor::helpButton(QWidget *parent, const QString &topic) {
    const AxisRoles roles = deriveAxisRoles(env_.base);
    const HelpTopic h = helpTopic(topic.toStdString(), *env_.strings, &roles);
    auto *b = makeInfoButton(parent, T("Help: %1").arg(qs8(h.title)), T("What is this?"));
    b->setText(QStringLiteral("?"));
    const QString text = qs8(h.title) + QStringLiteral("\n\n") + qs8(h.body);
    connect(b, &QToolButton::clicked, this, [b, text] { InfoPopup::open(b, text); });
    return b;
}

void MotionEditor::buildTop(QVBoxLayout *left, QHBoxLayout *tools) {
    // tools: Detect, Try, template, tour (in the row of the device choice)
    detectBtn_ = new QPushButton(T("Detect by moving"), this);
    detectBtn_->setCheckable(true);
    detectBtn_->setToolTip(T("Move the cap or press a button on the device to select it here. The daemon's output is paused while this is on."));
    tryBtn_ = new QPushButton(T("Try this joystick mode"), this);
    tryBtn_->setToolTip(T("Switch to this joystick mode now, on all profiles, to try it in a program"));
    templates_ = new QComboBox(this);
    templates_->setAccessibleName(T("Start from a template"));
    templates_->addItem(T("Start from a template..."), -1);
    const std::vector<MotionTemplate> all = motionTemplates(*env_.strings);
    for (size_t i = 0; i < all.size(); ++i) {
        templates_->addItem(qs8(all[i].title), int(i));
        templates_->setItemData(int(templates_->count()) - 1, spn::ui::qt::plainToolTip(qs8(all[i].description)), Qt::ToolTipRole);
    }
    tourBtn_ = new QPushButton(T("Show me how"), this);
    templates_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    templates_->setMinimumContentsLength(8);
    tools->addWidget(detectBtn_);
    tools->addWidget(tryBtn_);
    tools->addWidget(templates_, 1);
    tools->addWidget(tourBtn_);
    detectState_ = new QLabel(this);
    detectState_->setWordWrap(true);
    detectState_->setTextFormat(Qt::PlainText);
    detectState_->setAccessibleName(T("State of Detect by moving"));
    left->addWidget(detectState_);
    auto *tryRow = new QHBoxLayout;
    tryState_ = new QLabel(this);
    tryState_->setWordWrap(true);
    tryState_->setTextFormat(Qt::PlainText);
    tryState_->setAccessibleName(T("State of Try"));
    backBtn_ = new QPushButton(this);
    tryRow->addWidget(tryState_, 1);
    tryRow->addWidget(backBtn_);
    left->addLayout(tryRow);
    // the tour banner
    tourBox_ = new QFrame(this);
    tourBox_->setFrameShape(QFrame::NoFrame);  // a quiet tip line, not a panel
    auto *tl = new QHBoxLayout(tourBox_);
    tl->setContentsMargins(0, 0, 0, 0);
    tourLabel_ = new QLabel(tourBox_);
    tourLabel_->setWordWrap(true);
    tourLabel_->setTextFormat(Qt::PlainText);
    tourLabel_->setAccessibleName(T("Guided tour"));
    tourClick_ = new QPushButton(T("Click it instead"), tourBox_);
    tourNext_ = new QPushButton(T("Next"), tourBox_);
    tourSkip_ = new QPushButton(T("Skip the tour"), tourBox_);
    tl->addWidget(tourLabel_, 1);
    tl->addWidget(tourClick_);
    tl->addWidget(tourNext_);
    tl->addWidget(tourSkip_);
    left->addWidget(tourBox_);

    client_ = std::make_unique<LiveFeedClient>(env_.connector ? env_.connector : LiveFeedClient::Connector([dir = env_.runtimeDir](std::string *err) {
                                                    return connectFeed(dir, err);
                                                }));
    suspend_ = std::make_unique<SuspendHeartbeat>(env_.runtimeDir);
    clock_.start();
    timer_ = new QTimer(this);
    timer_->setInterval(33);  // ~30 Hz like the Axes page
    timer_->setTimerType(Qt::CoarseTimer);
    connect(timer_, &QTimer::timeout, this, [this] { pollFeed(); });

    connect(detectBtn_, &QPushButton::clicked, this, [this](bool on) { setDetect(on); });
    connect(tryBtn_, &QPushButton::clicked, this, [this] { tryThis(env_.form->id); });
    connect(backBtn_, &QPushButton::clicked, this, [this] {
        const std::string back = tryingFrom_.empty() ? std::string("normal") : tryingFrom_;
        tryThis(back);
        tryingFrom_.clear();
        if (env_.tryingChanged) env_.tryingChanged(std::string());
        refreshTop();
    });
    connect(templates_, &QComboBox::activated, this, [this, all](int i) {
        const int idx = templates_->itemData(i).toInt();
        if (idx < 0 || idx >= int(all.size())) return;
        applyTemplate(*env_.form, all[size_t(idx)]);
        tourEvent(TourEvent::ActionAssigned);
        if (env_.changed) env_.changed();
    });
    connect(tourBtn_, &QPushButton::clicked, this, [this] {
        if (!env_.tour) return;
        env_.tour->restart();
        refreshTour();
    });
    connect(tourClick_, &QPushButton::clicked, this, [this] {
        if (env_.tour) env_.tour->chooseClick();
        refreshTour();
    });
    connect(tourNext_, &QPushButton::clicked, this, [this] {
        if (env_.tour) env_.tour->next();
        refreshTour();
    });
    connect(tourSkip_, &QPushButton::clicked, this, [this] {
        if (env_.tour) {
            env_.tour->dismiss();
            std::string err;
            if (!env_.uiProgressFile.empty()) markMotionTourSeen(env_.uiProgressFile, &err);
        }
        refreshTour();
    });
}

bool MotionEditor::deviceAvailable() const {
    return !env_.deviceName.empty() && env_.daemonRunning && env_.daemonRunning();
}

void MotionEditor::tourEvent(TourEvent e) {
    if (!env_.tour || !env_.tour->active()) return;
    if (env_.tour->on(e)) refreshTour();
}

void MotionEditor::refreshTour() {
    TourModel *t = env_.tour;
    const bool show = t && (t->active() || t->finished());
    tourBox_->setVisible(show);
    if (!show) return;
    t->setDevice(deviceAvailable());
    if (t->finished()) {
        tourLabel_->setText(qs8(t->text(*env_.strings)));
        tourClick_->hide();
        tourNext_->hide();
        tourSkip_->setText(T("Close"));
        std::string err;
        if (!env_.uiProgressFile.empty()) markMotionTourSeen(env_.uiProgressFile, &err);
        return;
    }
    tourLabel_->setText(T("Step %1 of %2: %3").arg(t->number()).arg(TourModel::kSteps).arg(qs8(t->text(*env_.strings))));
    tourClick_->setVisible(t->canClickInstead());
    tourNext_->setVisible(t->step() == TourStep::Try);
    tourSkip_->setText(T("Skip the tour"));
    // a step that waits for the real movement needs Detect to be on
    if (t->waitsForDevice() && !detectOn_) setDetect(true);
}

QString MotionEditor::tourText() const { return tourBox_ && !tourBox_->isHidden() ? tourLabel_->text() : QString(); }

void MotionEditor::refreshTop() {
    if (tryingFrom_.empty() && env_.tryingFrom.empty()) {
        tryState_->clear();
        backBtn_->hide();
    }
    tryBtn_->setEnabled(env_.tryMode != nullptr);
    if (!tryingFrom_.empty()) {
        const QString name = env_.form->name.empty() ? qs8(env_.form->id) : qs8(env_.form->name);
        tryState_->setText(T("Trying: %1 on all profiles").arg(name));
        backBtn_->setText(T("Back to %1").arg(qs8(tryingFrom_)));
        backBtn_->show();
    }
}

QString MotionEditor::tryText() const { return tryState_ ? tryState_->text() : QString(); }

void MotionEditor::tryThis(const std::string &modeId) {
    if (!env_.tryMode) return;
    if (env_.flush) env_.flush();  // the daemon applies the saved file: write the last edit first
    const bool trying = modeId == env_.form->id;
    const std::string before = env_.activeMode ? env_.activeMode() : std::string();
    std::string err;
    if (!env_.tryMode(modeId, &err)) {
        tryState_->setText(mark(StatusKind::Error, T("Could not ask the daemon: %1").arg(qs8(err))));
        return;
    }
    if (trying) {
        tryingFrom_ = before.empty() || before == modeId ? std::string("normal") : before;
        if (env_.tryingChanged) env_.tryingChanged(tryingFrom_);
        tourEvent(TourEvent::Tried);
    }
    refreshTop();
}

void MotionEditor::refreshHints() {
    auto *col = static_cast<QVBoxLayout *>(hintBox_->layout());
    clearLayoutNow(col);
    hints_.clear();
    // the number of buttons of the device shown in the picture
    std::string name = env_.deviceName;
    int reported = env_.reportedButtons;
    if (deviceChoice_ > 0) {
        name = deviceCombo_->itemData(deviceChoice_).toString().toStdString();
        reported = 0;
    }
    const int buttons = knownDeviceModel(name) ? deviceLayout(name, reported).count : 0;
    for (const MotionHint &h : motionHints(*env_.form, env_.base, buttons, *env_.strings)) {
        auto *row = new QWidget(hintBox_);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        auto *text = new QLabel(mark(StatusKind::Warning, qs8(h.text)), row);
        text->setWordWrap(true);
        text->setProperty("warning", true);
        hl->addWidget(text, 1);
        auto *fix = new QPushButton(qs8(h.fixLabel), row);
        fix->setAccessibleName(qs8(h.text) + QStringLiteral(": ") + qs8(h.fixLabel));
        hl->addWidget(fix);
        col->addWidget(row);
        hints_ << qs8(h.text);
        const std::string fixId = h.fixId;
        connect(fix, &QPushButton::clicked, this, [this, fixId] {
            if (fixId.rfind("select:", 0) == 0) {
                select(qs8(std::string("m:") + fixId.substr(7)));
            } else if (fixId.rfind("tab:", 0) == 0) {
                if (env_.showTab) env_.showTab(qs8(fixId.substr(4)));
            } else if (fixId.rfind("unbind:", 0) == 0) {
                env_.form->setButton(std::atoi(fixId.c_str() + 7), "none");
                if (env_.changed) env_.changed();
            }
        });
    }
}

QStringList MotionEditor::hintTexts() const { return hints_; }

void MotionEditor::refreshSentence() {
    const QString id = picture_->current();
    QString text;
    if (id.startsWith(QLatin1String("m:"))) {
        Motion m;
        if (motionFromId(id.mid(2).toStdString(), &m)) {
            const AxisRoles roles = deriveAxisRoles(env_.base);
            const bool known = roles.has(m);
            const double live = detectOn_ && haveLive_ && known ? double(live_[roles.axis(m)]) : std::nan("");
            text = qs8(motionSentence(*env_.form, env_.base, m, *env_.strings, live));
            const MotionLabel l = motionLabel(*env_.form, env_.base, m, *env_.strings);
            if (!l.badge.empty()) text += QStringLiteral(". ") + qs8(l.badge);
            if (liveBar_) liveBar_->setValue(known ? std::min(int(kFullDeflection), std::abs(int(live_[roles.axis(m)]))) : 0);
        }
    } else if (id.startsWith(QLatin1String("b:"))) {
        text = qs8(buttonSentence(*env_.form, id.mid(2).toInt(), *env_.strings));
    }
    sentence_->setText(text);
}

QString MotionEditor::liveSentence() const { return sentence_ ? sentence_->text() : QString(); }

void MotionEditor::setDetect(bool on) {
    if (on == detectOn_ && detectBtn_->isChecked() == on) return;
    detectOn_ = on;
    detectBtn_->setChecked(on);
    detectModel_.reset();
    haveLive_ = false;
    if (on) {
        timer_->start();
    } else {
        timer_->stop();
        suspend_->stop();  // the output resumes at once
    }
    if (env_.detectChanged) env_.detectChanged(on);
    if (liveBar_) liveBar_->setVisible(on);
    refreshDetect();
    refreshSentence();
}

void MotionEditor::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    if (detectOn_) setDetect(false);  // leaving the tab switches it off and resumes the output
}

QString MotionEditor::detectText() const { return detectState_ ? detectState_->text() : QString(); }

void MotionEditor::refreshDetect() {
    if (!detectState_) return;
    if (!detectOn_) {
        detectState_->clear();
        detectState_->hide();
        return;
    }
    const int64_t now = clock_.nsecsElapsed();
    const ConnState cs = connectionState(client_->state(), feed_, now);
    QString text;
    if (cs == ConnState::Connected || haveLive_)
        text = T("Detecting: move the cap or press a button on the device to select it. The daemon's output is paused while this is on.");
    else
        text = mark(StatusKind::Warning, T("Start the daemon to detect by moving.") + QLatin1Char(' ') + T(connectionHint(cs, currentHostOs())));
    detectState_->setText(text);
    detectState_->show();
}

void MotionEditor::pollFeed() {
    const int64_t now = clock_.nsecsElapsed();
    suspend_->update(detectOn_, now);
    client_->poll(now, [&](const LiveSample &s) {
        feed_.update(s, now);
        injectSample(s.axes, s.buttons);
    });
    refreshDetect();
}

void MotionEditor::injectSample(const RawAxes &axes, uint32_t buttons) {
    if (!detectOn_) return;
    live_ = axes;
    haveLive_ = true;
    const DetectEvent e = detectModel_.feed(axes, buttons);
    if (e.kind != DetectEvent::None) {
        fromDetect_ = true;
        if (e.kind == DetectEvent::Motion) {
            Motion moved;
            if (deriveAxisRoles(env_.base).motionOf(e.axis, &moved))
                select(QStringLiteral("m:") + QString::fromLatin1(motionId(moved)));
            tourEvent(TourEvent::MotionDetected);
        } else {
            if (!select(QStringLiteral("b:") + QString::number(e.button))) {
                // a button the picture does not show: nothing to select
            } else {
                tourEvent(TourEvent::ButtonDetected);
            }
        }
        fromDetect_ = false;
    }
    refreshSentence();
}
