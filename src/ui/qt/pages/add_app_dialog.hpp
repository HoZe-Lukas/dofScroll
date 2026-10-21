// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Add application" dialog: searchable list of running programs (listed off the UI thread), the
// exact match string, an "already has a profile" marker, refresh, "capture next focused program"
// with a visible countdown, and manual entry with validation.
#pragma once
#include <QDialog>
#include <QElapsedTimer>
#include <QPointer>
#include <string>
#include <vector>

#include "profiles_env.hpp"
#include "ui/model/app_picker_model.hpp"

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTimer;
class QTreeWidget;
class QDialogButtonBox;

class AddAppDialog : public QDialog {
    Q_OBJECT
public:
    // current (not empty): "Change application" for an existing profile; the match string starts as `current` and
    // `configured` must not contain it.
    AddAppDialog(const ProfilesEnv &ctx, const std::vector<std::string> &configured, QWidget *parent = nullptr,
                 const QString &current = QString());
    ~AddAppDialog() override;
    QString matchString() const;  // validated, trimmed (valid after exec() == Accepted)

private:
    void refresh();
    void applyResult(bool ok, const QString &err, std::vector<spn::RunningApp> apps);
    void refill();
    void startCapture();
    void stopCapture();
    void tickCapture();
    void validate();

    const ProfilesEnv &ctx_;
    spn::AppPickerModel model_;
    spn::CaptureNextFocus capture_;
    QElapsedTimer clock_;
    QTimer *captureTimer_;
    QLineEdit *search_, *manual_;
    QTreeWidget *list_;
    QLabel *note_, *validation_, *countdown_;
    QProgressBar *busy_;
    QPushButton *refresh_, *capture_btn_;
    QDialogButtonBox *buttons_;
    bool listing_ = false;
    int generation_ = 0;  // ignores results of superseded listings
};
