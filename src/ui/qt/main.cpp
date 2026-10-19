// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll-ui: Qt6 Widgets tray icon and settings window (see docs/ui-options.md).
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QLockFile>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTimer>
#include <QTranslator>
#include <cstdio>
#include <cstring>

#include "main_window.hpp"
#include "pages/modes_page.hpp"
#include "qt_action_runner.hpp"
#include "tray.hpp"
#include "ui/model/ui_settings.hpp"
#include "ui_context.hpp"

int main(int argc, char **argv) {
    bool selfTest = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version")) {
            std::printf("dofscroll-ui %s\n", DOFSCROLL_VERSION);
            return 0;
        }
        if (!std::strcmp(argv[i], "--self-test")) selfTest = true;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("dofscroll"));
    QApplication::setApplicationVersion(QStringLiteral(DOFSCROLL_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);

    // Catalog converted from po/de.po at build time (dofscroll_<lang>.qm next to the binary).
    // The language comes from the per-user UI settings (General page): "en" = no catalog.
    const std::string lang = spn::ui::languageLocaleName(spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).language);
    QTranslator translator;
    if (lang != "en" &&
        translator.load(lang.empty() ? QLocale() : QLocale(QString::fromStdString(lang)),
                        QStringLiteral("dofscroll"), QStringLiteral("_"), QApplication::applicationDirPath()))
        QApplication::installTranslator(&translator);

    const QIcon icon(QStringLiteral(":/dofscroll-tray.svg"));
    QtActionRunner runner;

    if (selfTest) {
        // Private temporary directories: the test never touches the user's configuration.
        QTemporaryDir tmp;
        if (!tmp.isValid()) return 1;
        UiContext ctx;
        ctx.runner = &runner;
        ctx.configPath = (tmp.path() + QStringLiteral("/dofscroll.conf")).toStdString();
        ctx.modesDir = (tmp.path() + QStringLiteral("/modes")).toStdString();
        ctx.runtimeDir = (tmp.path() + QStringLiteral("/run")).toStdString();
        QDir().mkpath(tmp.path() + QStringLiteral("/modes"));
        QFile mode(tmp.path() + QStringLiteral("/modes/mine.conf"));
        if (!mode.open(QIODevice::WriteOnly)) return 1;
        mode.write("NAME = Mine\nBUTTON_1 = key:space\n");
        mode.close();

        MainWindow w(ctx);
        w.show();
        ModesPage modes(ctx);  // standalone (own catalog): built-in normal, off, timeline + the file mode
        modes.show();
        Tray tray(&w, icon, ctx);
        // Must not crash; the result depends on the platform (offscreen: usually false).
        const bool avail = QSystemTrayIcon::isSystemTrayAvailable();
        std::printf("self-test: entries=%d profiles=%d modes=%d tray-modes=%d tray-entries=%d tray-available=%d\n",
                    w.pageCount(), w.profileCount(), modes.modeCount(), tray.modeMenuCount(),
                    tray.menuEntryCount(), avail ? 1 : 0);
        const bool ok = w.pageCount() == 5 && w.profileCount() >= 0 && !w.profilePageHasModeSelector() && modes.modeCount() == 4 &&
                        tray.modeMenuCount() == 4 && tray.menuEntryCount() >= 8;
        return ok ? 0 : 1;
    }

    // Single-instance guard in a per-user directory (QLockFile detects stale locks itself).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                   QFileDevice::ExeOwner);
    QLockFile lock(dir + QStringLiteral("/dofscroll-ui.lock"));
    // A second start asks the running instance (marker file, polled below) to show its window.
    const QString raiseMarker = dir + QStringLiteral("/dofscroll-ui.raise");
    if (!lock.tryLock(0)) {
        QFile m(raiseMarker);
        if (m.open(QIODevice::WriteOnly)) m.close();
        std::fprintf(stderr, "dofscroll-ui is already running\n");
        return 0;
    }
    QFile::remove(raiseMarker);  // stale request of an earlier start

    const UiContext ctx = makeUiContext(&runner);
    MainWindow window(ctx);
    window.setWindowIcon(icon);
    Tray tray(&window, icon, ctx);
    tray.show();
    QTimer raiseTimer;
    QObject::connect(&raiseTimer, &QTimer::timeout, [&] {
        if (QFile::exists(raiseMarker) && QFile::remove(raiseMarker)) window.bringToFront();
    });
    raiseTimer.start(500);
    return app.exec();
}
