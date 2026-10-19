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
#include "pages/axes_page.hpp"
#include "pages/modes_page.hpp"
#include "qt_action_runner.hpp"
#include "theme.hpp"
#include "tray.hpp"
#include "ui/model/first_start.hpp"
#include "ui/model/instance_paths.hpp"
#include "ui/model/restart_plan.hpp"
#include "ui/model/theme_colors.hpp"
#include "ui/model/ui_settings.hpp"
#include "ui_context.hpp"

static void printUsage() {
    std::printf("Usage: dofscroll-ui [--help] [--version] [--self-test] [--screenshots <dir>] [--restart]\n"
                "  Settings window and tray icon for dofscroll (Qt). Qt options such as\n"
                "  -platform are passed through.\n"
                "  -h, --help   print this text and exit\n"
                "  --version    print the version and exit\n"
                "  --self-test  headless self-test with temporary directories\n"
                "  --screenshots <dir>  render every page at three sizes into PNG files, then exit\n"
                "  --restart    wait for a running instance to exit, then start (used by the tray)\n");
}

// Themes: Dark and Light change the palette of the application, the left tab icons follow it, and
// "System" brings the original palette back. Without the SVG module the icons are skipped.
static bool themeSelfTest(MainWindow &w) {
    const QPalette original = QApplication::palette();
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "theme self-test failed: %s\n", what);
        ok = ok && cond;
    };
    // does the icon of the first tab contain the colour of the palette's button text?
    auto iconHasColor = [&w](const QColor &c) {
        const QImage img = w.categoryIcon(0).pixmap(20, 20).toImage();
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                const QColor p = img.pixelColor(x, y);
                if (p.alpha() > 200 && p.red() == c.red() && p.green() == c.green() && p.blue() == c.blue()) return true;
            }
        return false;
    };
    for (spn::ui::Theme t : {spn::ui::Theme::Dark, spn::ui::Theme::Light}) {
        spn::ui::ThemeColors colors;
        check(spn::ui::themeColors(t, &colors), "colours of a forced theme");
        applyTheme(t);
        QApplication::processEvents();
        const QColor win = QApplication::palette().color(QPalette::Window);
        check(win == QColor(colors.window.r, colors.window.g, colors.window.b), "palette window colour");
#ifdef QT_SVG_LIB
        check(w.categoryIconsLoaded(), "left tab icons");
        check(iconHasColor(QColor(colors.buttonText.r, colors.buttonText.g, colors.buttonText.b)),
              "tab icons use the text colour of the theme");
#endif
        check(!w.grab().isNull(), "window renders");
    }
    applyTheme(spn::ui::Theme::System);
    QApplication::processEvents();
    check(QApplication::palette().color(QPalette::Window) == original.color(QPalette::Window), "System restores the palette");
    std::printf("theme self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

int main(int argc, char **argv) {
    bool selfTest = false;
    const char *screenshotDir = nullptr;
    bool restarted = false;  // started by "Restart interface": wait for the old instance's lock
    // Handled before QApplication exists: no window, no lock, no display needed.
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version")) {
            std::printf("dofscroll-ui %s\n", DOFSCROLL_VERSION);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            printUsage();
            return 0;
        }
        if (!std::strcmp(argv[i], "--self-test")) selfTest = true;
        if (!std::strcmp(argv[i], "--screenshots") && i + 1 < argc) screenshotDir = argv[i + 1];
        if (!std::strcmp(argv[i], spn::ui::restartFlag())) restarted = true;
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

    if (screenshotDir) {
        // Renders every page at three window sizes into PNG files (for the review of layout changes
        // and CI artifacts). Private temporary directories; works with QT_QPA_PLATFORM=offscreen.
        QTemporaryDir tmp;
        if (!tmp.isValid()) return 1;
        UiContext ctx;
        ctx.runner = &runner;
        ctx.configPath = (tmp.path() + QStringLiteral("/dofscroll.conf")).toStdString();
        ctx.modesDir = (tmp.path() + QStringLiteral("/modes")).toStdString();
        ctx.runtimeDir = (tmp.path() + QStringLiteral("/run")).toStdString();
        QDir().mkpath(tmp.path() + QStringLiteral("/modes"));
        QDir().mkpath(QString::fromUtf8(screenshotDir));
        MainWindow w(ctx);
        w.show();
        // Light at three sizes, dark at the medium size (the layout does not depend on the colours).
        static const struct { const char *theme; const char *name; int w, h; } shots[] = {
            {"light", "small", 720, 460}, {"light", "medium", 960, 560}, {"light", "wide", 1500, 820}, {"dark", "medium", 960, 560}};
        int written = 0;
        for (const auto &sz : shots) {
            applyTheme(spn::ui::themeFromId(sz.theme));
            w.resize(sz.w, sz.h);
            for (const std::string &id : w.pageIds()) {
                w.showPage(QString::fromStdString(id));
                QApplication::processEvents();
                const QString file = QString::fromUtf8(screenshotDir) + QStringLiteral("/%1-%2-%3.png")
                                         .arg(QString::fromLatin1(sz.theme), QString::fromLatin1(sz.name),
                                              QString::fromStdString(id));
                if (!w.grab().save(file)) return 1;
                ++written;
            }
        }
        std::printf("screenshots: %d files in %s\n", written, screenshotDir);
        return written > 0 ? 0 : 1;
    }

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
        const bool ok = w.pageCount() == 5 && w.categoryCount() == 4 && w.profileCount() >= 0 && !w.profilePageHasModeSelector() && modes.modeCount() == 4 &&
                        tray.modeMenuCount() == 4 && tray.menuEntryCount() >= 8;
        if (!ok) return 1;
        if (!themeSelfTest(w)) return 1;
        return axesPageSelfTest();  // Axes page with a fake live feed, rendered offscreen
    }

    // Single-instance guard in the per-user runtime directory: writable inside the systemd unit
    // (ReadWritePaths=%t) and cleaned at logout (src/ui/model/instance_paths.hpp).
    const UiContext ctx = makeUiContext(&runner);
    const std::string idir = spn::ui::instanceDir(ctx.runtimeDir, spn::platformPaths().configDir);
    QDir().mkpath(QString::fromStdString(idir));
    QLockFile lock(QString::fromStdString(spn::ui::instanceLockPath(idir)));
    // A second start asks the running instance (marker file, polled below) to show its window.
    const QString raiseMarker = QString::fromStdString(spn::ui::instanceRaisePath(idir));
    // A short timeout lets Qt's stale-lock detection run (dead owner process after SIGTERM/kill);
    // after "Restart interface" the old instance is still shutting down, so wait longer.
    if (!lock.tryLock(spn::ui::lockWaitMs(restarted))) {
        if (spn::ui::classifyLockFailure(lock.error() == QLockFile::LockFailedError) ==
            spn::ui::LockOutcome::AlreadyRunning) {
            QFile m(raiseMarker);
            if (m.open(QIODevice::WriteOnly)) m.close();
            std::fprintf(stderr, "dofscroll-ui is already running\n");
            return spn::ui::kExitAlreadyRunning;
        }
        std::fprintf(stderr, "dofscroll-ui: cannot create the single-instance lock %s (%s)\n",
                     qPrintable(lock.fileName()),
                     lock.error() == QLockFile::PermissionError ? "permission denied" : "unknown error");
        return spn::ui::kExitLockFailure;
    }
    QFile::remove(raiseMarker);  // stale request of an earlier start

    // First start, as the GTK tray does (src/ui/model/first_start.hpp).
    std::fputs(spn::ui::runFirstStart().text("dofscroll-ui").c_str(), stderr);

    // The theme of the user (before any window exists, so the whole interface starts in it).
    applyTheme(spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).theme);

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
