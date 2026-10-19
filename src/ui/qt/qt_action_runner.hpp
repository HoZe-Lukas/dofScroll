// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ActionRunner for the Qt UI. Opening files uses the desktop's default application; autostart and
// the daemon reload are wired in by the pages that own them (they report "not available" here).
#pragma once
#include <QDesktopServices>
#include <QString>
#include <QUrl>

#include "ui/model/action_runner.hpp"

class QtActionRunner : public spn::ui::ActionRunner {
public:
    bool openPath(const std::string &path, std::string *err) override {
        if (QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromUtf8(path.data(), qsizetype(path.size()))))) return true;
        if (err) *err = "cannot open " + path;
        return false;
    }
    bool setAutostart(bool, std::string *err) override { return unavailable(err); }
    bool requestReload(std::string *err) override { return unavailable(err); }

private:
    static bool unavailable(std::string *err) {
        if (err) *err = "not available in this build";
        return false;
    }
};
