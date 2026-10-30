// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "macro_editor.hpp"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "chord_field.hpp"
#include "key_capture.hpp"
#include "plain_text.hpp"
#include "ui_i18n.hpp"

using spn::MacroStep;
using spn::ui::MacroList;

namespace {
// The key name of keys.hpp for a key event while recording. Like the capture button, except that the modifier keys are keys of
// their own here (a macro presses Ctrl and the key as separate steps), with Ctrl and Cmd swapped back on macOS.
std::string recordKeyName(const QKeyEvent *e) {
#ifdef Q_OS_MACOS
    if (e->key() == Qt::Key_Control) return "super";
    if (e->key() == Qt::Key_Meta) return "ctrl";
#endif
    return captureKeyName(e);
}

QPushButton *smallButton(const QString &text, const char *objectName, const QString &description, QWidget *parent) {
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QString::fromLatin1(objectName));
    b->setAccessibleName(text);
    b->setAccessibleDescription(description);
    b->setMinimumHeight(24);
    b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // a long text must not widen the dialog
    b->setMinimumWidth(100);
    return b;
}

QLabel *wrapLabel(const QString &text, QWidget *parent, const char *objectName = nullptr) {
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    if (objectName) l->setObjectName(QString::fromLatin1(objectName));
    return l;
}
}  // namespace

// ---- the recording area ----

MacroRecordPad::MacroRecordPad(QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("macroRecordPad"));
    setFrameShape(QFrame::StyledPanel);
    setFocusPolicy(Qt::NoFocus);  // focusable only while recording: the keys of the dialog are never taken otherwise
    setAccessibleName(T("Recording area"));
    setAccessibleDescription(T("While recording, the keys you press here become steps of the macro"));
    auto *l = new QVBoxLayout(this);
    text_ = wrapLabel(T("Not recording."), this, "macroPadText");
    l->addWidget(text_);
}

void MacroRecordPad::setActive(bool on) {
    setFocusPolicy(on ? Qt::StrongFocus : Qt::NoFocus);
    text_->setText(on ? T("Recording: press the keys now. Press Esc three times in a row, or use Record stop, to end it.") : T("Not recording."));
    if (on) clock_.start();
}

bool MacroRecordPad::event(QEvent *e) {
    switch (e->type()) {
        case QEvent::ShortcutOverride:  // while recording every key is for the recording (Tab, Esc, Enter, shortcuts)
            if (focusPolicy() != Qt::NoFocus) {
                e->accept();
                return true;
            }
            break;
        case QEvent::KeyPress:
        case QEvent::KeyRelease: {
            if (focusPolicy() == Qt::NoFocus) break;
            auto *ke = static_cast<QKeyEvent *>(e);
            e->accept();
            if (ke->isAutoRepeat()) return true;
            const std::string name = recordKeyName(ke);
            if (name.empty()) {
                if (e->type() == QEvent::KeyPress) emit unsupportedKey();
                return true;
            }
            if (e->type() == QEvent::KeyPress) emit keyDown(qs8(name), clock_.elapsed());
            else emit keyUp(qs8(name), clock_.elapsed());
            return true;
        }
        default: break;
    }
    return QFrame::event(e);
}

void MacroRecordPad::focusOutEvent(QFocusEvent *e) {
    QFrame::focusOutEvent(e);
    if (focusPolicy() != Qt::NoFocus) emit lostFocus();  // the window was left or another control was clicked: recording ends
}

// ---- the editor ----

MacroEditor::MacroEditor(const QString &accessible, QWidget *parent) : QWidget(parent) {
    setObjectName(QStringLiteral("macroEditor"));
    setAccessibleName(accessible);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);

    // ---- the recorder ----
    v->addWidget(wrapLabel(T("Record the keys you press, or add the steps by hand below. Recording happens only in this window, while the recording area has the focus."), this, "macroIntro"));
    auto *recordRow = new QGridLayout;
    record_ = smallButton(T("Record"), "macroRecord", T("Start recording the keys you press into the steps below"), this);
    stop_ = smallButton(T("Record stop"), "macroRecordStop", T("Stop recording"), this);
    stop_->setEnabled(false);
    recordRow->addWidget(record_, 0, 0);
    recordRow->addWidget(stop_, 0, 1);
    v->addLayout(recordRow);
    // a check box does not wrap: a short text, the sentence in the label below
    times_ = new QCheckBox(T("Record the times"), this);
    times_->setObjectName(QStringLiteral("macroRecordTimes"));
    times_->setProperty("look", QStringLiteral("box"));
    times_->setChecked(true);
    times_->setAccessibleName(T("Also record the times between the keys"));
    times_->setAccessibleDescription(T("On: the pauses are the measured times. Off: a pause of 30 ms is put between the steps."));
    times_->setMinimumHeight(24);
    v->addWidget(times_);
    v->addWidget(wrapLabel(T("Also record the times between the keys. Off: a pause of 30 ms is put between the steps."), this, "macroTimesWords"));
    pad_ = new MacroRecordPad(this);
    v->addWidget(pad_);
    recordNote_ = wrapLabel(QString(), this, "macroRecordNote");
    recordNote_->hide();
    v->addWidget(recordNote_);

    // ---- the steps ----
    auto *listLabel = wrapLabel(T("Steps:"), this);
    v->addWidget(listLabel);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("macroSteps"));
    list_->setAccessibleName(T("Steps of the macro"));
    list_->setAccessibleDescription(T("The list of steps; choose one to change it below"));
    list_->setMinimumHeight(110);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setTextElideMode(Qt::ElideRight);
    listLabel->setBuddy(list_);
    v->addWidget(list_);
    count_ = wrapLabel(QString(), this, "macroCount");
    v->addWidget(count_);

    auto *buttons = new QGridLayout;
    addDown_ = smallButton(T("Add key press"), "macroAddDown", T("Add a step that presses a key, after the chosen step"), this);
    addUp_ = smallButton(T("Add key release"), "macroAddUp", T("Add a step that lets go of a key, after the chosen step"), this);
    addWait_ = smallButton(T("Add pause"), "macroAddWait", T("Add a pause, after the chosen step"), this);
    del_ = smallButton(T("Delete"), "macroDelete", T("Delete the chosen step"), this);
    up_ = smallButton(T("Move up"), "macroMoveUp", T("Move the chosen step one place up"), this);
    down_ = smallButton(T("Move down"), "macroMoveDown", T("Move the chosen step one place down"), this);
    clear_ = smallButton(T("Clear all"), "macroClear", T("Delete all steps of this macro"), this);
    buttons->addWidget(addDown_, 0, 0);
    buttons->addWidget(addUp_, 0, 1);
    buttons->addWidget(addWait_, 1, 0);
    buttons->addWidget(del_, 1, 1);
    buttons->addWidget(up_, 2, 0);
    buttons->addWidget(down_, 2, 1);
    buttons->addWidget(clear_, 3, 0);
    v->addLayout(buttons);

    // ---- the chosen step ----
    auto *typeLabel = wrapLabel(T("The chosen step:"), this);
    v->addWidget(typeLabel);
    type_ = new QComboBox(this);
    type_->setObjectName(QStringLiteral("macroStepType"));
    type_->addItem(T("Press a key"), int(MacroStep::Kind::Down));
    type_->addItem(T("Release a key"), int(MacroStep::Kind::Up));
    type_->addItem(T("Pause"), int(MacroStep::Kind::Wait));
    type_->setAccessibleName(T("Kind of the chosen step"));
    type_->setAccessibleDescription(T("Press a key, release a key or pause, for the chosen step"));
    type_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    type_->setMinimumContentsLength(10);
    type_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    type_->setMinimumWidth(100);
    typeLabel->setBuddy(type_);
    v->addWidget(type_);
    chordLabel_ = wrapLabel(T("Key of the step:"), this);
    v->addWidget(chordLabel_);
    ChordField::Options opt;
    chord_ = new ChordField(accessible + QStringLiteral(": ") + T("Key of the chosen step"), opt, this);
    chordLabel_->setBuddy(chord_->keyPicker());
    v->addWidget(chord_);
    waitLabel_ = wrapLabel(T("Pause in milliseconds:"), this);
    v->addWidget(waitLabel_);
    wait_ = new QSpinBox(this);
    wait_->setObjectName(QStringLiteral("macroWait"));
    wait_->setRange(spn::kMinMacroWaitMs, spn::kMaxMacroWaitMs);
    wait_->setValue(spn::kDefaultMacroWaitMs);
    wait_->setAccessibleName(T("Pause in milliseconds"));
    wait_->setAccessibleDescription(T("How long the macro waits at this step, 1 to 60000 milliseconds"));
    wait_->setMinimumHeight(24);
    wait_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    wait_->setMinimumWidth(100);
    waitLabel_->setBuddy(wait_);
    v->addWidget(wait_);

    // Tab order: the recorder, the list, the buttons, the chosen step
    QWidget::setTabOrder(record_, stop_);
    QWidget::setTabOrder(stop_, times_);
    QWidget::setTabOrder(times_, list_);
    QWidget::setTabOrder(list_, addDown_);
    QWidget::setTabOrder(addDown_, addUp_);
    QWidget::setTabOrder(addUp_, addWait_);
    QWidget::setTabOrder(addWait_, del_);
    QWidget::setTabOrder(del_, up_);
    QWidget::setTabOrder(up_, down_);
    QWidget::setTabOrder(down_, clear_);
    QWidget::setTabOrder(clear_, type_);
    QWidget::setTabOrder(type_, chord_->firstControl());  // the key field's own controls follow each other from there
    QWidget::setTabOrder(chord_->captureButton(), wait_);

    connect(record_, &QPushButton::clicked, this, [this] { startRecording(); });
    connect(stop_, &QPushButton::clicked, this, [this] { stopRecording(QString()); });
    connect(pad_, &MacroRecordPad::keyDown, this, [this](const QString &k, qint64 ms) { onKey(true, k, ms); });
    connect(pad_, &MacroRecordPad::keyUp, this, [this](const QString &k, qint64 ms) { onKey(false, k, ms); });
    connect(pad_, &MacroRecordPad::unsupportedKey, this, [this] {
        ++ignored_;
        recordNote_->setText(T("dofscroll cannot send this key, so it is not recorded. Add it by hand from the list if you can."));
        recordNote_->show();
    });
    connect(pad_, &MacroRecordPad::lostFocus, this, [this] {
        if (recorder_.recording()) stopRecording(T("The recording stopped because the keyboard focus left the recording area."));
    });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) {
        if (!quiet_) refreshEditor();
        refreshButtons();
    });
    connect(addDown_, &QPushButton::clicked, this, [this] { addStep(MacroStep::Kind::Down); });
    connect(addUp_, &QPushButton::clicked, this, [this] { addStep(MacroStep::Kind::Up); });
    connect(addWait_, &QPushButton::clicked, this, [this] { addStep(MacroStep::Kind::Wait); });
    connect(del_, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row < 0 || !steps_.remove(size_t(row))) return;
        refreshList(std::min(row, int(steps_.size()) - 1));
        emit stepsChanged();
    });
    connect(up_, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row < 0 || !steps_.moveUp(size_t(row))) return;
        refreshList(row - 1);
        emit stepsChanged();
    });
    connect(down_, &QPushButton::clicked, this, [this] {
        const int row = list_->currentRow();
        if (row < 0 || !steps_.moveDown(size_t(row))) return;
        refreshList(row + 1);
        emit stepsChanged();
    });
    connect(clear_, &QPushButton::clicked, this, [this] {
        if (steps_.empty()) return;
        steps_.clear();
        refreshList(-1);
        emit stepsChanged();
    });
    connect(type_, &QComboBox::activated, this, [this](int) { stepEdited(); });
    connect(chord_, &ChordField::changed, this, [this] { stepEdited(); });
    connect(wait_, &QSpinBox::valueChanged, this, [this](int) { if (!quiet_) stepEdited(); });
    // A dialog can hold several macro editors (one per direction of a motion): every control says which one it belongs to, in its
    // name (the buttons) or its description (the fields). The key field of a step names itself.
    for (QWidget *c : findChildren<QWidget *>()) {
        if (chord_->isAncestorOf(c) || c == chord_) continue;
        const bool button = qobject_cast<QPushButton *>(c) != nullptr;
        const bool field = c == list_ || c == type_ || c == wait_ || c == times_;
        if (!button && !field) continue;
        if (button) c->setAccessibleName(c->accessibleName() + QStringLiteral(", ") + accessible);
        c->setAccessibleDescription(accessible + QStringLiteral(": ") + c->accessibleDescription());
    }
    refreshList(-1);
}

QString MacroEditor::stepWords(const MacroStep &s) const {
    switch (s.kind) {
        case MacroStep::Kind::Down: return T("Press %1").arg(qs8(spn::ui::macroStepChord(s)));
        case MacroStep::Kind::Up: return T("Release %1").arg(qs8(spn::ui::macroStepChord(s)));
        case MacroStep::Kind::Wait: return T("Wait %1 ms").arg(s.waitMs);
    }
    return QString();
}

void MacroEditor::setSteps(const MacroList &steps) {
    steps_ = steps;
    refreshList(steps_.empty() ? -1 : 0);
}

void MacroEditor::refreshList(int select) {
    quiet_ = true;
    list_->clear();
    for (const MacroStep &s : steps_.steps()) list_->addItem(stepWords(s));
    if (select >= 0 && select < list_->count()) list_->setCurrentRow(select);
    quiet_ = false;
    count_->setText(steps_.full() ? T("%1 of %2 steps: the macro is full.").arg(steps_.size()).arg(spn::kMaxMacroSteps)
                                  : T("%1 of %2 steps.").arg(steps_.size()).arg(spn::kMaxMacroSteps));
    refreshEditor();
    refreshButtons();
}

void MacroEditor::refreshEditor() {
    const int row = list_->currentRow();
    const bool have = row >= 0 && size_t(row) < steps_.size();
    type_->setEnabled(have);
    if (!have) {
        chord_->hide();
        chordLabel_->hide();
        waitLabel_->hide();
        wait_->hide();
        return;
    }
    const MacroStep &s = steps_.at(size_t(row));
    quiet_ = true;
    type_->setCurrentIndex(type_->findData(int(s.kind)));
    const bool wait = s.kind == MacroStep::Kind::Wait;
    chord_->setVisible(!wait);
    chordLabel_->setVisible(!wait);
    waitLabel_->setVisible(wait);
    wait_->setVisible(wait);
    if (wait) wait_->setValue(s.waitMs);
    else chord_->setValue(spn::ui::macroStepChord(s));
    quiet_ = false;
}

void MacroEditor::refreshButtons() {
    const int row = list_->currentRow();
    const bool have = row >= 0 && size_t(row) < steps_.size();
    const bool rec = recorder_.recording();
    for (QPushButton *b : {addDown_, addUp_, addWait_}) b->setEnabled(!steps_.full() && !rec);
    del_->setEnabled(have && !rec);
    up_->setEnabled(have && row > 0 && !rec);
    down_->setEnabled(have && size_t(row) + 1 < steps_.size() && !rec);
    clear_->setEnabled(!steps_.empty() && !rec);
    record_->setEnabled(!rec && !steps_.full());
    stop_->setEnabled(rec);
    type_->setEnabled(have && !rec);
    chord_->setEnabled(!rec);
    wait_->setEnabled(!rec);
}

// The chosen step was changed in the editor below the list.
void MacroEditor::stepEdited() {
    if (quiet_) return;
    const int row = list_->currentRow();
    if (row < 0 || size_t(row) >= steps_.size()) return;
    const MacroStep::Kind t = MacroStep::Kind(type_->currentData().toInt());
    const MacroStep &old = steps_.at(size_t(row));
    MacroStep s;
    if (t == MacroStep::Kind::Wait) {
        s = MacroStep::wait(old.kind == MacroStep::Kind::Wait ? wait_->value() : spn::kDefaultMacroWaitMs);
    } else {
        std::string chord = old.kind == MacroStep::Kind::Wait ? std::string("space") : spn::ui::macroStepChord(old);
        if (old.kind != MacroStep::Kind::Wait && !chord_->value().empty()) chord = chord_->value();
        s = t == MacroStep::Kind::Down ? spn::ui::macroDown(chord) : spn::ui::macroUp(chord);
    }
    if (!steps_.set(size_t(row), s)) return;
    if (QListWidgetItem *it = list_->item(row)) it->setText(stepWords(s));
    if (s.kind != old.kind) refreshEditor();  // the fields of the other kind
    emit stepsChanged();
}

void MacroEditor::addStep(MacroStep::Kind type) {
    if (steps_.full()) return;
    const int row = list_->currentRow();
    const size_t at = row < 0 ? steps_.size() : size_t(row) + 1;
    const MacroStep s = type == MacroStep::Kind::Wait ? MacroStep::wait(spn::kDefaultMacroWaitMs) : type == MacroStep::Kind::Down ? spn::ui::macroDown("space") : spn::ui::macroUp("space");
    if (!steps_.insert(at, s)) return;
    refreshList(int(at));
    emit stepsChanged();
}

void MacroEditor::startRecording() {
    if (recorder_.recording() || steps_.full()) return;
    recorder_.setRecordTimes(times_->isChecked());
    recorder_.resetEndedByEscape();
    recorder_.start(steps_);
    ignored_ = 0;
    recordNote_->hide();
    pad_->setActive(true);
    refreshButtons();
    pad_->setFocus(Qt::OtherFocusReason);
}

void MacroEditor::stopRecording(const QString &why) {
    const bool wasRecording = recorder_.recording();
    recorder_.stop();
    pad_->setActive(false);
    QString note = why;
    if (note.isEmpty() && recorder_.full()) note = T("The macro is full (%1 steps): the recording ended.").arg(spn::kMaxMacroSteps);
    if (note.isEmpty() && recorder_.endedByEscape()) note = T("The recording ended with Esc pressed three times; those presses are not part of the macro.");
    if (note.isEmpty() && wasRecording) note = T("Recorded. Change, delete or move any step below.");
    if (!note.isEmpty()) {
        recordNote_->setText(note);
        recordNote_->show();
    }
    refreshButtons();
    if (wasRecording && pad_->hasFocus()) record_->setFocus(Qt::OtherFocusReason);
}

void MacroEditor::onKey(bool down, const QString &key, qint64 ms) {
    if (!recorder_.recording()) return;
    const std::string k = key.toStdString();
    const spn::ui::MacroRecorder::Result r = down ? recorder_.press(k, ms) : recorder_.release(k, ms);
    (void)r;
    steps_ = recorder_.steps();
    refreshList(int(steps_.size()) - 1);
    if (!recorder_.recording()) stopRecording(QString());
    emit stepsChanged();
}
