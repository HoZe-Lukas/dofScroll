// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "qt_action_runner.hpp"

#include <QDesktopServices>
#include <QProcess>
#include <QString>
#include <QUrl>

#include "platform/autostart_platform.hpp"
#include "ui_i18n.hpp"

bool QtActionRunner::openPath(const std::string &path, std::string *err) {
    if (QDesktopServices::openUrl(QUrl::fromLocalFile(qs8(path)))) return true;
    if (err) *err = "cannot open " + path;
    return false;
}

bool QtActionRunner::setAutostart(bool enable, std::string *err) {
    return spn::autostart::setAutostart(enable, err);
}

bool QtActionRunner::requestReload(std::string *err) {
#if defined(Q_OS_LINUX)
    QProcess p;
    p.start(QStringLiteral("systemctl"),
            {QStringLiteral("--user"), QStringLiteral("kill"), QStringLiteral("-s"), QStringLiteral("HUP"),
             QStringLiteral("dofscroll.service")});
    if (p.waitForFinished(3000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) return true;
    if (p.state() != QProcess::NotRunning) p.kill();
    if (err) *err = "systemctl --user kill -s HUP dofscroll.service failed (is the daemon running as a user service?)";
    return false;
#else
    (void)err;
    return true;
#endif
}
