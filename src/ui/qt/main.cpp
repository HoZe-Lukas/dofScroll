// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll-ui: Qt6 Widgets tray icon and settings window (skeleton, see docs/ui-options.md).
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QLockFile>
#include <QStandardPaths>
#include <QSystemTrayIcon>
#include <QTranslator>
#include <cstdio>
#include <cstring>

#include "main_window.hpp"
#include "pages/axes_page.hpp"
#include "tray.hpp"

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
    QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("dofscroll"), QStringLiteral("_"),
                        QApplication::applicationDirPath()))
        QApplication::installTranslator(&translator);

    const QIcon icon(QStringLiteral(":/dofscroll-tray.svg"));

    if (selfTest) {
        MainWindow w;
        w.show();
        // Must not crash; the result depends on the platform (offscreen: usually false).
        const bool avail = QSystemTrayIcon::isSystemTrayAvailable();
        std::printf("self-test: entries=%d profiles=%d tray-available=%d\n", w.pageCount(),
                    w.profileCount(), avail ? 1 : 0);
        if (w.pageCount() != 5 || w.profileCount() != 3) return 1;
        return axesPageSelfTest();  // Axes page with a fake live feed, rendered offscreen
    }

    // Single-instance guard in a per-user directory (QLockFile detects stale locks itself).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(dir);
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                   QFileDevice::ExeOwner);
    QLockFile lock(dir + QStringLiteral("/dofscroll-ui.lock"));
    if (!lock.tryLock(0)) {
        std::fprintf(stderr, "dofscroll-ui is already running\n");
        return 0;
    }

    MainWindow window;
    window.setWindowIcon(icon);
    Tray tray(&window, icon);
    tray.show();
    return app.exec();
}
