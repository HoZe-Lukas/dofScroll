// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Cleanup of the official 3Dconnexion driver's axis settings per program
// (src/ui/model/driver_cleanup_model.hpp): lists the programs whose axes the driver has switched on, the
// user ticks the ones to clean up (3D and CAD programs are not ticked by default), the axes of the ticked
// programs are switched off after a backup. Shown from the General page (Windows).
#pragma once
#include <QDialog>
#include <QString>

#include <string>
#include <vector>

#include "ui/model/driver_cleanup_model.hpp"

class QLabel;
class QListWidget;
class QPushButton;

class DriverCleanupDialog : public QDialog {
    Q_OBJECT
public:
    // `dir` = the driver's folder with the per-program files (tests pass a temporary folder).
    explicit DriverCleanupDialog(const std::string &dir, QWidget *parent = nullptr);
    bool scan();                            // reads the folder again; false if it cannot be read
    int rowCount() const;                   // programs listed
    bool rowChecked(int row) const;
    void setRowChecked(int row, bool on);
    void tickAll(bool on);                  // ticks (or unticks) every program
    QString note() const;                   // line below the list
    bool cleanUp(bool confirm = true);      // switches the ticked programs off; false on error or when nothing is ticked
    bool restoreLast(bool confirm = true);  // puts the newest backup back

private:
    void refreshList();
    std::string dir_;
    std::vector<spn::ui::DriverApp> apps_;
    QListWidget *list_;
    QLabel *note_;
    QPushButton *clean_;
    QPushButton *restore_;
};
