// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Log viewer: a small dialog with the end of the daemon log (src/ui/model/log_view_model.hpp),
// "Refresh" and "Copy". Shown from the Overview, the General page and the tray ("Show log...").
#pragma once
#include <QDialog>
#include <QString>

#include <string>

#include "ui/model/strings.hpp"

class QLabel;
class QPlainTextEdit;

class LogDialog : public QDialog {
    Q_OBJECT
public:
    // `strings` must outlive the dialog.
    LogDialog(const std::string &runtimeDir, const spn::ui::StringProvider &strings, QWidget *parent = nullptr);
    // Reads the log again; false if there is nothing to show (the reason is in the note).
    bool refresh();
    QString shownText() const;  // the log text in the view
    QString note() const;       // line below the text: what is shown, or why nothing is
    void copyAll();             // puts the shown text on the clipboard

    // Opens a dialog that deletes itself when it is closed; the same dialog is raised if it is open.
    static LogDialog *showFor(QWidget *parent, const std::string &runtimeDir, const spn::ui::StringProvider &strings);

private:
    std::string runtimeDir_;
    const spn::ui::StringProvider &strings_;
    QPlainTextEdit *view_;
    QLabel *note_;
};
