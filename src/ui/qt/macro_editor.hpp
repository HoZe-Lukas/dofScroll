// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The macro editor of the "New function..." dialog (stage 7b of docs/plan-motion-functions.md): the list of steps with the editor
// of the selected step (key fields of ChordField, pauses in milliseconds), delete / move up / move down / add / clear, and the
// recorder: "Record" and "Record stop" buttons and the check box for the times between the keys. Recording happens ONLY here,
// while the recording area has the keyboard focus: the area takes the key events it is sent as a widget, there is no global
// key logger, and when the focus leaves it (another window, a click elsewhere) the recording ends. Nothing is kept outside
// the step list. The steps are the model of ui/model/macro_model.hpp.
#pragma once
#include <QElapsedTimer>
#include <QFrame>
#include <QWidget>

#include "ui/model/macro_model.hpp"

class ChordField;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

// The area that receives the keys while recording: focusable only while a recording runs.
class MacroRecordPad : public QFrame {
    Q_OBJECT
public:
    explicit MacroRecordPad(QWidget *parent = nullptr);
    void setActive(bool on);  // focusable and showing "Recording" while on

signals:
    void keyDown(const QString &key, qint64 ms);
    void keyUp(const QString &key, qint64 ms);
    void unsupportedKey();
    void lostFocus();

protected:
    bool event(QEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

private:
    QElapsedTimer clock_;
    QLabel *text_ = nullptr;
};

class MacroEditor : public QWidget {
    Q_OBJECT
public:
    // `accessible` names the editor ("Macro for the negative direction"); `sideName` is used in the descriptions of the controls.
    MacroEditor(const QString &accessible, QWidget *parent = nullptr);

    const spn::ui::MacroList &steps() const { return steps_; }
    void setSteps(const spn::ui::MacroList &steps);  // does not emit stepsChanged()
    bool recording() const { return recorder_.recording(); }

    // The key events of a real keyboard reach the pad as widget events; the self-test sends the same events.
    MacroRecordPad *pad() const { return pad_; }

signals:
    void stepsChanged();

private:
    void refreshList(int select = -1);
    void refreshEditor();
    void refreshButtons();
    void stepEdited();
    void addStep(spn::MacroStep::Kind type);
    void startRecording();
    void stopRecording(const QString &why);
    void onKey(bool down, const QString &key, qint64 ms);
    QString stepWords(const spn::MacroStep &s) const;

    spn::ui::MacroList steps_;
    spn::ui::MacroRecorder recorder_;
    QListWidget *list_ = nullptr;
    QComboBox *type_ = nullptr;
    ChordField *chord_ = nullptr;
    QSpinBox *wait_ = nullptr;
    QLabel *waitLabel_ = nullptr, *chordLabel_ = nullptr, *count_ = nullptr, *recordNote_ = nullptr;
    QPushButton *del_ = nullptr, *up_ = nullptr, *down_ = nullptr, *clear_ = nullptr, *addDown_ = nullptr, *addUp_ = nullptr, *addWait_ = nullptr;
    QPushButton *record_ = nullptr, *stop_ = nullptr;
    QCheckBox *times_ = nullptr;
    MacroRecordPad *pad_ = nullptr;
    bool quiet_ = false;
    int ignored_ = 0;
};
