// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The Save / Discard / Cancel question for unsaved edits (rule: ui/model/unsaved_guard.hpp). The
// self-test replaces the question with an answer through setUnsavedAnswer().
#pragma once
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QWidget>
#include <utility>

#include "ui/model/unsaved_guard.hpp"
#include "ui_i18n.hpp"

namespace unsaved_prompt {

// Non-empty: answers the question instead of a message box (self-test only).
inline spn::ui::UnsavedAsk &answerHook() {
    static spn::ui::UnsavedAsk hook;
    return hook;
}

inline void setUnsavedAnswer(spn::ui::UnsavedAsk hook) { answerHook() = std::move(hook); }

// "what" is the name of the thing with edits (a mode name). Esc answers Cancel.
inline spn::ui::UnsavedChoice ask(QWidget *parent, const std::string &what) {
    if (answerHook()) return answerHook()(what);
    QMessageBox box(QMessageBox::Question, T("Unsaved changes"),
                    T("\"%1\" has unsaved changes. Save them first?").arg(qs8(what)), QMessageBox::NoButton, parent);
    QPushButton *save = box.addButton(T("Save"), QMessageBox::AcceptRole);
    QPushButton *discard = box.addButton(T("Discard"), QMessageBox::DestructiveRole);
    QPushButton *cancel = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(save);
    box.setEscapeButton(cancel);
    box.exec();
    if (box.clickedButton() == save) return spn::ui::UnsavedChoice::Save;
    if (box.clickedButton() == discard) return spn::ui::UnsavedChoice::Discard;
    return spn::ui::UnsavedChoice::Cancel;
}

}  // namespace unsaved_prompt
