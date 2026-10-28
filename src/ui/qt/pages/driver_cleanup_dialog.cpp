// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "driver_cleanup_dialog.hpp"

#include <QAbstractItemView>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>
#include <vector>

#include "ui_i18n.hpp"
#include "plain_text.hpp"

namespace {

QString outputText(spn::ui::DriverOutput o, const std::string &id) {
    switch (o) {
    case spn::ui::DriverOutput::MouseWheel: return T("mouse wheel");
    case spn::ui::DriverOutput::Keystroke: return T("keys");
    case spn::ui::DriverOutput::MouseMove: return T("mouse movement");
    case spn::ui::DriverOutput::Joystick: return T("joystick");
    case spn::ui::DriverOutput::Passthrough: return T("passed to the program");
    case spn::ui::DriverOutput::Other: break;
    }
    return qs8(id);
}

// "Name (program.exe)" and, grouped by what the driver does, the axes: "mouse wheel: X, Ry; keys: Z". An axis
// appears once per group (the driver lists the two directions of an axis separately).
QString rowText(const spn::ui::DriverApp &a) {
    QString t = qs8(a.name);
    if (!a.exe.empty() && a.exe != a.name) t += QStringLiteral(" (") + qs8(a.exe) + QLatin1Char(')');
    std::vector<std::pair<QString, QStringList>> groups;
    for (const spn::ui::DriverAxis &d : a.active) {
        const QString what = outputText(d.output, d.outputId);
        auto it = std::find_if(groups.begin(), groups.end(), [&](const auto &g) { return g.first == what; });
        if (it == groups.end()) {
            groups.push_back({what, QStringList()});
            it = groups.end() - 1;
        }
        const QString axis = qs8(d.input);
        if (!it->second.contains(axis)) it->second.append(axis);
    }
    QStringList parts;
    for (const auto &g : groups) parts << g.first + QStringLiteral(": ") + g.second.join(QStringLiteral(", "));
    t += QStringLiteral(" \u2014 ") + parts.join(QStringLiteral("; "));
    if (a.navigation) t += QStringLiteral("  [") + T("only passes the axes on") + QLatin1Char(']');
    return t;
}

}  // namespace

DriverCleanupDialog::DriverCleanupDialog(const std::string &dir, QWidget *parent) : QDialog(parent), dir_(dir) {
    setWindowTitle(T("3Dconnexion driver: remove axis settings"));
    resize(780, 520);
    auto *col = new QVBoxLayout(this);
    auto *intro = new QLabel(T("When the 3Dconnexion driver has an axis switched on for a program, that program gets the "
                               "driver's input and dofscroll's scrolling, so every movement arrives twice. Tick the programs "
                               "whose driver axes should be switched off. Programs that use the driver for 3D navigation "
                               "(CAD, 3D) are not ticked: leave them as they are. Every changed file is saved first and can "
                               "be restored."),
                             this);
    intro->setTextFormat(Qt::PlainText);
    intro->setWordWrap(true);
    col->addWidget(intro);
    list_ = new QListWidget(this);
    list_->setAccessibleName(T("Programs with active driver axes"));
    // Several rows can be ticked: click each box, or select rows (Ctrl/Shift) and press Space.
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setWordWrap(true);  // long rows wrap instead of being cut off
    list_->setTextElideMode(Qt::ElideNone);
    col->addWidget(list_, 1);
    auto *tickRow = new QHBoxLayout;
    auto *all = new QPushButton(T("Tick all"), this);
    all->setObjectName(QStringLiteral("tick-all"));
    all->setToolTip(T("Tick every program in the list"));
    auto *none = new QPushButton(T("Tick none"), this);
    none->setObjectName(QStringLiteral("tick-none"));
    none->setToolTip(T("Remove every tick"));
    tickRow->addWidget(all);
    tickRow->addWidget(none);
    tickRow->addStretch(1);
    col->addLayout(tickRow);
    connect(all, &QPushButton::clicked, this, [this] { tickAll(true); });
    connect(none, &QPushButton::clicked, this, [this] { tickAll(false); });
    note_ = new QLabel(this);
    note_->setTextFormat(Qt::PlainText);
    note_->setWordWrap(true);
    note_->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(note_);

    auto *buttons = new QDialogButtonBox(this);
    clean_ = buttons->addButton(T("Switch off the ticked programs"), QDialogButtonBox::ActionRole);
    clean_->setToolTip(T("Saves each file first, then switches the axes of the ticked programs off"));
    restore_ = buttons->addButton(T("Restore the last backup"), QDialogButtonBox::ActionRole);
    restore_->setToolTip(T("Puts the files of the newest backup back"));
    auto *rescan = buttons->addButton(T("Read again"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    connect(clean_, &QPushButton::clicked, this, [this] { cleanUp(true); });
    connect(restore_, &QPushButton::clicked, this, [this] { restoreLast(true); });
    connect(rescan, &QPushButton::clicked, this, [this] { scan(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    col->addWidget(buttons);
    scan();
}

bool DriverCleanupDialog::scan() {
    std::string err;
    bool truncated = false;
    const bool ok = spn::ui::scanDriverConfigs(dir_, &apps_, &err, &truncated);
    refreshList();
    if (!ok) {
        note_->setText(T("The settings folder of the 3Dconnexion driver was not found. Is the driver installed?"));
    } else if (apps_.empty()) {
        note_->setText(T("No program has driver axes switched on. Nothing to clean up."));
    } else {
        note_->setText(T("%1 programs have driver axes switched on. If a change does not take effect, restart the "
                         "3Dconnexion driver or sign out and in again.")
                           .arg(int(apps_.size())));
    }
    if (ok && truncated)
        note_->setText(note_->text() + QLatin1Char('\n') + T("The folder holds more files than are listed here; the list is not complete."));
    return ok;
}

void DriverCleanupDialog::refreshList() {
    list_->clear();
    for (const spn::ui::DriverApp &a : apps_) {
        auto *item = new QListWidgetItem(rowText(a), list_);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(a.selected ? Qt::Checked : Qt::Unchecked);
    }
    clean_->setEnabled(!apps_.empty());
    restore_->setEnabled(!spn::ui::listDriverBackups(dir_).empty());
}

int DriverCleanupDialog::rowCount() const { return list_->count(); }
bool DriverCleanupDialog::rowChecked(int row) const { return list_->item(row)->checkState() == Qt::Checked; }
void DriverCleanupDialog::setRowChecked(int row, bool on) { list_->item(row)->setCheckState(on ? Qt::Checked : Qt::Unchecked); }
void DriverCleanupDialog::tickAll(bool on) {
    for (int i = 0; i < list_->count(); ++i) setRowChecked(i, on);
}
QString DriverCleanupDialog::note() const { return note_->text(); }

bool DriverCleanupDialog::cleanUp(bool confirm) {
    int picked = 0;
    for (size_t i = 0; i < apps_.size(); ++i) {
        apps_[i].selected = rowChecked(int(i));
        picked += apps_[i].selected ? 1 : 0;
    }
    if (picked == 0) {
        note_->setText(T("Tick at least one program."));
        return false;
    }
    if (confirm && spn::ui::qt::plainBox(QMessageBox::Question, this, windowTitle(),
                                         T("Switch off the driver axes of %1 programs? The files are saved first and can be restored.")
                                             .arg(picked)) != QMessageBox::Yes)
        return false;
    spn::ui::DriverCleanupReport rep;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const bool ok = spn::ui::applyDriverCleanup(dir_, apps_, stamp.toStdString(), &rep);
    scan();
    if (ok) {
        note_->setText(T("Done: %1 files changed. The originals are in %2.").arg(int(rep.changed.size())).arg(qs8(rep.backupDir)));
    } else {
        QString msg = T("Not everything worked:");
        for (const std::string &f : rep.failed) msg += QStringLiteral(" ") + qs8(f) + QLatin1Char(';');
        note_->setText(msg);
    }
    return ok;
}

bool DriverCleanupDialog::restoreLast(bool confirm) {
    const std::vector<std::string> backups = spn::ui::listDriverBackups(dir_);
    if (backups.empty()) {
        note_->setText(T("There is no backup."));
        return false;
    }
    if (confirm && spn::ui::qt::plainBox(QMessageBox::Question, this, windowTitle(), T("Put the files of the backup %1 back?").arg(qs8(backups.front()))) !=
                       QMessageBox::Yes)
        return false;
    spn::ui::DriverCleanupReport rep;
    const bool ok = spn::ui::restoreDriverBackup(dir_, backups.front(), &rep);
    scan();
    if (ok) note_->setText(T("Restored: %1 files.").arg(int(rep.changed.size())));
    else note_->setText(T("The backup could not be restored completely."));
    return ok;
}
