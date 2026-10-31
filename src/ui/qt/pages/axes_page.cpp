// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "axes_page.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <string>
#include <unordered_map>

#include "a11y_announce.hpp"
#include "axis_convention.hpp"
#include "undo_focus.hpp"
#include "ui/model/undo_rules.hpp"
#include "config.hpp"
#include "seed.hpp"
#include "motion_view.hpp"
#include "ui/model/axis_roles_model.hpp"
#include "ui/model/fresh_state.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/split_parts_model.hpp"
#include "ui/model/ui_settings.hpp"
#include "ui/model/suspend_heartbeat.hpp"
#include "ui/model/ui_progress.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

namespace {

// ---- helpers --------------------------------------------------------------------------------

// Translator for the wizard header (it formats msgids through a plain C hook). The cache keeps
// the returned pointers valid.
const char *wizardTranslate(const char *msgid) {
    static std::unordered_map<std::string, std::string> cache;
    auto it = cache.find(msgid);
    if (it == cache.end()) it = cache.emplace(msgid, T(msgid).toUtf8().toStdString()).first;
    return it->second.c_str();
}

// Untrusted UTF-8 (errors, feed text): invalid bytes are replaced before the shared qs8.
QString qs(const std::string &s) { return qs8(sanitizeUtf8(s)); }

}  // namespace

// ---- the custom-drawn live view -------------------------------------------------------------------

// Draws the axes (bar, peak marker, number), optionally the channel outputs, button indicators and
// the mode line. Colors come from the palette only; a 30 Hz timer of the page calls update().
class LiveBarsWidget : public QWidget {
public:
    LiveBarsWidget(const AxesViewModel *vm, bool compact, QWidget *parent)
        : QWidget(parent), vm_(vm), compact_(compact) {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::MinimumExpanding);
        setAccessibleName(T("Live axis values"));
        setToolTip(T("Live values of the six axes as the daemon reads them (before any processing). "
                     "The thin marker is the peak."));
        for (int i = 0; i < AX_COUNT; i++) axisLabel_[i] = QString::fromUtf8(axisName(Axis(i)));
        chanLabel_[0] = T("Scroll V");
        chanLabel_[1] = T("Scroll H");
        chanLabel_[2] = T("Zoom");
    }

    QSize minimumSizeHint() const override {
        const int rh = rowHeight();
        const int rows = AX_COUNT + (compact_ ? 0 : 3 + 3 + 2 + (parts_.empty() ? 0 : int(parts_.size()) + 1));
        return QSize(fontMetrics().horizontalAdvance(QStringLiteral("0")) * 36, rows * rh);
    }
    QSize sizeHint() const override { return minimumSizeHint() + QSize(120, 40); }

    // Push and tilt parts of the channels the active joystick mode splits (empty: none), drawn below the
    // channel outputs. The size follows the number of rows.
    void setParts(std::vector<spn::ui::SplitPartRow> parts) {
        const bool resize = parts.size() != parts_.size();
        parts_ = std::move(parts);
        describe();
        if (resize) updateGeometry();
    }
    // The plain-word second name of each axis ("z: Push forward / back"); an empty text leaves the axis name alone.
    void setAxisWords(const std::array<QString, AX_COUNT> &words) {
        bool changed = false;
        for (int i = 0; i < AX_COUNT; i++) {
            const QString name = QString::fromUtf8(axisName(Axis(i)));
            const QString label = words[size_t(i)].isEmpty() ? name : name + QStringLiteral(": ") + words[size_t(i)];
            if (axisLabel_[i] != label) {
                axisLabel_[i] = label;
                changed = true;
            }
        }
        if (!changed) return;
        describe();
        updateGeometry();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPalette &pal = palette();
        const int rh = rowHeight();
        const QFontMetrics fm = fontMetrics();
        int labelW = 0;  // the widest label, translated names included, so that none is cut
        for (const QString &l : axisLabel_) labelW = std::max(labelW, fm.horizontalAdvance(l));
        for (const QString &l : chanLabel_) labelW = std::max(labelW, fm.horizontalAdvance(l));
        for (const auto &r : parts_) labelW = std::max(labelW, fm.horizontalAdvance(qs8(r.label)));
        labelW += 18;
        const int valueW = fm.horizontalAdvance(QStringLiteral("-00000")) + 8;
        const int barX = labelW, barW = std::max(40, width() - labelW - valueW - 4);
        const bool live = vm_->hasData();
        const bool paused = vm_->paused();
        QColor track = pal.color(QPalette::WindowText);  // a soft track: the text colour at low strength
        track.setAlpha(36);
        const QColor text = pal.color(QPalette::Text);
        const QColor bar = pal.color(live && !paused ? QPalette::Active : QPalette::Disabled, QPalette::Highlight);
        const QColor peak = pal.color(QPalette::Link);

        int y = 2;
        auto row = [&](const QString &label, double frac, double peakFrac, bool hasPeak, const QString &value) {
            const QRect r(barX, y + 3, barW, rh - 6);
            p.setPen(text);
            p.drawText(QRect(0, y, labelW - 6, rh), Qt::AlignVCenter | Qt::AlignRight, label);
            p.setPen(Qt::NoPen);
            p.setBrush(track);
            p.drawRoundedRect(r, 5, 5);
            const int cx = r.center().x();
            const int len = int(std::lround(frac * (r.width() / 2.0)));
            p.setBrush(bar);
            p.drawRect(QRect(std::min(cx, cx + len), r.top() + 1, std::max(1, std::abs(len)), r.height() - 2));
            p.setPen(QPen(text, 1));
            p.drawLine(cx, r.top() - 1, cx, r.bottom() + 1);
            if (hasPeak) {
                const int px = cx + int(std::lround(peakFrac * (r.width() / 2.0)));
                p.setPen(QPen(peak, 2));
                p.drawLine(px, r.top() - 2, px, r.bottom() + 2);
            }
            p.setPen(live ? text : pal.color(QPalette::Disabled, QPalette::Text));
            p.drawText(QRect(r.right() + 4, y, valueW, rh), Qt::AlignVCenter | Qt::AlignRight, value);
            y += rh;
        };

        for (int i = 0; i < AX_COUNT; i++) {
            buf_.setNum(vm_->shown().axes[i]);
            row(axisLabel_[i], vm_->fraction(Axis(i)), vm_->peakFraction(Axis(i)), live, buf_);
        }
        if (compact_) return;

        y += rh / 2;
        for (int i = 0; i < 3; i++) {
            buf_.setNum(vm_->channelValue(i), 'f', 1);
            row(chanLabel_[i], vm_->channelFraction(i), 0.0, false, buf_);
        }

        if (!parts_.empty()) {
            // Push (x, y, z) and tilt (rx, rz) part of a split channel, before sensitivity and curve.
            y += rh / 2;
            for (const auto &r : parts_) {
                buf_.setNum(r.value);
                row(qs8(r.label), spn::ui::barFraction(r.value, vm_->fullScale()), 0.0, false, buf_);
            }
        }

        y += rh / 2;
        // Button indicators 1..N (filled = pressed), wrapped.
        const int cell = rh + 2, count = vm_->buttonCount();
        const int perRow = std::max(1, (width() - 4) / cell);
        for (int b = 1; b <= count; b++) {
            const int col = (b - 1) % perRow, rowNo = (b - 1) / perRow;
            const QRect r(2 + col * cell, y + rowNo * cell, rh, rh);
            const bool down = vm_->buttonDown(b);
            p.setPen(QPen(pal.color(QPalette::Mid), 1));
            p.setBrush(down ? QBrush(pal.color(QPalette::Highlight)) : QBrush(Qt::NoBrush));
            p.drawRoundedRect(r, 4, 4);
            p.setPen(down ? pal.color(QPalette::HighlightedText) : text);
            buf_.setNum(b);
            p.drawText(r, Qt::AlignCenter, buf_);
        }
        y += ((count - 1) / perRow + 1) * cell + rh / 2;

        // Mode line.
        const LiveSample &s = vm_->shown();
        status_.clear();
        if (!vm_->deviceLabel().empty()) status_ = qs(truncateUtf8(vm_->deviceLabel(), 96)) + QStringLiteral("  |  ");
        status_ += T("Joystick mode:") + QLatin1Char(' ') + (live ? qs(truncateUtf8(s.mode, 64)) : QStringLiteral("-"));
        if (live) {
            if (!s.enabled) status_ += QStringLiteral("  |  ") + T("disabled");
            if (s.nav) status_ += QStringLiteral("  |  ") + T("navigation");
            if (s.zooming) status_ += QStringLiteral("  |  ") + T("zooming");
            if (s.idle) status_ += QStringLiteral("  |  ") + T("idle");
        }
        if (paused) status_ += QStringLiteral("  |  ") + T("paused");
        p.setPen(text);
        p.drawText(QRect(2, y, width() - 4, rh), Qt::AlignVCenter | Qt::AlignLeft, status_);
    }

private:
    // The axis names with their plain words and the parts of a split channel, for a screen reader (the bars are drawn).
    void describe() {
        QString desc;
        for (const QString &l : axisLabel_)
            if (l.contains(QLatin1Char(':'))) desc += l + QLatin1Char('\n');
        for (const auto &r : parts_)
            desc += qs8(r.label) + QStringLiteral(": ") + qs8(r.action) + QLatin1Char('\n');
        setAccessibleDescription(desc.trimmed());
    }
    int rowHeight() const { return fontMetrics().height() + 10; }

    const AxesViewModel *vm_;
    bool compact_;
    std::vector<spn::ui::SplitPartRow> parts_;
    QString axisLabel_[AX_COUNT], chanLabel_[3];
    QString buf_, status_;  // reused per frame
};

// ---- the page ---------------------------------------------------------------------------------

AxesPage::AxesPage(const UiContext &ctx, const AxesPageEnv &env, QWidget *parent)
    : QWidget(parent), ctx_(ctx), connector_(env.connector) {
    wiz::trHook() = &wizardTranslate;  // wizard messages are built toolkit-free
    if (!connector_) {
        const std::string dir = ctx_.runtimeDir;  // empty on Windows (named pipe)
        connector_ = [dir](std::string *err) { return connectFeed(dir, err); };
    }
    client_ = std::make_unique<LiveFeedClient>(connector_);
    session_ = std::make_unique<AxesSession>(*client_);
    clock_.start();
    // The daemon sends no output while this page evaluates the mouse; the file
    // is removed when the page is hidden, destroyed or the application quits.
    suspend_ = std::make_unique<SuspendHeartbeat>(ctx_.runtimeDir);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] { suspend_->stop(); });

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    stack_ = new QStackedWidget(this);
    outer->addWidget(stack_);
    auto *live = new QWidget;
    auto *wizard = new QWidget;
    buildLivePage(live);
    buildWizardPage(wizard);
    stack_->addWidget(live);
    stack_->addWidget(wizard);

    timer_ = new QTimer(this);
    timer_->setInterval(33);  // ~30 Hz
    timer_->setTimerType(Qt::CoarseTimer);
    connect(timer_, &QTimer::timeout, this, [this] { pollOnce(now()); });
    // Ctrl+Z (Cmd+Z) does the same as the Undo button, except while a text field has the focus. No time limit.
    auto *undoKey = new QShortcut(QKeySequence::Undo, this);
    undoKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(undoKey, &QShortcut::activated, this, [this] {
        if (spn::ui::undoShortcutApplies(undo_.valid() && undoBar_->isVisibleTo(this), focusInTextInput())) undoWizard();
    });
    refreshLive(now());  // the "Daemon: ..." line is there from the first frame
}

AxesPage::~AxesPage() {
    suspend_->stop();
    if (wiz::trHook() == &wizardTranslate) wiz::trHook() = [](const char *s) { return s; };
}

void AxesPage::buildLivePage(QWidget *page) {
    auto *l = new QVBoxLayout(page);
    // Title and the three buttons in one row at the top: the buttons stay in reach in a low window, whatever the
    // height of the bars below them.
    auto *top = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("<b>") + T("Axes / assistant") + QStringLiteral("</b>"));
    top->addWidget(title);
    top->addStretch(1);
    l->addLayout(top);
    connLine_ = new QLabel;
    connLine_->setTextFormat(Qt::PlainText);
    connHint_ = new QLabel;
    connHint_->setTextFormat(Qt::PlainText);
    connHint_->setWordWrap(true);
    connHint_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(connLine_);
    l->addWidget(connHint_);
    undoBar_ = new QWidget;  // "Axis mixes saved to Global. [Undo]" for a while after Apply
    auto *ubl = new QHBoxLayout(undoBar_);
    ubl->setContentsMargins(0, 0, 0, 0);
    undoApply_ = new QPushButton(T("Undo"));
    undoApply_->setToolTip(T("Put the axis mixes and direction switches of the Global profile back as they were before Apply.") +
                           QStringLiteral(" (") + QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText) + QLatin1Char(')'));
    auto *dismissApply = new QPushButton(T("Dismiss"));
    dismissApply->setObjectName(QStringLiteral("undoDismiss"));
    dismissApply->setToolTip(T("Close this bar; the axis mixes stay as Apply set them"));
    ubl->addWidget(undoApply_);
    ubl->addWidget(dismissApply);
    connect(dismissApply, &QPushButton::clicked, this, [this] { dropUndo(); });
    ubl->addStretch(1);
    undoBar_->hide();
    l->addWidget(undoBar_);
    connect(undoApply_, &QPushButton::clicked, this, [this] { undoWizard(); });
    devRow_[0] = makeDeviceRow(devCombo_[0], devLabel_[0]);
    l->addWidget(devRow_[0]);
    bars_ = new LiveBarsWidget(&session_->view(), false, page);
    l->addWidget(bars_, 1);
    auto *parts = new QLabel;  // filled by refreshPartsHint(): the axes of the shown configuration (issue #186)
    partsHint_ = parts;
    parts->setTextFormat(Qt::PlainText);
    refreshPartsHint();
    parts->setWordWrap(true);
    l->addWidget(parts);

    auto *row = top;
    pause_ = new QPushButton(T("Pause"));
    pause_->setCheckable(true);
    pause_->setToolTip(T("Freeze the display to read the values."));
    auto *reset = new QPushButton(T("Reset peaks"));
    reset->setToolTip(T("Clear the peak markers and the bar scale."));
    openWiz_ = new QPushButton(T("Axis assistant..."));
    openWiz_->setToolTip(T("Learn which axis does what by moving the cap, step by step."));
    row->addWidget(pause_);
    row->addWidget(reset);
    row->addWidget(openWiz_);
    QWidget::setTabOrder(pause_, reset);
    QWidget::setTabOrder(reset, openWiz_);

    connect(pause_, &QPushButton::toggled, this, [this](bool on) { session_->view().setPaused(on); });
    connect(reset, &QPushButton::clicked, this, [this] { session_->view().resetPeaks(); });
    connect(openWiz_, &QPushButton::clicked, this, [this] { openWizard(); });
}

// "Device: [All devices | A | B]": pins the page and the assistant to one device (issue #139). Hidden
// with fewer than two devices (AxesSession::selectableDevices is empty then).
QWidget *AxesPage::makeDeviceRow(QComboBox *&combo, QLabel *&label) {
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    label = new QLabel(T("Device:"));
    label->setTextFormat(Qt::PlainText);
    combo = new QComboBox;
    combo->setAccessibleName(T("Device"));  // a QComboBox reports its current text as the name: the purpose is the description
    const QString purpose = T("With several devices: show and measure only this device. \"All devices\" follows the one that moved last.");
    combo->setToolTip(purpose);
    combo->setAccessibleDescription(T("Device") + QStringLiteral(": ") + purpose);
    label->setBuddy(combo);
    h->addWidget(label);
    h->addWidget(combo, 1);
    w->setVisible(false);
    connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, combo](int i) {
        const QVariant v = combo->itemData(i);
        if (v.isValid()) session_->selectDevice(v.toInt());
        refreshDevices();  // the other combo follows; this one already shows the choice
    });
    return w;
}

// The combos are filled when the list of devices changes and only follow the selection otherwise: a rebuild on
// every poll or choice would close the open list and reset the keyboard focus. While a list is open nothing is
// touched; the next poll catches up.
void AxesPage::refreshDevices() {
    const auto &devs = session_->selectableDevices();
    std::vector<int> ids;
    for (const LiveDevice &d : devs) ids.push_back(d.id);
    const int sel = session_->selectedDevice();
    const bool listChanged = ids != devShown_ || !devFilled_;
    bool allDone = true;
    for (int k = 0; k < 2; k++) {
        QComboBox *c = devCombo_[k];
        if (c->view() && c->view()->isVisible()) {
            allDone = false;
            continue;
        }
        QSignalBlocker block(c);
        if (listChanged) {
            c->clear();
            c->addItem(T("All devices (the one that moved last)"), spn::ui::AxesSession::kAllDevices);
            for (const LiveDevice &d : devs) c->addItem(qs(truncateUtf8(d.name, 64)), int(d.id));
            ++devRebuilds_;
        }
        const int at = c->findData(sel);
        if (at >= 0 && c->currentIndex() != at) c->setCurrentIndex(at);
        else if (at < 0 && c->currentIndex() != 0) c->setCurrentIndex(0);
    }
    if (allDone) {
        devShown_ = ids;
        devFilled_ = true;
    }
    const bool show = devs.size() >= 2;
    devRow_[0]->setVisible(show);
    devRow_[1]->setVisible(show);
    devCombo_[1]->setEnabled(!session_->wizard().measuring());  // a step is measured on one device
}

void AxesPage::buildWizardPage(QWidget *page) {
    auto *l = new QVBoxLayout(page);
    wizHead_ = new QLabel;
    wizChips_ = new motionview::ChipFlow;
    wizChips_->setAccessibleName(T("The five motions of the assistant"));
    wizInstr_ = new QLabel;
    wizInstr_->setTextFormat(Qt::PlainText);
    wizInstr_->setWordWrap(true);
    QFont big = wizInstr_->font();
    big.setPointSizeF(big.pointSizeF() * 1.3);
    wizInstr_->setFont(big);
    wizArrow_ = new QLabel;
    wizArrow_->setTextFormat(Qt::PlainText);
    QFont arrow = wizArrow_->font();
    arrow.setPointSizeF(arrow.pointSizeF() * 1.6);
    arrow.setBold(true);
    wizArrow_->setFont(arrow);
    wizMeaning_ = new QLabel;
    wizMeaning_->setTextFormat(Qt::PlainText);
    wizMeaning_->setWordWrap(true);
    wizState_ = new QLabel;  // "Waiting", "Measuring", "Detected" as text, not only a bar
    wizState_->setWordWrap(true);
    wizState_->setTextFormat(Qt::PlainText);
    QFont st = wizState_->font();
    st.setBold(true);
    wizState_->setFont(st);
    wizMeasure_ = new QProgressBar;
    wizMeasure_->setRange(0, 1000);
    wizMeasure_->setTextVisible(false);
    wizMeasure_->setAccessibleName(T("Time left for this step"));
    devRow_[1] = makeDeviceRow(devCombo_[1], devLabel_[1]);
    wizBars_ = new LiveBarsWidget(&session_->view(), true, page);
    wizResult_ = new QLabel;
    wizResult_->setTextFormat(Qt::PlainText);
    wizResult_->setWordWrap(true);
    wizResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    wizResult_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    l->addWidget(wizHead_);
    l->addWidget(wizChips_);
    l->addWidget(wizInstr_);
    l->addWidget(wizArrow_);
    l->addWidget(wizMeaning_);
    l->addWidget(wizState_);
    l->addWidget(wizMeasure_);
    l->addWidget(devRow_[1]);
    l->addWidget(wizBars_);
    l->addWidget(wizResult_, 1);
    manualAdvance_ = new QCheckBox(T("Don't advance automatically"));
    manualAdvance_->setToolTip(T("Stay on a detected step until you press Next, and do not skip an optional step by itself. For slow or keyboard-only use."));
    l->addWidget(manualAdvance_);
    connect(manualAdvance_, &QCheckBox::toggled, this, [this](bool on) {
        spn::ui::AxisAssistantTuning t = session_->wizard().tuning();
        t.manual_advance = on;
        session_->wizard().setTuning(t);
        std::string err;
        // remembered with the other assistant settings in ui.conf; a file that cannot be read stays as it is
        if (!spn::ui::updateUiSettings(spn::ui::uiSettingsPath(), [on](spn::ui::UiSettings &u) { u.assistant.manual_advance = on; }, &err))
            ctx_.report(T("The setting could not be saved: %1").arg(qs(err)), true);
    });

    auto *row = new QHBoxLayout;
    bCancel_ = new QPushButton(T("Cancel"));
    bCancel_->setToolTip(T("Leave the assistant without changing anything (Esc)."));
    bBack_ = new QPushButton(T("Back"));
    bSkip_ = new QPushButton(T("Skip"));
    bStart_ = new QPushButton(T("Start now"));
    bStart_->setToolTip(T("Normally a step starts when you move the cap. This starts it at once."));
    bNext_ = new QPushButton(T("Next"));
    bNext_->setToolTip(T("Go on at once. Otherwise the next step starts by itself after a moment."));
    bApply_ = new QPushButton(T("Apply"));
    bApply_->setToolTip(T("Write the detected axis mixes into the Global profile (Enter). You can undo it afterwards."));
    bApply_->setDefault(true);
    row->addWidget(bCancel_);
    row->addStretch(1);
    for (QPushButton *b : {bBack_, bSkip_, bStart_, bNext_, bApply_}) row->addWidget(b);
    l->addLayout(row);
    QWidget::setTabOrder(bApply_, bNext_);
    QWidget::setTabOrder(bNext_, bStart_);
    QWidget::setTabOrder(bStart_, bSkip_);
    QWidget::setTabOrder(bSkip_, bBack_);
    QWidget::setTabOrder(bBack_, bCancel_);
    QWidget::setTabOrder(bCancel_, manualAdvance_);

    auto &w = session_->wizard();
    connect(bCancel_, &QPushButton::clicked, this, [this] { cancelWizard(); });
    connect(bBack_, &QPushButton::clicked, this, [this, &w] { w.back(); refreshWizard(now()); });
    connect(bSkip_, &QPushButton::clicked, this, [this, &w] { w.skip(); refreshWizard(now()); });
    connect(bNext_, &QPushButton::clicked, this, [this, &w] { w.next(); refreshWizard(now()); });
    connect(bStart_, &QPushButton::clicked, this, [this, &w] {
        // "Start now" before a movement, "Measure again" after a result (or a failed try)
        if (w.waiting() && !w.done(w.step())) w.start(now(), session_->connected(now()));
        else w.again(now());
        refreshWizard(now());
    });
    connect(bApply_, &QPushButton::clicked, this, [this] { applyWizard(); });

    // Keyboard only: Esc leaves without writing; Enter presses the focused button, or Apply on the
    // result page (a plain widget has no default button, so this is done by hand).
    auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), page);
    esc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, [this] { cancelWizard(); });
    for (const QKeySequence &k : {QKeySequence(Qt::Key_Return), QKeySequence(Qt::Key_Enter)}) {
        auto *enter = new QShortcut(k, page);
        enter->setContext(Qt::WidgetWithChildrenShortcut);
        connect(enter, &QShortcut::activated, this, [this] {
            QWidget *fw = QApplication::focusWidget();
            auto *f = qobject_cast<QPushButton *>(fw);
            if (f && f->isEnabled() && f->isVisible()) f->click();
            else if (qobject_cast<QComboBox *>(fw) || qobject_cast<QCheckBox *>(fw)) return;  // the device list and the check box keep Enter
            else if (session_->wizard().onResultPage()) applyWizard();
        });
    }
}

bool AxesPage::wizardVisible() const { return stack_->currentIndex() == 1; }

void AxesPage::openWizard() {
    auto &w = session_->wizard();
    w.reset();
    const spn::ui::AxisAssistantTuning tuning = spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).assistant;  // axis_assistant_* keys
    w.setTuning(tuning);
    {
        QSignalBlocker block(manualAdvance_);  // the saved choice, without writing it again
        manualAdvance_->setChecked(tuning.manual_advance);
    }
    wizWasResult_ = false;
    applyNote_.clear();
    if (ctx_.settings) {
        const spn::Settings &g = ctx_.settings->profiles().sectionSettings(0);
        w.setZoomLateral(g.zoom_lateral);
        // The lift/press axis: what the configuration leaves out of the channels, else the table of the platform.
        const spn::ui::AxisRoles roles = spn::ui::deriveAxisRoles(g);
        w.setLiftAxis(roles.matched && roles.has(spn::ui::Motion::Lift)
                          ? roles.axis(spn::ui::Motion::Lift)
                          : spn::ui::wizardFallbackLiftAxis(g, spn::seedOsName()));
    }
    savedNote_.clear();
    stack_->setCurrentIndex(1);
    refreshWizard(now());
}

void AxesPage::cancelWizard() {
    session_->wizard().reset();  // nothing was written; just leave
    stack_->setCurrentIndex(0);
}

void AxesPage::refreshPartsHint() {
    if (!partsHint_) return;
    const spn::Settings st = ctx_.settings ? ctx_.settings->effective(0) : spn::ui::freshSettings();
    const spn::ui::AxisRoles roles = spn::ui::deriveAxisRoles(st);
    partsHint_->setText(qs8(spn::ui::axesHint(roles, st, ctx_.strings ? *ctx_.strings : ownStrings_)));
    if (bars_) {  // "z: Push forward / back" where the configuration says what the axis is
        std::array<QString, AX_COUNT> words;
        const auto plain = spn::ui::axisPlainWords(roles);
        for (int i = 0; i < AX_COUNT; i++)
            if (plain[size_t(i)]) words[size_t(i)] = T(plain[size_t(i)]);
        bars_->setAxisWords(words);
    }
}

void AxesPage::applyWizard() {
    auto &w = session_->wizard();
    if (!w.canApply()) return;
    if (!ctx_.settings || !ctx_.strings) return;
    std::string err;
    const spn::ui::GlobalSnapshot before = spn::ui::snapshotGlobal(*ctx_.settings);
    dropUndo();  // the next Apply makes the offer of the last one obsolete
    if (!w.applyTo(*ctx_.settings, &err)) {
        applyNote_ = T("Error: %1").arg(qs(err));
        spn::ui::announce(this, applyNote_);
        refreshWizard(now());
        return;
    }
    refreshPartsHint();
    // The axis mixes are written now (not after a pause): the live page below shows their effect.
    SettingsModel::SaveResult r;
    if (ctx_.autosave) {
        ctx_.autosave->requestImmediate();
        ctx_.autosave->flush();
        r.saved = !ctx_.settings->dirty();
        r.reloaded = r.saved && !ctx_.autosave->failed();
        r.error = ctx_.autosave->statusText().toUtf8().toStdString();
    } else {
        r = ctx_.settings->save(*ctx_.strings);
    }
    if (!r.saved) {
        applyNote_ = T("Error: %1").arg(qs(r.error));
        spn::ui::announce(this, applyNote_);
        refreshWizard(now());
        return;
    }
    w.reset();
    stack_->setCurrentIndex(0);
    {  // the Overview's "Getting started" list counts the axes step as done
        std::string ignored;
        spn::ui::markAxisAssistantApplied(spn::ui::uiProgressPath(ctx_.configPath), &ignored);
    }
    // The live page shows the outcome; a reload failure is reported but the file is saved.
    savedNote_ = r.reloaded ? T("Axis mixes saved to the Global profile.")
                            : T("Axis mixes saved to the Global profile. %1").arg(qs(r.error));
    undo_.before = before;  // one click brings the old values back, as long as nothing else changes them
    undo_.after = spn::ui::snapshotGlobal(*ctx_.settings);
    undoBar_->setVisible(undo_.valid());
    spn::ui::announce(this, savedNote_ + QLatin1Char(' ') +
                                T("Undo with the Undo button or %1. Dismiss closes the bar.")
                                    .arg(QKeySequence(QKeySequence::Undo).toString(QKeySequence::NativeText)));
    refreshLive(now());
}

void AxesPage::dropUndo() {
    undo_ = spn::ui::AppliedUndo();
    if (undoBar_) undoBar_->hide();
}

void AxesPage::checkUndo() {
    if (undo_.valid() && ctx_.settings && !undo_.stillApplies(*ctx_.settings)) dropUndo();
}

// Puts the Global values back as they were before Apply (the same keys, written at once).
void AxesPage::undoWizard() {
    checkUndo();
    if (!undo_.valid() || !ctx_.settings) return;
    std::string err;
    if (!spn::ui::restoreGlobal(*ctx_.settings, undo_.before, &err)) {
        savedNote_ = T("Undo failed: %1").arg(qs(err));
        spn::ui::announce(this, savedNote_);
        refreshLive(now());
        return;
    }
    dropUndo();
    refreshPartsHint();
    if (ctx_.autosave) {
        ctx_.autosave->requestImmediate();
        ctx_.autosave->flush();
    } else if (ctx_.strings) {
        ctx_.settings->save(*ctx_.strings);
    }
    savedNote_ = T("The axis mixes of the Global profile are back as they were before.");
    spn::ui::announce(this, savedNote_);
    refreshLive(now());
}

void AxesPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    checkUndo();  // the Global values may have been edited on another page meanwhile
    timer_->start();  // polls the feed only while the page is visible
}

void AxesPage::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    timer_->stop();
    suspend_->stop();  // leaving the page, closing the window: output resumes at once
    if (session_->wizard().measuring()) session_->wizard().connectionLost();
}

// Pause while the page is visible in the active window (live bars) or the assistant is open.
bool AxesPage::wantsSuspend() const {
    if (!isVisible()) return false;
    return wizardVisible() || window()->isActiveWindow();
}

void AxesPage::pollOnce(int64_t nowNs) {
    suspend_->update(wantsSuspend(), nowNs);
    const bool finished = session_->poll(nowNs);
    refreshLive(nowNs);
    refreshDevices();
    if (wizardVisible() || finished) refreshWizard(nowNs);
    refreshParts();
    bars_->update();
    if (wizardVisible()) wizBars_->update();
}

// The push and tilt rows of the active joystick mode (as the daemon reports it); none while the daemon
// is not connected or the mode splits no channel.
void AxesPage::refreshParts() {
    std::vector<spn::ui::SplitPartRow> rows;
    const spn::ui::AxesViewModel &vm = session_->view();
    if (vm.hasData() && ctx_.settings) {
        const LiveSample &sample = vm.shown();
        rows = spn::ui::splitPartRows(ctx_.settings->modeDef(sample.mode), ctx_.settings->effective(0), sample.axes,
                                      ctx_.strings ? *ctx_.strings : ownStrings_);
    }
    parts_ = rows;
    bars_->setParts(std::move(rows));
}

void AxesPage::refreshLive(int64_t nowNs) {
    const ConnState cs = session_->connection(nowNs);
    connLine_->setText(T("Daemon:") + QLatin1Char(' ') + T(connectionText(cs)));
    connLine_->setVisible(!connLine_->text().isEmpty());
    QString text = cs == ConnState::Connected ? QString() : T(connectionHint(cs, currentHostOs()));
    if (!savedNote_.isEmpty()) text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + savedNote_;
    connHint_->setText(text);
    connHint_->setVisible(!text.isEmpty());
}

void AxesPage::refreshWizard(int64_t nowNs) {
    auto &w = session_->wizard();
    const bool result = w.onResultPage();
    const bool connected = session_->connected(nowNs);
    const int s = std::min(w.step(), int(wiz::ROLE_COUNT) - 1);
    if (result) {
        wizHead_->setText(QStringLiteral("<b>") + T("Result") + QStringLiteral("</b>"));
    } else {
        const QString step = T("Step %1 of %2: %3%4")
                                 .arg(w.step() + 1)
                                 .arg(int(wiz::ROLE_COUNT))
                                 .arg(T(wiz::roleTitle(w.step())),
                                      wiz::roleOptional(w.step()) ? T(" (optional)") : QString());
        wizHead_->setText(QStringLiteral("<b>") + step.toHtmlEscaped() + QStringLiteral("</b>"));
    }
    // The five motions: the current one solid, the others dashed, each with its state in words.
    {
        QList<motionview::Chip> chips;
        for (int r = 0; r < wiz::ROLE_COUNT; ++r) {
            motionview::Chip c;
            QString state = w.skipped(r) ? T("skipped") : (w.done(r) && w.stepResult(r).valid) ? T("done") : QString();
            c.text = T(wiz::roleTitle(r)) + (state.isEmpty() ? QString() : QStringLiteral(" - ") + state);
            c.muted = result || r != w.step();
            chips.append(c);
        }
        wizChips_->setChips(chips);
        wizChips_->setVisible(true);
    }
    for (QWidget *x : {static_cast<QWidget *>(wizInstr_), static_cast<QWidget *>(wizMeaning_),
                       static_cast<QWidget *>(wizState_), static_cast<QWidget *>(wizBars_)})
        x->setVisible(!result);
    wizArrow_->setVisible(!result);
    wizMeasure_->setVisible(!result && w.measuring());

    QString msg;
    if (result) {
        const wiz::WizardResult &f = w.final();
        msg = T("Detected mapping:");
        for (const spn::ui::WizardMappingRow &r : spn::ui::wizardMappingRows(w))
            msg += QStringLiteral("\n    ") + qs(r.direction) + QStringLiteral(": ") + qs(r.detail);
        if (f.ok) {
            msg += QStringLiteral("\n\n") + T("Apply changes this in the Global profile:");
            if (ctx_.settings) {
                for (const spn::ui::WizardChangeRow &row : spn::ui::wizardChangeRows(*ctx_.settings, w))
                    msg += QStringLiteral("\n    ") +
                           (row.changed() ? T("%1: now %2, new %3").arg(T(row.label), qs(row.now), qs(row.next))
                                          : T("%1: %2 (unchanged)").arg(T(row.label), qs(row.next)));
            } else {
                msg += QStringLiteral("\n    ") + T("Scroll V") + QStringLiteral(" = ") + qs(f.mix_v) + QStringLiteral("\n    ") +
                       T("Scroll H") + QStringLiteral(" = ") + qs(f.mix_h) + QStringLiteral("\n    ") + T("Zoom") +
                       QStringLiteral(" = ") + qs(f.mix_zoom);
            }
            if (ctx_.settings) {
                const std::vector<std::string> kept = spn::ui::wizardProfilesKeepingDirections(*ctx_.settings);
                if (!kept.empty()) {
                    QStringList names;
                    for (size_t i = 0; i < kept.size() && i < 5; ++i) names << qs8(kept[i]);
                    if (kept.size() > 5) names << T("... and %1 more").arg(int(kept.size() - 5));
                    msg += QStringLiteral("\n\n") +
                           T("Device and program profiles with their own direction settings (inverted axes or reversed "
                             "directions) keep them: %1").arg(names.join(QStringLiteral(", ")));
                }
            }
            msg += QStringLiteral("\n\n") +
                   T("\"Apply\" sets these axis mixes in the Global profile and resets the direction "
                     "switches and the inverted axes to normal. You can undo it afterwards. \"Cancel\" changes nothing.");
        } else {
            msg += QStringLiteral("\n\n") + T("The mapping is incomplete and cannot be applied.");
        }
        for (const auto &e : f.errors) msg += QStringLiteral("\n\n") + T("Error: %1").arg(qs(e));
        for (const auto &x : f.warnings) msg += QStringLiteral("\n\n") + T("Warning: %1").arg(qs(x));
        if (!applyNote_.isEmpty()) msg += QStringLiteral("\n\n") + applyNote_;
    } else {
        wizInstr_->setText(T(wizardStepText(s).instruction));
        wizMeaning_->setText(T(wizardStepText(s).meaning));
        const spn::ui::WizardFirstMove &fm = spn::ui::wizardFirstMove(s);
        const bool picked = w.measuring() && w.halves() >= 1;
        wizArrow_->setText(qs8(fm.arrow) + QStringLiteral("  ") + T("First: %1").arg(T(fm.words)) +
                           (picked ? QStringLiteral("  (") + T("picked up") + QLatin1Char(')') : QString()));
        wizState_->setText(qs(spn::ui::wizardStateText(w, connected)));
        wizMeasure_->setValue(int(w.progress(nowNs) * 1000.0));
        if (!w.notice().empty()) msg = qs(w.notice());
        if (!w.note().empty()) msg += (msg.isEmpty() ? QString() : QStringLiteral("\n\n")) + qs(w.note());
        if (w.done(s) && !w.stepResult(s).valid)
            msg += (msg.isEmpty() ? QString() : QStringLiteral("\n\n")) + T(spn::ui::wizardStepHint(w.stepResult(s))) +
                   QStringLiteral(" ") + T("Move the cap again to repeat this step.");
        else if (w.done(s) && w.stepResult(s).valid) {
            msg += (msg.isEmpty() ? QString() : QStringLiteral("\n\n")) +
                   T("Detected: %1").arg(T(wiz::roleTitle(s)));
            if (w.stepResult(s).ratio > wiz::AMBIGUOUS_RATIO)
                msg += QStringLiteral(" ") + T("Close call - repeat if necessary and move more cleanly in only one direction.");
        } else if (w.skipped(s) && w.notice().empty())
            msg += (msg.isEmpty() ? QString() : QStringLiteral("\n\n")) + T("Skipped.");
        if (!connected && !w.measuring())
            msg += (msg.isEmpty() ? QString() : QStringLiteral("\n\n")) +
                   T(connectionHint(session_->connection(nowNs), currentHostOs()));
    }
    wizResult_->setText(msg);

    bStart_->setVisible(!result);
    const bool freshWait = w.waiting() && !w.done(s);
    bStart_->setText(freshWait ? T("Start now") : T("Measure again"));
    bStart_->setEnabled(freshWait ? w.canStart(connected) : w.canAgain());
    bSkip_->setVisible(!result && wiz::roleOptional(w.step()));
    bSkip_->setEnabled(w.canSkip());
    bBack_->setEnabled(w.canBack());
    bNext_->setVisible(!result);
    bNext_->setEnabled(w.canNext());
    bApply_->setVisible(result);
    bApply_->setEnabled(w.canApply());
    if (result && !wizWasResult_ && bApply_->isEnabled()) bApply_->setFocus();  // Enter applies
    wizWasResult_ = result;
}

QWidget *createAxesPage(const UiContext &ctx) { return new AxesPage(ctx); }

// ---- self-test -----------------------------------------------------------------------------------

namespace {
struct SelfTestConn : FeedConn {
    std::deque<uint8_t> in;
    long read(uint8_t *b, size_t n) override {
        size_t k = 0;
        while (k < n && !in.empty()) {
            b[k++] = in.front();
            in.pop_front();
        }
        return static_cast<long>(k);
    }
    long write(const uint8_t *, size_t) override { return 0; }
};
}  // namespace

int axesPageSelfTest() {
    SelfTestConn *conn = nullptr;
    SettingsModel settings;
    settings.setProfiles(ProfileSet(), "axes-selftest-unused.conf");
    {   // a joystick mode that splits the vertical channel (push scrolls, tilt does nothing)
        ModeSet ms;
        ModeDef m;
        m.id = "splitm";
        m.label = "Splitm";
        m.vSplit.pushSet = true;
        m.vSplit.push.type = ChannelActionType::WheelV;
        m.vSplit.tiltSet = true;
        m.vSplit.tilt.type = ChannelActionType::Off;
        ms.modes.push_back(m);
        settings.setModes(ms, std::string());
    }
    UiContext ctx;
    ctx.settings = &settings;
    AxesPageEnv env;
    env.connector = [&conn](std::string *) -> std::unique_ptr<FeedConn> {
        auto c = std::make_unique<SelfTestConn>();
        conn = c.get();
        return c;
    };
    AxesPage page(ctx, env);
    page.resize(560, 520);
    page.show();
    int64_t t = 0;
    page.pollOnce(t);  // connects
    if (!conn) return 1;
    for (int i = 0; i < 5; i++) {
        LiveSample s;
        s.axes = RawAxes{10 * i, -20, 300, 0, 0, 5};
        s.buttons = 3;
        s.v = 12.5f;
        s.mode = "normal";
        for (uint8_t b : encodeLiveSample(s)) conn->in.push_back(b);
        t += 33000000;
        page.pollOnce(t);
    }
    const bool connected = page.session().connected(t);
    const bool data = page.session().view().shown().axes[AX_Z] == 300 && page.session().view().buttonCount() == 2;
    const bool noParts = page.partRows().empty();  // "normal" splits no channel
    page.grab();  // renders the custom widget offscreen
    // the active mode splits V: push (z) and tilt (rx) rows with the live values, before the wizard
    bool parts = false;
    {
        LiveSample s;
        s.axes = RawAxes{0, 0, 120, 0, 0, 0};
        s.axes[AX_RX] = -40;
        s.mode = "splitm";
        for (uint8_t b : encodeLiveSample(s)) conn->in.push_back(b);
        t += 33000000;
        page.pollOnce(t);
        const auto &rows = page.partRows();
        parts = rows.size() == 2 && rows[0].id == "v_push" && rows[0].value == 120 && rows[1].id == "v_tilt" && rows[1].value == -40;
        page.grab();
    }
    page.openWizard();
    page.pollOnce(t += 33000000);
    const bool wizOk = page.wizardVisible() && page.session().wizard().canStart(page.session().connected(t));
    page.grab();
    page.cancelWizard();
    const bool left = !page.wizardVisible();
    std::printf("axes-page self-test: connected=%d data=%d parts=%d/%d wizard=%d cancel=%d\n", connected, data, noParts, parts, wizOk, left);
    return connected && data && noParts && parts && wizOk && left ? 0 : 1;
}

bool AxesPage::deviceRowVisible() const { return devRow_[0]->isVisibleTo(this); }
QString AxesPage::resultText() const { return wizResult_->text(); }
