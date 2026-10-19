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
#include "ui/model/axes_model.hpp"

class QLabel;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QTimer;
class LiveBarsWidget;

// Where the page gets its data from and writes to. Empty fields mean: the real connection to the
// daemon, the default config path, and no reload request after saving.
struct AxesPageEnv {
    spn::LiveFeedClient::Connector connector;   // empty: connectFeed(runtime dir)
    std::string configPath;                     // empty: spn::defaultConfigPath()
    std::function<bool(std::string *)> reload;  // empty: the daemon is not notified
};

class AxesPage : public QWidget {
    Q_OBJECT
public:
    explicit AxesPage(const AxesPageEnv &env = {}, QWidget *parent = nullptr);
    ~AxesPage() override;

    // One 30 Hz tick with an explicit clock (the timer calls it with the real one; the self-test
    // drives it directly).
    void pollOnce(int64_t nowNs);
    spn::ui::AxesSession &session() { return *session_; }
    bool wizardVisible() const;
    void openWizard();
    void cancelWizard();

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void buildLivePage(QWidget *page);
    void buildWizardPage(QWidget *page);
    void refreshLive(int64_t nowNs);
    void refreshWizard(int64_t nowNs);
    void applyWizard();
    int64_t now() const { return clock_.nsecsElapsed(); }

    AxesPageEnv env_;
    QElapsedTimer clock_;
    std::unique_ptr<spn::LiveFeedClient> client_;
    std::unique_ptr<spn::ui::AxesSession> session_;
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

QWidget *createAxesPage();

// Offscreen self-test: constructs the page with a fake feed, polls, renders and opens the wizard.
// Returns 0 on success (used by `dofscroll-ui --self-test`).
int axesPageSelfTest();
