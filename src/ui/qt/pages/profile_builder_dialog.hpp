// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "New profile": builds a profile from existing parts (src/ui/model/profile_builder_model.hpp): the kind (program or
// device) and the match name, a base to start from (nothing, or the settings of an existing profile), a joystick mode
// (it follows the mode by name, or the profile gets its own copy), a button mode per button (the same choice) and
// single motions or buttons taken over from other profiles. The result is a normal profile, editable afterwards on the
// Motions and buttons tab. Nothing is written until "Create profile"; a refused build says why in the dialog.
#pragma once
#include <QDialog>
#include <QList>
#include <QPointer>
#include <set>
#include <vector>

#include "pages/profiles_env.hpp"
#include "ui/model/profile_builder_model.hpp"
#include "ui_context.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QVBoxLayout;
class QWidget;

class ProfileBuilderDialog : public QDialog {
    Q_OBJECT
public:
    ProfileBuilderDialog(const UiContext &ctx, const ProfilesEnv &env, QWidget *parent = nullptr);
    // After exec() == Accepted: the profile that was created (its section in the model).
    size_t createdSection() const { return created_.section; }
    const spn::ui::ProfileBuildResult &result() const { return created_; }
    // The spec the form describes now (the dialog's buttons call buildProfile with it).
    spn::ui::ProfileBuild spec() const;
    QString problemText() const;

private slots:
    void accept() override;

private:
    struct ButtonRow {
        QWidget *box = nullptr;
        QSpinBox *number = nullptr;
        QComboBox *mode = nullptr;
        QCheckBox *copy = nullptr;
        QPushButton *remove = nullptr;
    };
    struct MotionTakeRow {
        QWidget *box = nullptr;
        QComboBox *motion = nullptr, *source = nullptr;
        QPushButton *remove = nullptr;
    };
    struct TakeButtonRow {
        QWidget *box = nullptr;
        QSpinBox *number = nullptr;
        QComboBox *source = nullptr;
        QPushButton *remove = nullptr;
    };
    void addButtonRow();
    void addMotionRow();
    void addTakeButtonRow();
    void validate();
    void choosePattern();
    void updateTabOrder();
    std::set<int> usedButtons() const;
    static void focusNeighbour(QWidget *next, QWidget *previous, QWidget *fallback);

    const UiContext &ctx_;
    const ProfilesEnv &env_;
    QRadioButton *app_ = nullptr, *device_ = nullptr;
    QLineEdit *name_ = nullptr;
    QComboBox *base_ = nullptr, *mode_ = nullptr;
    QCheckBox *modeCopy_ = nullptr;
    QLabel *modeWords_ = nullptr, *problem_ = nullptr, *summary_ = nullptr;
    QVBoxLayout *buttonRows_ = nullptr, *motionRows_ = nullptr, *takeButtonRows_ = nullptr;
    QPushButton *ok_ = nullptr, *cancel_ = nullptr, *choose_ = nullptr, *addButton_ = nullptr, *addMotion_ = nullptr, *addTakeButton_ = nullptr;
    QLabel *nameLabel_ = nullptr, *nameHelp_ = nullptr;
    std::vector<spn::ui::BorrowSource> motionSources_, buttonSources_;
    std::vector<ButtonRow> buttons_;
    std::vector<MotionTakeRow> motions_;
    std::vector<TakeButtonRow> takeButtons_;
    std::vector<std::string> modeIds_, buttonModeIds_;
    spn::ui::ProfileBuildResult created_;
    QString buildError_;
};
