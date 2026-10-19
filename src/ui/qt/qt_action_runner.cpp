// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "qt_action_runner.hpp"

#include <QDesktopServices>
#include <QFileInfo>
#include <QString>
#include <QUrl>

#include "platform/autostart_platform.hpp"
#include "platform/platform.hpp"
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

std::string QtStrings::tr(const char *source) const { return T(source).toStdString(); }
