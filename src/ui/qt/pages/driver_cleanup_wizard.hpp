// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The cleanup of the official 3Dconnexion driver's per-program axis settings as a step-by-step assistant
// (issue #184). Shown from the General page (Windows). The pages:
//   1 Introduction and consent  what the driver does, what is and is not changed, the backup, the running driver
//   2 Scan result               the folder, the numbers, the files that were skipped and why
//   3 Choose the programs       a table with a real check box per program (ticks are independent of the row
//                               selection), Tick all / none / all except 3D and CAD / Invert, a filter, a counter
//   4 Preview                   exactly what will change; nothing is written yet
//   5 Apply                     the backup and the change, with a progress bar
//   6 Result                    what changed, where the backup is, restore it, restart the driver
// Every file operation and every rule lives in src/ui/model/driver_cleanup_model.hpp; this is the view.
#pragma once
#include <QDialog>
#include <QString>
#include <QWizard>

#include <string>
#include <vector>

#include "ui/model/driver_cleanup_model.hpp"

class QListWidget;
class QPushButton;
class QLabel;

// What the pages share.
struct DriverWizardState {
    std::string dir;                              // the driver's folder (tests pass a temporary one)
    std::vector<spn::ui::DriverApp> apps;         // the programs with active axes; `selected` = ticked
    spn::ui::DriverScanReport scan;
    spn::ui::DriverCleanupReport report;          // of the last cleanup
    bool scanned = false;
    int generation = 0;                           // counts the scans: the table is rebuilt when it changes
    bool applied = false;                         // the cleanup ran
    std::string backupStamp;                      // the backup of the cleanup (the folder name)
};

// "Restore an older backup": lists the backups with their date and number of files.
class DriverBackupDialog : public QDialog {
    Q_OBJECT
public:
    DriverBackupDialog(const std::string &dir, QWidget *parent = nullptr);
    int rowCount() const;
    void selectRow(int row);
    std::string selectedStamp() const;  // "" = nothing selected
    QPushButton *restoreButton() const { return restore_; }

private:
    std::vector<std::string> stamps_;
    QListWidget *list_;
    QPushButton *restore_;
};

class DriverCleanupWizard : public QWizard {
    Q_OBJECT
public:
    enum PageId { IntroPage, ScanPage, ChoosePage, PreviewPage, ApplyPage, ResultPage };

    // `dir` = the driver's folder with the per-program files.
    explicit DriverCleanupWizard(const std::string &dir, QWidget *parent = nullptr);
    DriverWizardState &state() { return state_; }
    const DriverWizardState &state() const { return state_; }

    // Puts the files of the backup `stamp` back. `confirm`: ask first. Shows the outcome on the result page.
    bool restoreBackup(const std::string &stamp, bool confirm = true);
    // Opens the list of the backups and restores the chosen one (the "older backup" button).
    void restoreOlder();

signals:
    void restored(bool ok);

private:
    DriverWizardState state_;
};
