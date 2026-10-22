// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/motion_editor.hpp"

#include <QAccessible>
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
#include <QPushButton>
#include <QRadioButton>
#include <QToolTip>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <set>

#include "key_combo.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/motion_map_model.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

// ---------------------------------------------------------------------------------------------------------
// The picture

namespace {
constexpr int kMinTarget = 24;  // px, the smallest item

int itemPad(const QFontMetrics &fm) { return std::max(4, fm.height() / 3); }
}  // namespace

MotionPicture::MotionPicture(QWidget *parent) : QWidget(parent) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(false);
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
    const int pad = itemPad(fm), gap = std::max(6, fm.height() / 2);
    int cw = 0;
    for (const Item &it : items_)
        if (it.id.startsWith(QLatin1String("m:")))
            cw = std::max({cw, fm.horizontalAdvance(it.title) + 2 * pad, std::min(fm.horizontalAdvance(it.text) + 2 * pad, 12 * fm.height())});
    cw = std::max(cw, 8 * fm.height());
    return 2 * cw + 2 * gap + 5 * fm.height();
}

MotionPicture::Plan MotionPicture::plan(int width) const {
    const QFontMetrics fm(font());
    const int em = fm.height(), pad = itemPad(fm), gap = std::max(6, em / 2);
    Plan p;
    p.rects.resize(items_.size());
    // Six motion chips in two columns around the cap.
    int cw = 8 * em;
    for (const Item &it : items_)
        if (it.id.startsWith(QLatin1String("m:")))
            cw = std::max({cw, fm.horizontalAdvance(it.title) + 2 * pad, std::min(fm.horizontalAdvance(it.text) + 2 * pad, 12 * em)});
    const int ch = std::max(kMinTarget, 2 * em + 2 * pad);
    static const char *const left[3] = {"m:v_push", "m:h_push", "m:twist"};
    static const char *const right[3] = {"m:v_tilt", "m:h_tilt", "m:lift"};
    const int w = std::max(width, 2 * cw + 2 * gap + 5 * em);
    for (int r = 0; r < 3; ++r) {
        const int li = indexOf(QLatin1String(left[r])), ri = indexOf(QLatin1String(right[r]));
        if (li >= 0) p.rects[li] = QRect(0, r * (ch + gap), cw, ch);
        if (ri >= 0) p.rects[ri] = QRect(w - cw, r * (ch + gap), cw, ch);
    }
    p.cap = QRect(cw + gap, 0, w - 2 * cw - 2 * gap, 3 * ch + 2 * gap);
    int y = 3 * ch + 2 * gap + 2 * gap;
    // The buttons below, with a heading line.
    QList<int> btn;
    for (int i = 0; i < items_.size(); ++i)
        if (items_[i].id.startsWith(QLatin1String("b:"))) btn << i;
    p.buttonsTop = y;
    if (!btn.isEmpty()) {
        y += em + gap;  // heading
        int bw = kMinTarget;
        for (int i : btn) bw = std::max(bw, std::min(fm.horizontalAdvance(items_[i].text) + 2 * pad, 9 * em));
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
    p.height = std::max(y - (btn.isEmpty() ? 2 * gap : 0), 3 * ch + 2 * gap);
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
    const QPoint c = capRect_.center();
    const int radius = std::max(2 * em, std::min(capRect_.width(), capRect_.height()) / 2 - em / 2);
    p.setPen(QPen(line, 1));
    for (const Item &it : items_) {
        if (!it.id.startsWith(QLatin1String("m:"))) continue;
        const bool leftSide = it.rect.center().x() < c.x();
        const QPoint from(leftSide ? it.rect.right() : it.rect.left(), it.rect.center().y());
        p.drawLine(from, QPoint(c.x() + (leftSide ? -radius / 2 : radius / 2), c.y() + (it.rect.center().y() - c.y()) / 3));
    }
    p.setBrush(base);
    p.setPen(QPen(line, 2));
    p.drawEllipse(c, radius, radius);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, radius * 3 / 5, radius * 3 / 5);
    p.setPen(mutedText);
    const QString capText = T("Cap");
    p.drawText(QRect(c.x() - radius, c.y() - em / 2, 2 * radius, em), Qt::AlignCenter, capText);

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
        p.setBrush(base);
        p.drawRoundedRect(QRectF(it.rect).adjusted(1.5, 1.5, -1.5, -1.5), em / 3.0, em / 3.0);
        if (cur && hasFocus()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(palette().color(QPalette::WindowText), 2, Qt::DotLine));
            p.drawRoundedRect(QRectF(it.rect).adjusted(-3, -3, 3, 3), em / 3.0 + 2, em / 3.0 + 2);
        }
        const QRect inner = it.rect.adjusted(pad, pad / 2, -pad, -pad / 2);
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(it.muted ? mutedText : text);
        p.drawText(inner.adjusted(0, 0, 0, -em), Qt::AlignHCenter | Qt::AlignTop,
                   QFontMetrics(f).elidedText(it.title, Qt::ElideRight, inner.width()));
        p.setFont(font());
        p.setPen(it.muted ? mutedText : text);
        p.drawText(QRect(inner.left(), inner.bottom() - em + 1, inner.width(), em), Qt::AlignHCenter | Qt::AlignBottom,
                   fm.elidedText(it.text, Qt::ElideRight, inner.width()));
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
            return;
        }
    QWidget::mousePressEvent(e);
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
    const QPoint c = items_[from].rect.center();
    int best = -1;
    double bestScore = 0;
    for (int i = 0; i < items_.size(); ++i) {
        if (i == from) continue;
        const QPoint d = items_[i].rect.center() - c;
        const int along = d.x() * dx + d.y() * dy;
        if (along <= 0) continue;
        const int across = std::abs(dx ? d.y() : d.x());
        const double score = along + 2.0 * across;  // prefer the item straight ahead
        if (best < 0 || score < bestScore) {
            best = i;
            bestScore = score;
        }
    }
    return best;
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
    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    // Left: device choice, picture, live sentence.
    auto *left = new QVBoxLayout;
    auto *devRow = new QHBoxLayout;
    auto *devLabel = new QLabel(T("Device:"), this);
    deviceCombo_ = new QComboBox(this);
    deviceCombo_->setAccessibleName(T("Device whose buttons are shown"));
    devLabel->setBuddy(deviceCombo_);
    devRow->addWidget(devLabel);
    devRow->addWidget(deviceCombo_, 1);
    left->addLayout(devRow);
    picture_ = new MotionPicture(this);
    left->addWidget(picture_);
    sentence_ = new QLabel(this);
    sentence_->setWordWrap(true);
    sentence_->setTextFormat(Qt::PlainText);
    sentence_->setAccessibleName(T("What the selected item does"));
    left->addWidget(sentence_);
    left->addStretch(1);
    root->addLayout(left, 3);
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
    });
    connect(picture_, &MotionPicture::selected, this, [this](const QString &id) {
        rebuildInspector();
        emit selectionChanged(id);
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
}

bool MotionEditor::select(const QString &id) {
    for (const MotionPicture::Item &it : picture_->items())
        if (it.id == id) {
            picture_->setCurrent(id);
            return true;
        }
    return false;
}

QString MotionEditor::inspectorTitle() const { return inspectorTitle_ ? inspectorTitle_->text() : QString(); }

void MotionEditor::rebuildPicture() {
    const StringProvider &s = *env_.strings;
    QList<MotionPicture::Item> items;
    for (const MotionLabel &l : motionLabels(*env_.form, env_.base, s)) {
        MotionPicture::Item it;
        it.id = QStringLiteral("m:") + QString::fromLatin1(motionId(l.motion));
        it.title = qs8(l.name);
        it.text = qs8(l.action) + (l.reversed ? QStringLiteral(", ") + T("reversed") : QString());
        it.muted = l.kind != MotionKind::Active;
        it.tooltip = qs8(l.name) + QStringLiteral(" (") + qs8(l.how) + QStringLiteral("): ") + it.text +
                     (l.badge.empty() ? QString() : QStringLiteral(". ") + qs8(l.badge));
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
    // clear
    while (QLayoutItem *item = inspectorCol_->takeAt(0)) {
        delete item->widget();  // at once: a lookup right after a selection must not find the old controls
        delete item;
    }
    inspectorTitle_ = nullptr;
    const QString id = picture_->current();
    sentence_->clear();
    if (id.startsWith(QLatin1String("m:"))) {
        Motion m;
        if (motionFromId(id.mid(2).toStdString(), &m)) {
            if (m == Motion::Lift) buildLiftInspector(inspectorCol_);
            else buildMotionInspector(inspectorCol_, m);
            const MotionLabel l = motionLabel(*env_.form, env_.base, m, *env_.strings);
            sentence_->setText(qs8(l.name) + QStringLiteral(": ") + qs8(l.action) + (l.reversed ? QStringLiteral(", ") + T("reversed") : QString()) +
                               (l.badge.empty() ? QString() : QStringLiteral(". ") + qs8(l.badge)));
        }
    } else if (id.startsWith(QLatin1String("b:"))) {
        const int n = id.mid(2).toInt();
        buildButtonInspector(inspectorCol_, n);
        const std::string *act = env_.form->button(n);
        sentence_->setText(T("Button %1: %2").arg(n).arg(act ? qs8(buttonActionWords(*act, *env_.strings)) : T("not bound here, the profile's binding applies")));
    }
    inspectorCol_->addStretch(1);
}

void MotionEditor::buildMotionInspector(QVBoxLayout *col, Motion m) {
    const MotionLabel l = motionLabel(*env_.form, env_.base, m, *env_.strings);
    inspectorTitle_ = new QLabel(QStringLiteral("<b>") + qs8(l.name).toHtmlEscaped() + QStringLiteral("</b>"), inspectorBox_);
    inspectorTitle_->setTextFormat(Qt::RichText);
    col->addWidget(inspectorTitle_);
    auto *how = new QLabel(qs8(l.how), inspectorBox_);
    col->addWidget(how);

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

    // Action cards: one radio button each, with the words of the action.
    auto *actions = new QGroupBox(T("What it does"), inspectorBox_);
    auto *av = new QVBoxLayout(actions);
    auto *group = new QButtonGroup(actions);
    const ChannelForm cur = actionOf(*env_.form, env_.base, m);
    for (const KindCard &c : kCards) {
        auto *rb = new QRadioButton(T(c.label), actions);
        rb->setToolTip(T(c.tip));
        rb->setChecked(cur.type == c.type);
        group->addButton(rb);
        av->addWidget(rb);
        connect(rb, &QRadioButton::clicked, this, [this, m, c, cur] {
            ChannelForm a;
            a.type = c.type;
            if (a.usesKeys()) {
                a.neg = cur.usesKeys() && !cur.neg.empty() ? cur.neg : "up";
                a.pos = cur.usesKeys() && !cur.pos.empty() ? cur.pos : "down";
            }
            if (!assign(*env_.form, env_.base, m, a).ok) return;
            if (env_.changed) env_.changed();
        });
    }
    col->addWidget(actions);

    if (cur.usesKeys())
        if (ChannelForm *ref = actionRef(*env_.form, env_.base, m)) {
            auto *keys = new QGroupBox(T("Keys"), inspectorBox_);
            auto *kg = new QGridLayout(keys);
            auto committed = [this] {
                if (env_.committed) env_.committed();
            };
            kg->addWidget(new QLabel(T("Negative:"), keys), 0, 0, Qt::AlignRight);
            kg->addWidget(chordPicker(keys, this, &ref->neg, qs8(l.name) + QStringLiteral(" ") + T("negative"), committed), 0, 1);
            kg->addWidget(new QLabel(T("Positive:"), keys), 1, 0, Qt::AlignRight);
            kg->addWidget(chordPicker(keys, this, &ref->pos, qs8(l.name) + QStringLiteral(" ") + T("positive"), committed), 1, 1);
            kg->setColumnStretch(1, 1);
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

    // Same as / swap with another motion
    auto *copyRow = new QHBoxLayout;
    auto *same = new QComboBox(inspectorBox_);
    auto *swap = new QComboBox(inspectorBox_);
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
    col->addLayout(copyRow);
    connect(same, &QComboBox::activated, this, [this, m, same](int i) {
        Motion o;
        if (i <= 0 || !motionFromId(same->itemData(i).toString().toStdString(), &o)) return;
        if (assign(*env_.form, env_.base, m, actionOf(*env_.form, env_.base, o)).ok && env_.changed) env_.changed();
    });
    connect(swap, &QComboBox::activated, this, [this, m, swap](int i) {
        Motion o;
        if (i <= 0 || !motionFromId(swap->itemData(i).toString().toStdString(), &o)) return;
        const ChannelForm mine = actionOf(*env_.form, env_.base, m), theirs = actionOf(*env_.form, env_.base, o);
        if (assign(*env_.form, env_.base, m, theirs).ok && assign(*env_.form, env_.base, o, mine).ok && env_.changed) env_.changed();
    });

    buildTuning(col, m);
}

void MotionEditor::buildTuning(QVBoxLayout *col, Motion m) {
    const std::string prefix = tuningPrefix(*env_.form, env_.base, m);
    if (prefix.empty()) return;
    static const char *const fields[][2] = {{"SENS", "Sensitivity"}, {"DEADZONE", "Dead zone"}, {"CURVE", "Curve"}, {"SMOOTH_MS", "Smoothing (ms)"}};
    bool anySet = false;
    for (const auto &f : fields) anySet = anySet || env_.form->overrideValue(prefix + f[0]);
    auto *box = new QGroupBox(T("Tune this motion"), inspectorBox_);
    box->setCheckable(true);
    box->setChecked(anySet);
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
    // collapsed: only the check box line shows
    for (QWidget *w : box->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) w->setVisible(anySet);
    connect(box, &QGroupBox::toggled, this, [box](bool on) {
        for (QWidget *w : box->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) w->setVisible(on);
    });
    col->addWidget(box);
}

void MotionEditor::buildLiftInspector(QVBoxLayout *col) {
    const MotionLabel l = motionLabel(*env_.form, env_.base, Motion::Lift, *env_.strings);
    inspectorTitle_ = new QLabel(QStringLiteral("<b>") + qs8(l.name).toHtmlEscaped() + QStringLiteral("</b>"), inspectorBox_);
    inspectorTitle_->setTextFormat(Qt::RichText);
    col->addWidget(inspectorTitle_);
    col->addWidget(new QLabel(qs8(l.how), inspectorBox_));
    auto *warn = new QLabel(mark(StatusKind::Warning, T("Lift / press is prone to false triggers when the cap is grabbed or released. "
                                                       "No built-in joystick mode uses it.")),
                            inspectorBox_);
    warn->setWordWrap(true);
    warn->setProperty("warning", true);
    col->addWidget(warn);
    col->addWidget(new QLabel(T("Lift / press has no action of its own: it can be added to another motion."), inspectorBox_));

    // where it is added now and with which weight
    const Settings eff = effectiveSettings(*env_.form, env_.base);
    int where = 0;
    double weight = 1.0;
    const MotionChannel order[3] = {MotionChannel::V, MotionChannel::H, MotionChannel::Twist};
    for (int i = 0; i < 3; ++i) {
        const Mix &mix = motion_map_detail::mixOf(eff, order[i]);
        for (const Term &t : mix.terms)
            if (t.axis == AX_Y && t.weight != 0.0) {
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
    inspectorTitle_ = new QLabel(QStringLiteral("<b>") + T("Button %1").arg(n).toHtmlEscaped() + QStringLiteral("</b>"), inspectorBox_);
    inspectorTitle_->setTextFormat(Qt::RichText);
    col->addWidget(inspectorTitle_);
    auto *grid = new QGridLayout;
    grid->setColumnStretch(2, 1);
    col->addLayout(grid);
    if (env_.addButtonRow) env_.addButtonRow(inspectorBox_, grid, 0, n);
}
