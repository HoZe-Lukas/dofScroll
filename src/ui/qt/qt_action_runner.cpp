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
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "platform/autostart_platform.hpp"
#ifndef _WIN32
#include "platform/exec_lookup.hpp"
#endif
#include "platform/platform.hpp"
#include "ui/model/release_keys.hpp"
#include "ui/model/restart_plan.hpp"
#include "ui/model/setup_guide_model.hpp"
#include "ui/model/ui_links.hpp"
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

bool QtActionRunner::openUrl(const std::string &url, std::string *err) {
    if (!spn::ui::isOpenableUrl(url)) {
        if (err) *err = T("Only https addresses are opened").toStdString();
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl(QString::fromStdString(url), QUrl::StrictMode))) {
        if (err) *err = T("No application could open %1").arg(QString::fromStdString(url)).toStdString();
        return false;
    }
    return true;
}

bool QtActionRunner::openSystemSettings(const std::string &url, std::string *err) {
    if (!spn::ui::isSystemSettingsUrl(url)) {
        if (err) *err = T("This settings pane is not known").toStdString();
        return false;
    }
    if (!QDesktopServices::openUrl(QUrl(QString::fromStdString(url)))) {
        if (err) *err = T("The system settings could not be opened").toStdString();
        return false;
    }
    return true;
}

bool QtActionRunner::setAutostart(bool enable, std::string *err) {
    return spn::autostart::setAutostart(enable, err);
}

bool QtActionRunner::requestReload(std::string *err) { return spn::requestReload(err); }

namespace {
QStringList toQArgs(const spn::ui::Argv &argv) {
    QStringList args;
    for (size_t i = 1; i < argv.size(); ++i) args << QString::fromStdString(argv[i]);
    return args;
}

// Windows: this is a GUI process, a child that is a console program (powershell.exe, dofscroll.exe)
// would open a console window that flashes up. CREATE_NO_WINDOW prevents it. No effect elsewhere.
void noConsoleWindow(QProcess *p) {
#ifdef Q_OS_WIN
    p->setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *a) { a->flags |= CREATE_NO_WINDOW; });
#else
    (void)p;
#endif
}

// Runs argv (no shell) and waits; the exit code, or -1 if it did not start or did not finish in time.
// Helper programs (systemctl, launchctl) come from the fixed system directories, never from PATH
// (the same lookup as the daemon). Windows plans already carry full paths.
bool resolveHelper(spn::ui::Argv *argv) {
#ifdef _WIN32
    (void)argv;
    return true;
#else
    return spn::ui::resolveHelperArgv(argv, [](const std::string &n) { return spn::findHelper(n); });
#endif
}

int runWait(spn::ui::Argv argv, int timeoutMs) {
    if (argv.empty() || !resolveHelper(&argv)) return -1;
    QProcess p;
    noConsoleWindow(&p);
    p.start(QString::fromStdString(argv[0]), toQArgs(argv));
    if (!p.waitForStarted(5000)) return -1;
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        return -1;
    }
    return p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
}

bool startDetached(spn::ui::Argv argv) {
    if (argv.empty() || !resolveHelper(&argv)) return false;
    QProcess p;
    noConsoleWindow(&p);
    p.setProgram(QString::fromStdString(argv[0]));
    p.setArguments(toQArgs(argv));
    return p.startDetached();
}

unsigned currentUid() {
#ifdef _WIN32
    return 0;
#else
    return static_cast<unsigned>(::getuid());
#endif
}
}  // namespace

// Detached `dofscroll --release-keys` (argv, no shell): the daemon binary next to this executable if
// there is one (same build or install), otherwise the one on PATH.
bool QtActionRunner::releaseKeys(std::string *err) {
    const std::string exeDir = QCoreApplication::applicationDirPath().toStdString();
    const std::vector<std::string> argv = spn::ui::releaseKeysCommand(exeDir, [](const std::string &p) {
        return QFileInfo(QString::fromStdString(p)).isExecutable();
    });
    if (!startDetached(argv)) {
        if (err) *err = T("Cannot run %1").arg(QString::fromStdString(argv[0])).toStdString();
        return false;
    }
    return true;
}

// Plan in restart_plan.hpp: the service manager restarts the daemon when it manages it, otherwise
// stop (Windows: stop-daemon.ps1) and start the binary next to this executable.
bool QtActionRunner::restartDaemon(std::string *err) {
    const std::string exeDir = QCoreApplication::applicationDirPath().toStdString();
    const spn::ui::DaemonRestartPlan plan = spn::ui::daemonRestartPlan(
        spn::ui::hostOs(), exeDir,
        [](const std::string &p) { return QFileInfo(QString::fromStdString(p)).isExecutable(); }, currentUid());
    if (!plan.probe.empty() && runWait(plan.probe, 10000) == 0) {
        if (runWait(plan.managed, 30000) == 0) return true;
        if (err) *err = T("Cannot restart the daemon through the service manager").toStdString();
        return false;
    }
    if (!plan.stop.empty()) {
        // stop-daemon.ps1: 0 = no daemon was running, 10 = it was running and has been stopped.
        const int rc = runWait(plan.stop, 30000);
        if (rc != 0 && rc != 10) {
            if (err) *err = T("Cannot stop the running daemon").toStdString();
            return false;
        }
    }
    if (!startDetached(plan.spawn)) {
        if (err) *err = T("Cannot run %1").arg(QString::fromStdString(plan.spawn[0])).toStdString();
        return false;
    }
    return true;
}

// Plan in restart_plan.hpp (daemonStartPlan): the service manager starts the daemon when it knows it,
// otherwise (or when that fails) the binary is started detached. The daemon guards itself against a
// second instance, so a start that races with another one does no harm.
bool QtActionRunner::startDaemon(std::string *err) {
    const std::string exeDir = QCoreApplication::applicationDirPath().toStdString();
    const spn::ui::DaemonStartPlan plan = spn::ui::daemonStartPlan(
        spn::ui::hostOs(), exeDir,
        [](const std::string &p) { return QFileInfo(QString::fromStdString(p)).isExecutable(); }, currentUid());
    const bool known = plan.probe.empty() || runWait(plan.probe, 10000) == 0;
    if (known && !plan.managed.empty() && runWait(plan.managed, 30000) == 0) return true;
    if (!startDetached(plan.spawn)) {
        if (err) *err = T("Cannot run %1").arg(QString::fromStdString(plan.spawn[0])).toStdString();
        return false;
    }
    return true;
}

// systemctl is-active: 0 active, 3 inactive or failed, 4 no such unit (spacenavd may run in another way).
spn::ui::ServiceState QtActionRunner::spacenavdState() {
    if (spn::ui::hostOs() != spn::ui::Os::Linux) return spn::ui::ServiceState::Unknown;
    const int rc = runWait(spn::ui::spacenavdProbe(), 10000);
    return rc == 0 ? spn::ui::ServiceState::Active
                   : rc == 3 ? spn::ui::ServiceState::Inactive : spn::ui::ServiceState::Unknown;
}

// Under dofscroll-ui.service systemd restarts this process; otherwise a copy is started with
// --restart (it waits for our single-instance lock) and the caller quits this process.
bool QtActionRunner::restartInterface(std::string *err) {
    const bool underUnit = !qEnvironmentVariableIsEmpty("INVOCATION_ID") &&
                           spn::ui::hostOs() == spn::ui::Os::Linux && runWait(spn::ui::uiUnitProbe(), 10000) == 0;
    spn::ui::Argv args;
    const QStringList all = QCoreApplication::arguments();
    for (int i = 1; i < all.size(); ++i) args.push_back(all[i].toStdString());
    const spn::ui::UiRestartPlan plan =
        spn::ui::uiRestartPlan(spn::ui::hostOs(), QCoreApplication::applicationFilePath().toStdString(), args, underUnit);
    if (!plan.managed.empty()) {
        if (runWait(plan.managed, 30000) == 0) return true;
        if (err) *err = T("Cannot restart the interface through the service manager").toStdString();
        return false;
    }
    if (!startDetached(plan.relaunch)) {
        if (err) *err = T("Cannot run %1").arg(QString::fromStdString(plan.relaunch[0])).toStdString();
        return false;
    }
    return true;
}

std::string QtStrings::tr(const char *source) const { return T(source).toStdString(); }
