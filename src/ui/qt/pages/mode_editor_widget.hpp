// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// In-page editor for a joystick mode file (Joystick modes page): name, description, the three
// channels (including chords and step:), the buttons (the shared button row) and the settings
// overrides. A thin view on spn::ui::ModeEditor, which owns the logic (comment-preserving save,
// validation, loader check, change detection). There is no Save or Revert button: every valid
// change is written by itself (a switch or drop-down at once, typed text after a pause); an
// invalid form is not written and says what is wrong next to the fields. The file is only ever
// replaced by a version the loader accepts.
#pragma once
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>
#include <string>

#include "autosaver.hpp"
#include "ui/model/mode_editor_model.hpp"
#include "ui/model/tour_model.hpp"
#include "qt_action_runner.hpp"
#include "ui_context.hpp"

class QGridLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class MotionEditor;
struct ButtonRowEnv;
namespace spn { struct ButtonModeSet; }

class ModeEditorWidget : public QWidget {
    Q_OBJECT
public:
    explicit ModeEditorWidget(const UiContext &ctx, QWidget *parent = nullptr);
    // Opens the file of a loaded mode; false (and a message box) if it cannot be read.
    bool open(const std::string &path, const spn::ModeDef &def);
    spn::ui::ModeEditor &editor() { return editor_; }
    const spn::ui::ModeEditor &editor() const { return editor_; }
    // Writes the pending changes now (synchronously). true if there was nothing to write or the
    // write worked; false if the form is invalid or the write was refused.
    bool flush();
    bool pending() const { return saver_ && saver_->pending(); }
    // Writes the form now even if it was not changed (self-test); false: refused.
    bool save(bool overwriteChanged = false);
    // Called when an edit changed the form (the views of the form call it); schedules the write.
    void edited(bool typed);
    // Test seam: answers the "the file changed on disk" question (true = overwrite) instead of a dialog.
    void setOverwriteAnswer(std::function<bool()> answer) { overwriteAnswer_ = std::move(answer); }
    Autosaver *autosaver() const { return saver_.get(); }
    QString problemText() const;  // what is wrong with the form now ("" = valid)
    // Sub-tabs: motions (the picture), settings (name, description, channels), buttons, overrides. The last one stays
    // selected while the form is rebuilt and when another mode is opened.
    // The page that hosts the editor alone (Editor page): no title and no Close button.
    void setCompact(bool on);
    QStringList tabIds() const;
    MotionEditor *motionEditor() const { return motionEditor_; }
    QString currentTabId() const;
    bool selectTab(const QString &id);

signals:
    void closeRequested();
    void saved(const QString &id);

private:
    void rebuild();  // all widgets from the form
    int addChannelRow(QGridLayout *g, int row, const QString &label, spn::ui::ChannelForm *form);  // returns the rows used
    // One scroll channel: the plain action row, or with "Split push and tilt" on a push and a tilt row
    // plus the tuning fields of each part. Returns the rows used. prefix: "V" or "H".
    int addSplitChannel(QGridLayout *g, int row, const QString &label, const QString &pushLabel, const QString &tiltLabel,
                        spn::ui::ChannelForm *channel, spn::ui::SplitForm *split, const char *prefix);
    void showProblems();
    ButtonRowEnv buttonRowEnv(const spn::ButtonModeSet &buttonModes) const;
    void addBindingRow(QWidget *parent, QGridLayout *grid, int gridRow, int n, const ButtonRowEnv &env);
    spn::ui::Autosave::Outcome finishWrite(const spn::ui::ModeEditor::SaveResult &r);

    UiContext ctx_;
    QtStrings ownStrings_;  // used when the context has no shared provider (standalone page, tests)
    spn::ui::ModeEditor editor_;
    std::string dir_;
    QTabWidget *tabs_ = nullptr;
    QString lastTab_ = QStringLiteral("motions");
    QString motionSel_;  // the selected motion or button of the Motions and buttons tab, kept across rebuilds
    bool compact_ = false;  // the Editor page: no scroll area per tab
    MotionEditor *motionEditor_ = nullptr;
    std::unique_ptr<spn::ui::TourModel> motionTour_;  // lives here so that it survives the rebuild after an edit
    bool motionDetect_ = false;                      // Detect by moving was on
    std::string motionTrying_;                       // the joystick mode before "Try this joystick mode" ("" = not trying)
    QLabel *title_ = nullptr, *problems_ = nullptr;
    QPlainTextEdit *warnings_ = nullptr;
    QPushButton *closeBtn_ = nullptr, *textBtn_ = nullptr;
    std::unique_ptr<Autosaver> saver_;
    std::function<bool()> overwriteAnswer_;
};
