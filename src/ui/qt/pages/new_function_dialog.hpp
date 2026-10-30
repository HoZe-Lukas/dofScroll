// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "New function..." (stage 7a of docs/plan-motion-functions.md): the dialog that composes one function for a motion or a device
// button of a profile. Kind (key or combination, mouse click, hold, macro, scroll or zoom, move the pointer), the fields of the
// kind per direction, analog / digital, rate and trigger, a live preview of the action text. There is no Save button: as soon as
// every field is valid the function is assigned (the model validates with the parsers of the daemon and writes through the
// autosave); an invalid state is shown next to its field and nothing is written. The logic is in
// ui/model/new_function_model.hpp; this is only the view.
#pragma once
#include <QDialog>
#include <functional>
#include <vector>

#include "config.hpp"
#include "ui/model/new_function_model.hpp"
#include "ui/model/strings.hpp"
#include "ui_context.hpp"

class ChordField;
class MacroEditor;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

struct NewFunctionEnv {
    spn::ui::FunctionTarget target;
    QString targetName;   // "Horizontal tilt", "Button 3": named in the texts and the descriptions of the controls
    QString scopeName;    // "this profile", "Global"
    spn::ui::FunctionDraft initial;
    bool editing = false;  // the motion or button has a function of its own already (the title says so)
    spn::Settings effective;  // the settings that apply (the words about the trigger level)
    const spn::ui::StringProvider *strings = nullptr;
    // Validates and assigns; false + problems when something is invalid or the joystick mode keeps the motion.
    std::function<bool(const spn::ui::FunctionDraft &, std::vector<spn::ui::FunctionProblem> *)> apply;
    // Where the function applies and what it replaces, in words (after every assignment).
    std::function<QString()> stateWords;
    // Push and tilt of a scroll channel are set separately: whether the channel is split yet, in words ("" for other targets).
    std::function<QString()> splitWords;
};

class NewFunctionDialog : public QDialog {
    Q_OBJECT
public:
    explicit NewFunctionDialog(const NewFunctionEnv &env, QWidget *parent = nullptr);

    const spn::ui::FunctionDraft &draft() const { return draft_; }
    bool assigned() const { return assigned_; }  // the last edit was written
    int writes() const { return writes_; }       // how many times a function was assigned
    QString previewText() const;                 // the action text shown
    void chooseKind(spn::ui::FunctionKind kind); // as the user does with the drop-down

signals:
    void functionAssigned();  // after every assignment

private:
    void build();
    QWidget *buildKeyPage();
    QWidget *buildClickPage();
    QWidget *buildHoldPage();
    QWidget *buildWheelPage();
    QWidget *buildPointerPage();
    QWidget *buildMacroPage();
    void syncWidgets();   // the widgets show the draft
    void edited();        // the user changed something: check, assign, show
    void refreshAnalog();
    void refreshFields();
    void showResult(bool ok, const std::vector<spn::ui::FunctionProblem> &problems);
    QLabel *label(const QString &text, const char *objectName = nullptr);
    QLineEdit *addField(QWidget *host, const QString &labelText, const char *objectName, const std::string &key, QLabel **note);

    NewFunctionEnv env_;
    spn::ui::FunctionDraft draft_;
    bool assigned_ = false, quiet_ = false;
    int writes_ = 0;
    QComboBox *kind_ = nullptr;
    QLabel *splitWords_ = nullptr, *kindNotes_ = nullptr, *actionNote_ = nullptr, *preview_ = nullptr, *status_ = nullptr, *summary_ = nullptr;
    QStackedWidget *pages_ = nullptr;
    std::vector<spn::ui::FunctionKind> pageKinds_;
    ChordField *keyNeg_ = nullptr, *keyPos_ = nullptr, *holdNeg_ = nullptr, *holdPos_ = nullptr;
    QComboBox *clickNeg_ = nullptr, *clickPos_ = nullptr, *wheel_ = nullptr, *pointer_ = nullptr;
    MacroEditor *macroNeg_ = nullptr, *macroPos_ = nullptr;
    QComboBox *macroDirection_ = nullptr;
    QCheckBox *analog_ = nullptr;
    QLabel *analogReason_ = nullptr, *modeWords_ = nullptr, *defaultsWords_ = nullptr;
    QWidget *fieldsHost_ = nullptr, *ratesHost_ = nullptr, *triggerHost_ = nullptr;
    QLineEdit *rateMin_ = nullptr, *rateMax_ = nullptr, *trigger_ = nullptr;
    QLabel *rateMinNote_ = nullptr, *rateMaxNote_ = nullptr, *triggerNote_ = nullptr, *triggerLabel_ = nullptr;
    bool keyMouse_ = false;
};

// Opens the dialog for a motion or a button of the profile `section` (window-modal, without a nested event loop; it deletes
// itself when closed). `changed` runs after every assignment and when the dialog closes (the page redraws).
NewFunctionDialog *openNewFunctionDialog(const UiContext &ctx, size_t section, const spn::ui::FunctionTarget &target, QWidget *parent,
                                         std::function<void()> changed);
