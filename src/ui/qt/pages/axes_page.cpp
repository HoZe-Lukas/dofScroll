// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "axes_page.hpp"

#include <QApplication>
#include <QComboBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QProgressBar>
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

#include "axis_convention.hpp"
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
        QString desc;
        for (const auto &r : parts_)
            desc += qs8(r.label) + QStringLiteral(": ") + qs8(r.action) + QLatin1Char('\n');
        setAccessibleDescription(desc.trimmed());
        if (resize) updateGeometry();
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
}

AxesPage::~AxesPage() {
    suspend_->stop();
    if (wiz::trHook() == &wizardTranslate) wiz::trHook() = [](const char *s) { return s; };
}

void AxesPage::buildLivePage(QWidget *page) {
    auto *l = new QVBoxLayout(page);
    auto *title = new QLabel(QStringLiteral("<b>") + T("Axes / assistant") + QStringLiteral("</b>"));
    l->addWidget(title);
    connLine_ = new QLabel;
    connHint_ = new QLabel;
    connHint_->setWordWrap(true);
    connHint_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->addWidget(connLine_);
    l->addWidget(connHint_);
    devRow_[0] = makeDeviceRow(devCombo_[0], devLabel_[0]);
    l->addWidget(devRow_[0]);
    bars_ = new LiveBarsWidget(&session_->view(), false, page);
    l->addWidget(bars_, 1);
    auto *parts = new QLabel;  // filled by refreshPartsHint(): the axes of the shown configuration (issue #186)
    partsHint_ = parts;
    refreshPartsHint();
    parts->setWordWrap(true);
    parts->setAccessibleName(T("Which movement drives which part"));
    l->addWidget(parts);

    auto *row = new QHBoxLayout;
    pause_ = new QPushButton(T("Pause"));
    pause_->setCheckable(true);
    pause_->setToolTip(T("Freeze the display to read the values."));
    auto *reset = new QPushButton(T("Reset peaks"));
    reset->setToolTip(T("Clear the peak markers and the bar scale."));
    openWiz_ = new QPushButton(T("Axis assistant..."));
    openWiz_->setToolTip(T("Learn which axis does what by moving the cap, step by step."));
    row->addWidget(pause_);
    row->addWidget(reset);
    row->addStretch(1);
    row->addWidget(openWiz_);
    l->addLayout(row);
    QWidget::setTabOrder(pause_, reset);
    QWidget::setTabOrder(reset, openWiz_);

    connect(pause_, &QPushButton::toggled, this, [this](bool on) { session_->view().setPaused(on); });
    connect(reset, &QPushButton::clicked, this, [this] { session_->view().resetPeaks(); });
    connect(openWiz_, &QPushButton::clicked, this, [this] { openWizard(); });
}

// "Device: [All devices | A | B]": pins the page and the assistant to one device (issue #139). Hidden
// until the daemon reports two or more devices.
QWidget *AxesPage::makeDeviceRow(QComboBox *&combo, QLabel *&label) {
    auto *w = new QWidget;
    auto *h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    label = new QLabel(T("Device:"));
    combo = new QComboBox;
    combo->setAccessibleName(T("Device"));
    combo->setToolTip(T("With several devices: show and measure only this device. \"All devices\" follows the one that moved last."));
    label->setBuddy(combo);
    h->addWidget(label);
    h->addWidget(combo, 1);
    w->setVisible(false);
    connect(combo, QOverload<int>::of(&QComboBox::activated), this, [this, combo](int i) {
        const QVariant v = combo->itemData(i);
        if (v.isValid()) session_->selectDevice(v.toInt());
        devSelShown_ = -2;  // refresh both combos
        refreshDevices();
    });
    return w;
}

void AxesPage::refreshDevices() {
    const auto &devs = session_->selectableDevices();
    std::vector<int> ids;
    for (const LiveDevice &d : devs) ids.push_back(d.id);
    const int sel = session_->selectedDevice();
    if (ids != devShown_ || sel != devSelShown_) {
        devShown_ = ids;
        devSelShown_ = sel;
        for (int k = 0; k < 2; k++) {
            QSignalBlocker block(devCombo_[k]);
            devCombo_[k]->clear();
            devCombo_[k]->addItem(T("All devices (the one that moved last)"), spn::ui::AxesSession::kAllDevices);
            int cur = 0;
            for (const LiveDevice &d : devs) {
                devCombo_[k]->addItem(qs(truncateUtf8(d.name, 64)), int(d.id));
                if (d.id == sel) cur = devCombo_[k]->count() - 1;
            }
            devCombo_[k]->setCurrentIndex(cur);
        }
    }
    const bool show = !devs.empty();
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
    wizInstr_->setWordWrap(true);
    QFont big = wizInstr_->font();
    big.setPointSizeF(big.pointSizeF() * 1.3);
    wizInstr_->setFont(big);
    wizArrow_ = new QLabel;
    QFont arrow = wizArrow_->font();
    arrow.setPointSizeF(arrow.pointSizeF() * 1.6);
    arrow.setBold(true);
    wizArrow_->setFont(arrow);
    wizMeaning_ = new QLabel;
    wizMeaning_->setWordWrap(true);
    wizState_ = new QLabel;  // "Waiting", "Measuring", "Detected" as text, not only a bar
    wizState_->setWordWrap(true);
    QFont st = wizState_->font();
    st.setBold(true);
    wizState_->setFont(st);
    wizState_->setAccessibleName(T("State of the step"));
    wizMeasure_ = new QProgressBar;
    wizMeasure_->setRange(0, 1000);
    wizMeasure_->setTextVisible(false);
    wizMeasure_->setAccessibleName(T("Time left for this step"));
    devRow_[1] = makeDeviceRow(devCombo_[1], devLabel_[1]);
    wizBars_ = new LiveBarsWidget(&session_->view(), true, page);
    wizResult_ = new QLabel;
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
    bApply_->setToolTip(T("Write the detected axis mixes into the [global] settings (Enter)."));
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
            auto *f = qobject_cast<QPushButton *>(QApplication::focusWidget());
            if (f && f->isEnabled() && f->isVisible()) f->click();
            else if (session_->wizard().onResultPage()) applyWizard();
        });
    }
}

bool AxesPage::wizardVisible() const { return stack_->currentIndex() == 1; }

void AxesPage::openWizard() {
    auto &w = session_->wizard();
    w.reset();
    w.setTuning(spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).assistant);  // axis_assistant_* keys, no UI for them
    wizWasResult_ = false;
    applyNote_.clear();
    if (ctx_.settings) {
        const spn::Settings &g = ctx_.settings->profiles().sectionSettings(0);
        w.setZoomLateral(g.zoom_lateral);
        // The lift/press axis: what the configuration leaves out of the channels, else the table of the platform.
        const spn::ui::AxisRoles roles = spn::ui::deriveAxisRoles(g);
        w.setLiftAxis(roles.matched && roles.has(spn::ui::Motion::Lift)
                          ? roles.axis(spn::ui::Motion::Lift)
                          : spn::conventionLiftAxis(spn::conventionFor(spn::seedOsName(), "")));
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
    partsHint_->setText(qs8(spn::ui::axesHint(spn::ui::deriveAxisRoles(st), st, ctx_.strings ? *ctx_.strings : ownStrings_)));
}

void AxesPage::applyWizard() {
    auto &w = session_->wizard();
    if (!w.canApply()) return;
    if (!ctx_.settings || !ctx_.strings) return;
    std::string err;
    if (!w.applyTo(*ctx_.settings, &err)) {
        applyNote_ = T("Error: %1").arg(qs(err));
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
    savedNote_ = r.reloaded ? T("Axis mixes saved to [global].")
                            : T("Axis mixes saved to [global]. %1").arg(qs(r.error));
    refreshLive(now());
}

void AxesPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
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
            msg += QStringLiteral("\n\n") + T("This will be written:") + QStringLiteral("\n    ") + T("Scroll V") +
                   QStringLiteral(" = ") + qs(f.mix_v) + QStringLiteral("\n    ") + T("Scroll H") +
                   QStringLiteral(" = ") + qs(f.mix_h) + QStringLiteral("\n    ") + T("Zoom") + QStringLiteral(" = ") +
                   qs(f.mix_zoom) + QStringLiteral("\n\n") +
                   T("\"Apply\" sets these axis mixes in the [global] settings and resets the direction "
                     "switches to normal. \"Cancel\" changes nothing.");
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
