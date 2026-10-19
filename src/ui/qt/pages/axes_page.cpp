// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "axes_page.hpp"

#include <QApplication>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
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

#include "config.hpp"
#include "ui/model/settings_model.hpp"
#include "ui_i18n.hpp"
#if defined(__APPLE__)
#include "platform/mac/mac_paths.hpp"
#endif

using namespace spn;
using namespace spn::ui;

namespace {

// ---- helpers --------------------------------------------------------------------------------

// Directory of the live feed socket (the pipe name on Windows carries the user SID, no directory).
std::string feedRuntimeDir() {
#if defined(_WIN32)
    return std::string();
#elif defined(__APPLE__)
    const char *home = std::getenv("HOME");
    return macPaths(home ? home : "").runtimeDir;
#else
    const char *rt = std::getenv("XDG_RUNTIME_DIR");
    return rt && *rt ? rt : "";
#endif
}

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
        const int rows = AX_COUNT + (compact_ ? 0 : 3 + 3 + 2);
        return QSize(fontMetrics().horizontalAdvance(QStringLiteral("0")) * 36, rows * rh);
    }
    QSize sizeHint() const override { return minimumSizeHint() + QSize(120, 40); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QPalette &pal = palette();
        const int rh = rowHeight();
        const QFontMetrics fm = fontMetrics();
        const int labelW = fm.horizontalAdvance(QStringLiteral("Scroll H")) + 12;
        const int valueW = fm.horizontalAdvance(QStringLiteral("-00000")) + 8;
        const int barX = labelW, barW = std::max(40, width() - labelW - valueW - 4);
        const bool live = vm_->hasData();
        const bool paused = vm_->paused();
        const QColor track = pal.color(QPalette::Mid);
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
            p.drawRoundedRect(r, 3, 3);
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

        y += rh / 2;
        // Button indicators 1..N (filled = pressed), wrapped.
        const int cell = rh + 2, count = vm_->buttonCount();
        const int perRow = std::max(1, (width() - 4) / cell);
        for (int b = 1; b <= count; b++) {
            const int col = (b - 1) % perRow, rowNo = (b - 1) / perRow;
            const QRect r(2 + col * cell, y + rowNo * cell, rh, rh);
            const bool down = vm_->buttonDown(b);
            p.setPen(QPen(track, 1));
            p.setBrush(down ? pal.color(QPalette::Highlight) : Qt::NoBrush);
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
        status_ += T("Mode:") + QLatin1Char(' ') + (live ? qs(truncateUtf8(s.mode, 64)) : QStringLiteral("-"));
        if (live) {
            if (!s.enabled) status_ += QStringLiteral("  |  ") + T("disabled");
            if (s.nav) status_ += QStringLiteral("  |  ") + T("navigation mode");
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
    QString axisLabel_[AX_COUNT], chanLabel_[3];
    QString buf_, status_;  // reused per frame
};

// ---- the page ---------------------------------------------------------------------------------

AxesPage::AxesPage(const UiContext &ctx, const AxesPageEnv &env, QWidget *parent)
    : QWidget(parent), ctx_(ctx), connector_(env.connector) {
    wiz::trHook() = &wizardTranslate;  // wizard messages are built toolkit-free
    if (!connector_) {
        const std::string dir = ctx_.runtimeDir.empty() ? feedRuntimeDir() : ctx_.runtimeDir;
        connector_ = [dir](std::string *err) { return connectFeed(dir, err); };
    }
    client_ = std::make_unique<LiveFeedClient>(connector_);
    session_ = std::make_unique<AxesSession>(*client_);
    clock_.start();

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
    bars_ = new LiveBarsWidget(&session_->view(), false, page);
    l->addWidget(bars_, 1);

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

void AxesPage::buildWizardPage(QWidget *page) {
    auto *l = new QVBoxLayout(page);
    wizHead_ = new QLabel;
    wizSteps_ = new QProgressBar;
    wizSteps_->setRange(0, wiz::ROLE_COUNT);
    wizSteps_->setTextVisible(false);
    wizInstr_ = new QLabel;
    wizInstr_->setWordWrap(true);
    QFont big = wizInstr_->font();
    big.setPointSizeF(big.pointSizeF() * 1.2);
    wizInstr_->setFont(big);
    wizMeaning_ = new QLabel;
    wizMeaning_->setWordWrap(true);
    wizMeasure_ = new QProgressBar;
    wizMeasure_->setRange(0, 1000);
    wizBars_ = new LiveBarsWidget(&session_->view(), true, page);
    wizResult_ = new QLabel;
    wizResult_->setWordWrap(true);
    wizResult_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    wizResult_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    l->addWidget(wizHead_);
    l->addWidget(wizSteps_);
    l->addWidget(wizInstr_);
    l->addWidget(wizMeaning_);
    l->addWidget(wizMeasure_);
    l->addWidget(wizBars_);
    l->addWidget(wizResult_, 1);

    auto *row = new QHBoxLayout;
    bCancel_ = new QPushButton(T("Cancel"));
    bCancel_->setToolTip(T("Leave the assistant without changing anything."));
    bBack_ = new QPushButton(T("Back"));
    bSkip_ = new QPushButton(T("Skip"));
    bStart_ = new QPushButton(T("Start"));
    bNext_ = new QPushButton(T("Next"));
    bApply_ = new QPushButton(T("Apply"));
    bApply_->setToolTip(T("Write the detected axis mixes into the [global] settings."));
    row->addWidget(bCancel_);
    row->addStretch(1);
    for (QPushButton *b : {bBack_, bSkip_, bStart_, bNext_, bApply_}) row->addWidget(b);
    l->addLayout(row);
    QWidget::setTabOrder(bStart_, bNext_);
    QWidget::setTabOrder(bNext_, bApply_);
    QWidget::setTabOrder(bApply_, bBack_);
    QWidget::setTabOrder(bBack_, bSkip_);
    QWidget::setTabOrder(bSkip_, bCancel_);

    auto &w = session_->wizard();
    connect(bCancel_, &QPushButton::clicked, this, [this] { cancelWizard(); });
    connect(bBack_, &QPushButton::clicked, this, [this, &w] { w.back(); refreshWizard(now()); });
    connect(bSkip_, &QPushButton::clicked, this, [this, &w] { w.skip(); refreshWizard(now()); });
    connect(bNext_, &QPushButton::clicked, this, [this, &w] { w.next(); refreshWizard(now()); });
    connect(bStart_, &QPushButton::clicked, this, [this, &w] {
        w.start(now(), session_->connected(now()));
        refreshWizard(now());
    });
    connect(bApply_, &QPushButton::clicked, this, [this] { applyWizard(); });
}

bool AxesPage::wizardVisible() const { return stack_->currentIndex() == 1; }

void AxesPage::openWizard() {
    auto &w = session_->wizard();
    w.reset();
    applyNote_.clear();
    if (ctx_.settings) w.setZoomLateral(ctx_.settings->profiles().sectionSettings(0).zoom_lateral);
    savedNote_.clear();
    stack_->setCurrentIndex(1);
    refreshWizard(now());
}

void AxesPage::cancelWizard() {
    session_->wizard().reset();  // nothing was written; just leave
    stack_->setCurrentIndex(0);
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
    const SettingsModel::SaveResult r = ctx_.settings->save(*ctx_.strings);
    if (!r.saved) {
        applyNote_ = T("Error: %1").arg(qs(r.error));
        refreshWizard(now());
        return;
    }
    w.reset();
    stack_->setCurrentIndex(0);
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
    if (session_->wizard().measuring()) session_->wizard().connectionLost();
}

void AxesPage::pollOnce(int64_t nowNs) {
    const bool finished = session_->poll(nowNs);
    refreshLive(nowNs);
    if (wizardVisible() || finished) refreshWizard(nowNs);
    bars_->update();
    if (wizardVisible()) wizBars_->update();
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
    wizSteps_->setValue(w.step());
    for (QWidget *x : {static_cast<QWidget *>(wizInstr_), static_cast<QWidget *>(wizMeaning_),
                       static_cast<QWidget *>(wizMeasure_), static_cast<QWidget *>(wizBars_)})
        x->setVisible(!result);

    QString msg;
    if (result) {
        const wiz::WizardResult &f = w.final();
        if (f.ok) {
            msg = T("Detected mapping:") + QStringLiteral("\n    ") + T("Scroll V") + QStringLiteral(" = ") +
                  qs(f.mix_v) + QStringLiteral("\n    ") + T("Scroll H") + QStringLiteral(" = ") + qs(f.mix_h) +
                  QStringLiteral("\n    ") + T("Zoom") + QStringLiteral(" = ") + qs(f.mix_zoom) +
                  QStringLiteral("\n\n") +
                  T("\"Apply\" sets these axis mixes in the [global] settings and resets the direction "
                    "switches to normal. \"Cancel\" changes nothing.");
        } else {
            msg = T("The mapping is incomplete and cannot be applied.");
        }
        for (const auto &e : f.errors) msg += QStringLiteral("\n\n") + T("Error: %1").arg(qs(e));
        for (const auto &x : f.warnings) msg += QStringLiteral("\n\n") + T("Warning: %1").arg(qs(x));
        if (!applyNote_.isEmpty()) msg += QStringLiteral("\n\n") + applyNote_;
    } else {
        wizInstr_->setText(T(wizardStepText(s).instruction));
        wizMeaning_->setText(T(wizardStepText(s).meaning));
        wizMeasure_->setValue(int(w.progress(nowNs) * 1000.0));
        wizMeasure_->setFormat(w.measuring()
                                   ? T("Measuring - move now! %1 s left").arg(w.secondsLeft(nowNs), 0, 'f', 1)
                               : w.done(s) ? T("Measurement complete")
                               : session_->connected(nowNs) ? T("Ready - click \"Start\", then move (3 s)")
                                                            : T("No connection to the daemon"));
        wizMeasure_->setTextVisible(true);
        if (!w.note().empty()) msg = qs(w.note());
        else if (w.done(s)) msg = qs(w.stepResult(s).message);
        else if (w.skipped(s)) msg = T("Skipped.");
        if (!session_->connected(nowNs) && !w.measuring())
            msg += (msg.isEmpty() ? QString() : QStringLiteral("\n\n")) +
                   T(connectionHint(session_->connection(nowNs), currentHostOs()));
    }
    wizResult_->setText(msg);

    bStart_->setVisible(!result);
    bStart_->setText(w.done(s) ? T("Measure again") : T("Start"));
    bStart_->setEnabled(w.canStart(session_->connected(nowNs)));
    bSkip_->setVisible(!result && wiz::roleOptional(w.step()));
    bSkip_->setEnabled(w.canSkip());
    bBack_->setEnabled(w.canBack());
    bNext_->setVisible(!result);
    bNext_->setEnabled(w.canNext());
    bApply_->setVisible(result);
    bApply_->setEnabled(w.canApply());
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
    page.grab();  // renders the custom widget offscreen
    page.openWizard();
    page.pollOnce(t += 33000000);
    const bool wizOk = page.wizardVisible() && page.session().wizard().canStart(page.session().connected(t));
    page.grab();
    page.cancelWizard();
    const bool left = !page.wizardVisible();
    std::printf("axes-page self-test: connected=%d data=%d wizard=%d cancel=%d\n", connected, data, wizOk, left);
    return connected && data && wizOk && left ? 0 : 1;
}
