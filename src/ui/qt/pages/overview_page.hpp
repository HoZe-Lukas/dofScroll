// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Overview" page, the first thing the window shows: the state of the daemon, what is active now,
// quick actions and one card per device or program profile. The data comes from
// src/ui/model/overview_model.hpp; the page only draws it and reports clicks.
#pragma once
#include <QEvent>
#include <QString>
#include <QWidget>

#include <vector>

#include "ui/model/overview_model.hpp"

#include "ui_context.hpp"

class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;
class QVBoxLayout;

class OverviewPage : public QWidget {
    Q_OBJECT
public:
    explicit OverviewPage(const UiContext &ctx, QWidget *parent = nullptr);
    int profileCardCount() const { return static_cast<int>(cards_.size()); }
    int cardColumns() const { return columns_; }
    // True if every card is at least as high as its content needs (no squashed or clipped cards).
    bool cardsFit() const;
    // True if the first row of cards reaches the right edge of the card area (the columns share the width).
    bool cardsFillWidth() const;
    // Reads the models again (status line, cards). Called every second and when the page is shown.
    void refresh();

signals:
    void openProfile(int section);  // a card was clicked: show this profile on the Profiles page
    void openPage(const QString &pageId);

protected:
    bool eventFilter(QObject *watched, QEvent *e) override;  // the card area got another width: wrap the cards again
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    void rebuildGettingStarted(const std::vector<spn::ui::GettingStartedStep> &steps);
    void rebuildCards();
    void layoutCards();
    void runAction(int index);

    const UiContext &ctx_;
    QLabel *statusLine_ = nullptr, *infoLine_ = nullptr, *warningLine_ = nullptr;
    QLabel *actionMsg_ = nullptr, *profilesCount_ = nullptr;
    QFrame *emptyState_ = nullptr;
    QWidget *gettingBox_ = nullptr;      // first-run guidance, hidden once the device is seen and a profile exists
    QString gettingKey_;
    QWidget *cardHost_ = nullptr;
    QGridLayout *cardGrid_ = nullptr;
    std::vector<QFrame *> cards_;
    QString cardsKey_;  // what the cards were built from, to rebuild only when it changed
    int columns_ = 1;
};

QWidget *createOverviewPage(const UiContext &ctx);
