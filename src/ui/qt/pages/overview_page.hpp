// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Overview" page, the first thing the window shows: the state of the daemon, what is active now,
// quick actions (mode switch, tasks, log, documentation) and one card per device or program profile.
// The data comes from src/ui/model/overview_model.hpp; the page only draws it and reports clicks.
#pragma once
#include <QEvent>
#include <QString>
#include <QWidget>

#include <vector>

#include "ui/model/overview_model.hpp"

#include "ui_context.hpp"

class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
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
    // Text of the filter box above the cards; the cards shown are the profiles that match it.
    void setProfileFilter(const QString &text);
    // The quick actions (the same code as a click on the button).
    void openDocumentation();
    void showLog();
    void switchMode(const QString &id);
    QString actionMessage() const;    // result of the last quick action ("" = fine)
    QComboBox *modeCombo() const { return mode_; }
    // Widest content (the page stops growing there, see design_tokens.hpp).
    int contentWidth() const;
    QStringList gettingStartedTexts() const;  // what the "Getting started" box shows now (tests)

signals:
    void openProfile(int section);  // a card was clicked: show this profile on the Profiles page
    void openPage(const QString &pageId);
    void newProgramProfile();       // "New profile for the running program": open its picker directly

protected:
    bool eventFilter(QObject *watched, QEvent *e) override;  // the card area got another width: wrap the cards again
    void resizeEvent(QResizeEvent *e) override;
    void showEvent(QShowEvent *e) override;

private:
    void rebuildGettingStarted(const std::vector<spn::ui::GettingStartedStep> &steps);
    void rebuildCards(const spn::ui::OverviewData &d, const std::vector<size_t> &shown);
    void rebuildModes(const spn::ui::OverviewData &d);
    void layoutCards();
    void runAction(int index);
    void showMessage(const std::string &err);

    const UiContext &ctx_;
    QLabel *statusLine_ = nullptr, *infoLine_ = nullptr, *warningLine_ = nullptr;
    QLabel *actionMsg_ = nullptr, *profilesCount_ = nullptr, *noMatch_ = nullptr;
    QFrame *emptyState_ = nullptr;
    QWidget *gettingBox_ = nullptr;      // first-run guidance, hidden once the device is seen and a profile exists
    QString gettingKey_;
    QStringList gettingTexts_;
    QWidget *content_ = nullptr;
    QComboBox *mode_ = nullptr;
    QString modeKey_;
    QLineEdit *filter_ = nullptr;
    QWidget *cardHost_ = nullptr;
    QGridLayout *cardGrid_ = nullptr;
    QGridLayout *quickGrid_ = nullptr;
    std::vector<QPushButton *> quickButtons_;
    int quickColumns_ = 0;
    void layoutQuickActions();
    std::vector<QFrame *> cards_;
    QString cardsKey_;  // what the cards were built from, to rebuild only when it changed
    int columns_ = 1;
};

QWidget *createOverviewPage(const UiContext &ctx);
