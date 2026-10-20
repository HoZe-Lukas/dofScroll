// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "log_dialog.hpp"

#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

#include "ui/model/log_view_model.hpp"
#include "ui_i18n.hpp"

LogDialog::LogDialog(const std::string &runtimeDir, const spn::ui::StringProvider &strings, QWidget *parent)
    : QDialog(parent), runtimeDir_(runtimeDir), strings_(strings) {
    setWindowTitle(T("dofscroll log"));
    resize(760, 460);
    auto *col = new QVBoxLayout(this);
    view_ = new QPlainTextEdit(this);
    view_->setReadOnly(true);
    view_->setLineWrapMode(QPlainTextEdit::NoWrap);
    view_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view_->setAccessibleName(T("Log text"));
    col->addWidget(view_, 1);
    note_ = new QLabel(this);
    note_->setWordWrap(true);
    note_->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(note_);

    auto *buttons = new QDialogButtonBox(this);
    auto *refreshBtn = buttons->addButton(T("Refresh"), QDialogButtonBox::ActionRole);
    refreshBtn->setToolTip(T("Read the log file again"));
    auto *copy = buttons->addButton(T("Copy"), QDialogButtonBox::ActionRole);
    copy->setToolTip(T("Copy the shown text to the clipboard"));
    buttons->addButton(QDialogButtonBox::Close);
    connect(refreshBtn, &QPushButton::clicked, this, [this] { refresh(); });
    connect(copy, &QPushButton::clicked, this, [this] { copyAll(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    col->addWidget(buttons);
    refresh();
}

bool LogDialog::refresh() {
    const spn::ui::LogView v = spn::ui::readLogView(runtimeDir_, strings_);
    if (!v.ok) {
        view_->clear();
        note_->setText(qs8(v.error));
        return false;
    }
    view_->setPlainText(qs8(v.text));
    QScrollBar *bar = view_->verticalScrollBar();
    bar->setValue(bar->maximum());  // the newest lines
    QString note = T("The end of %1 (at most %2 KB).").arg(qs8(v.path)).arg(spn::ui::kLogViewBytes / 1024);
    if (v.cut) note += QLatin1Char(' ') + T("Older lines are left out.");
    if (v.text.empty()) note = T("The log file is empty.");
    note_->setText(note);
    return !v.text.empty();
}

QString LogDialog::shownText() const { return view_->toPlainText(); }
QString LogDialog::note() const { return note_->text(); }

void LogDialog::copyAll() { QApplication::clipboard()->setText(view_->toPlainText()); }

LogDialog *LogDialog::showFor(QWidget *parent, const std::string &runtimeDir, const spn::ui::StringProvider &strings) {
    static QPointer<LogDialog> open;  // one log window at a time
    if (open) {
        open->refresh();
        open->show();
        open->raise();
        open->activateWindow();
        return open;
    }
    auto *dlg = new LogDialog(runtimeDir, strings, parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    open = dlg;
    dlg->show();
    return dlg;
}
