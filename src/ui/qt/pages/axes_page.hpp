// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Axes / assistant" page: live bars of the six axes, buttons, channel outputs and the active mode
// from the daemon's live feed, plus the axis assistant (step-by-step wizard). A thin view on
// src/ui/model/axes_model.hpp; the only data source is the live feed (all three OSes).
#pragma once
#include <QElapsedTimer>
#include <QString>
#include <QWidget>
#include <functional>
#include <memory>
#include <string>

#include "live_feed/live_feed.hpp"
#include "qt_action_runner.hpp"
#include "ui_context.hpp"
#include "ui/model/axes_model.hpp"
#include "ui/model/split_parts_model.hpp"
#include "ui/model/suspend_heartbeat.hpp"

class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class LiveBarsWidget;

// Optional override of the live feed connection (tests, self-test); empty = the real daemon.
struct AxesPageEnv {
    spn::LiveFeedClient::Connector connector;
};

class AxesPage : public QWidget {
    Q_OBJECT
public:
    explicit AxesPage(const UiContext &ctx, const AxesPageEnv &env = {}, QWidget *parent = nullptr);
    ~AxesPage() override;

    // One 30 Hz tick with an explicit clock (the timer calls it with the real one; the self-test
    // drives it directly).
    void pollOnce(int64_t nowNs);
    spn::ui::AxesSession &session() { return *session_; }
    // The push/tilt rows now shown for the active joystick mode (empty if it splits no channel).
    const std::vector<spn::ui::SplitPartRow> &partRows() const { return parts_; }
    bool wizardVisible() const;
    bool wantsSuspend() const;
    const spn::ui::SuspendHeartbeat &suspend() const { return *suspend_; }
    void openWizard();
    void cancelWizard();

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void buildLivePage(QWidget *page);
    void buildWizardPage(QWidget *page);
    void refreshLive(int64_t nowNs);
    void refreshParts();
    void refreshWizard(int64_t nowNs);
    void applyWizard();
    int64_t now() const { return clock_.nsecsElapsed(); }

    UiContext ctx_;
    QtStrings ownStrings_;  // used when the context has no shared provider (standalone page, tests)
    spn::LiveFeedClient::Connector connector_;
    QElapsedTimer clock_;
    std::unique_ptr<spn::LiveFeedClient> client_;
    std::unique_ptr<spn::ui::AxesSession> session_;
    std::unique_ptr<spn::ui::SuspendHeartbeat> suspend_;
    std::vector<spn::ui::SplitPartRow> parts_;
    QTimer *timer_ = nullptr;
    QStackedWidget *stack_ = nullptr;

    QLabel *connLine_ = nullptr, *connHint_ = nullptr;
    LiveBarsWidget *bars_ = nullptr, *wizBars_ = nullptr;
    QPushButton *pause_ = nullptr, *openWiz_ = nullptr;

    QLabel *wizHead_ = nullptr, *wizInstr_ = nullptr, *wizMeaning_ = nullptr, *wizResult_ = nullptr;
    QProgressBar *wizSteps_ = nullptr, *wizMeasure_ = nullptr;
    QPushButton *bStart_ = nullptr, *bSkip_ = nullptr, *bBack_ = nullptr, *bNext_ = nullptr,
                *bApply_ = nullptr, *bCancel_ = nullptr;
    QString applyNote_, savedNote_;
};

QWidget *createAxesPage(const UiContext &ctx);

// Offscreen self-test: constructs the page with a fake feed, polls, renders and opens the wizard.
// Returns 0 on success (used by `dofscroll-ui --self-test`).
int axesPageSelfTest();  // uses a private temporary config
