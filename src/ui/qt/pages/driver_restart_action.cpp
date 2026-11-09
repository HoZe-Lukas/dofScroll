// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "driver_restart_action.hpp"

#include <QApplication>
#include <QMessageBox>
#include <QStringList>
#include <QWidget>

#include <memory>

#include "platform/driver_restart_os.hpp"
#include "plain_text.hpp"
#include "ui_i18n.hpp"

namespace spn::ui::qt {

QString driverRestartQuestion() {
    return T("Restart the 3Dconnexion driver now? Its settings window is closed and the driver service of your "
             "sign-in is ended and started again. The SpaceMouse does not react for a few seconds. Save what you do in the "
             "3Dconnexion settings first.");
}

QString driverRestartSummary(const DriverRestartReport &rep) {
    if (!rep.supported) return T("The 3Dconnexion driver cannot be restarted from here on this system. Sign out and in again instead.");
    QString t;
    if (rep.ok()) {
        t = T("The 3Dconnexion driver was restarted. It reads its settings files again.");
        if (!rep.closed.empty()) t += QLatin1Char(' ') + T("Its settings window was closed.");
    } else if (!rep.serviceFound && rep.failed.size() == 1 && rep.closed.empty()) {
        t = T("The 3Dconnexion driver is not running, so there is nothing to restart.");
    } else {
        QStringList why;
        for (const std::string &f : rep.failed) why << qs8(f);
        t = T("The 3Dconnexion driver could not be restarted:") + QLatin1Char(' ') + why.join(QStringLiteral("; ")) + QLatin1Char('.') +
            QLatin1Char(' ') + T("Sign out and in again instead.");
    }
    if (!rep.left.empty()) {
        QStringList l;
        for (const std::string &x : rep.left) l << qs8(x);
        t += QLatin1Char('\n') + T("Not ended (they run with more rights than dofscroll has):") + QLatin1Char(' ') + l.join(QStringLiteral("; "));
    }
    return t;
}

QString restartDriverInteractive(QWidget *parent, bool confirm, bool *ran) {
    if (ran) *ran = false;
    std::unique_ptr<DriverRestartOps> ops = makeDriverRestartOps();
    if (!ops) return driverRestartSummary([] {
        DriverRestartReport r;
        r.supported = false;
        return r;
    }());
    if (confirm && plainBox(QMessageBox::Question, parent, T("Restart the 3Dconnexion driver"), driverRestartQuestion()) != QMessageBox::Yes)
        return QString();
    if (ran) *ran = true;
    DriverRestartReport rep;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    restartDriver(*ops, osDriverRestartRules(), &rep);
    QApplication::restoreOverrideCursor();
    return driverRestartSummary(rep);
}

}  // namespace spn::ui::qt
