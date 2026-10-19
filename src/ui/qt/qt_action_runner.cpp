// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "qt_action_runner.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QFileInfo>
#include <QProcess>
#include <QStringList>
#include <QString>
#include <QUrl>
#include <vector>

#include "platform/autostart_platform.hpp"
#include "platform/platform.hpp"
#include "ui/model/release_keys.hpp"
#include "ui_i18n.hpp"

bool QtActionRunner::openPath(const std::string &path, std::string *err) {
    const QString p = QString::fromStdString(path);
    if (path.empty() || !QFileInfo::exists(p)) {
        if (err) *err = T("Does not exist: %1").arg(p).toStdString();
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(p))) {
        if (err) *err = T("No application could open %1").arg(p).toStdString();
        return false;
    }
    return true;
}

bool QtActionRunner::setAutostart(bool enable, std::string *err) {
    return spn::autostart::setAutostart(enable, err);
}

bool QtActionRunner::requestReload(std::string *err) { return spn::requestReload(err); }

// Detached `dofscroll --release-keys` (argv, no shell): the daemon binary next to this executable if
// there is one (same build or install), otherwise the one on PATH.
bool QtActionRunner::releaseKeys(std::string *err) {
    const std::string exeDir = QCoreApplication::applicationDirPath().toStdString();
    const std::vector<std::string> argv = spn::ui::releaseKeysCommand(exeDir, [](const std::string &p) {
        return QFileInfo(QString::fromStdString(p)).isExecutable();
    });
    QStringList args;
    for (size_t i = 1; i < argv.size(); ++i) args << QString::fromStdString(argv[i]);
    if (!QProcess::startDetached(QString::fromStdString(argv[0]), args)) {
        if (err) *err = T("Cannot run %1").arg(QString::fromStdString(argv[0])).toStdString();
        return false;
    }
    return true;
}

std::string QtStrings::tr(const char *source) const { return T(source).toStdString(); }
