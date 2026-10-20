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
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <cstdio>
#include <cstring>

#include "main_window.hpp"
#include "pages/axes_page.hpp"
#include "pages/button_modes_page.hpp"
#include "pages/mode_editor_widget.hpp"
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

// Lets the event loop run until the layouts have settled: a resize or a palette change posts layout
// requests, which are handled by the next round, so one processEvents() is not enough for a picture.
static void settle() {
    for (int i = 0; i < 4; ++i) QApplication::processEvents();
}

// Button modes page: lists the files, edits one in the page and saves it, creates and deletes one.
static bool buttonModesSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "button modes self-test failed: %s\n", what);
        ok = ok && cond;
    };
    ButtonModesPage page(ctx);  // standalone: its own catalog
    page.show();
    settle();
    check(page.buttonModeCount() == 1, "one button mode in the folder");
    check(page.editCurrent(QStringLiteral("Edited"), QStringLiteral("Changed"), QStringLiteral("key:space")),
          "edit and save in the page");
    QFile f(tmpPath + QStringLiteral("/buttons/latch.conf"));
    check(f.open(QIODevice::ReadOnly), "the file exists");
    const QByteArray text = f.readAll();
    check(text.contains("NAME = Edited") && text.contains("DESCRIPTION = Changed") && text.contains("ACTION = key:space"),
          "the file holds the edited values");
    check(!page.dirty(), "saved: no unsaved changes");
    // a file that appears in the folder shows up (the page polls the folder stamp)
    QFile second(tmpPath + QStringLiteral("/buttons/second.conf"));
    check(second.open(QIODevice::WriteOnly) && second.write("ACTION = click\n") > 0, "write a second file");
    second.close();
    page.refreshIfChanged();
    check(page.buttonModeCount() == 2, "a new file shows up in the list");
    QFile::remove(tmpPath + QStringLiteral("/buttons/second.conf"));
    page.refreshIfChanged();
    check(page.buttonModeCount() == 1, "a removed file disappears from the list");
    return ok;
}

// Mode editor: opens the file mode "mine" in the page, edits channel, button and override, saves, and checks the
// file (comments and the name survive, the daemon loader accepts the result).
static bool modeEditorSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "mode editor self-test failed: %s\n", what);
        ok = ok && cond;
    };
    ModesPage page(ctx);
    page.show();
    settle();
    check(page.selectMode(QStringLiteral("mine")), "the file mode is listed");
    check(page.openEditor() && page.editorWidget(), "the editor opens for a file mode");
    spn::ui::ModeForm &f = page.editorWidget()->editor().form();
    check(f.id == "mine" && f.name == "Mine" && f.button(1) && *f.button(1) == "key:space", "the form shows the file");
    f.h = {spn::ChannelActionType::Step, "ctrl+pageup", "ctrl+pagedown"};
    f.setButton(2, "toggle:nav");
    f.overrides.emplace_back("SCROLL_V_SENS", "2");
    check(page.editorWidget()->editor().validate().empty(), "the edited form is valid");
    check(page.editorWidget()->save(), "save");
    QFile file(tmpPath + QStringLiteral("/modes/mine.conf"));
    check(file.open(QIODevice::ReadOnly), "the file exists");
    const QByteArray text = file.readAll();
    check(text.contains("NAME = Mine") && text.contains("H_ACTION = step:ctrl+pageup,ctrl+pagedown") &&
              text.contains("BUTTON_2 = toggle:nav") && text.contains("SCROLL_V_SENS = 2") && text.contains("BUTTON_1 = key:space"),
          "the file holds the edits and keeps the rest");
    const spn::ModeSet ms = spn::loadModes((tmpPath + QStringLiteral("/modes")).toStdString(), nullptr);
    const spn::ModeDef *m = ms.find("mine");
    check(m && m->h.type == spn::ChannelActionType::Step && m->buttons.size() == 2, "the daemon loader reads the saved file");
    // an invalid value is refused and nothing is written
    f.overrides.back().second = "abc";
    check(!page.editorWidget()->editor().validate().empty() && !page.editorWidget()->save(), "invalid values are refused");
    return ok;
}

// Overview: it is the first page, shows one card per profile of the configuration, and a card opens
// its profile on the Profiles page.
static bool overviewSelfTest(MainWindow &w) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "overview self-test failed: %s\n", what);
        ok = ok && cond;
    };
    w.show();
    settle();
    check(w.currentPageId() == QStringLiteral("overview"), "the Overview is the first page");
    check(w.overviewCardCount() == 3, "one card per profile (device, two programs)");
    check(w.showPage(QStringLiteral("profiles")) && w.currentPageId() == QStringLiteral("profiles"), "showPage");
    check(w.showPage(QStringLiteral("overview")), "back to the Overview");
    // the cards wrap and keep their height when the window is made narrower or wider
    for (const QSize &size : {QSize(1500, 820), QSize(960, 560), QSize(720, 460), QSize(1100, 700)}) {
        w.resize(size);
        settle();
        check(w.overviewCardsFit(), "cards keep their height after a resize");
        check(w.overviewCardsFillWidth(), "cards share the width after a resize");
    }
    // ... and wrap: more columns in a wide window than in a narrow one, never fewer than one. The
    // window manager or a small screen may limit the size, so compare the widths actually reached.
    w.resize(1500, 820);
    settle();
    const int wideColumns = w.overviewColumns(), wideWidth = w.width();
    w.resize(720, 460);
    // The platform may deliver the resize a little later: give it some rounds before judging.
    for (int i = 0; i < 40 && wideColumns >= 2 && w.overviewColumns() >= wideColumns; ++i) {
        settle();
        QThread::msleep(10);
    }
    check(w.overviewColumns() >= 1 && wideColumns >= w.overviewColumns(), "cards never wrap to more columns in a narrower window");
    if (wideWidth - w.width() >= 400 && w.width() <= 800 && wideColumns >= 2) {  // 3 cards need ~780 px
        if (wideColumns <= w.overviewColumns())
            std::fprintf(stderr, "overview: wide %d px = %d columns, narrow %d px = %d columns\n", wideWidth, wideColumns,
                         w.width(), w.overviewColumns());
        check(wideColumns > w.overviewColumns(), "cards wrap to fewer columns in a narrow window");
    }
    w.resize(960, 560);
    // the cards open their profile: section 1 is the device profile, 3 the last program profile
    for (size_t section : {size_t(1), size_t(3)}) {
        w.openOverviewProfile(static_cast<int>(section));
        settle();
        check(w.currentPageId() == QStringLiteral("profiles") && w.selectedProfileIs(section), "a card opens its profile");
        w.showPage(QStringLiteral("overview"));
    }
    std::printf("overview self-test: %s\n", ok ? "ok" : "failed");
    return ok;
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
        settle();
        const QColor win = QApplication::palette().color(QPalette::Window);
        check(win == QColor(colors.window.r, colors.window.g, colors.window.b), "palette window colour");
#ifdef QT_SVG_LIB
        check(w.categoryIconsLoaded(), "left tab icons");
        check(iconHasColor(QColor(colors.buttonText.r, colors.buttonText.g, colors.buttonText.b)),
              "tab icons use the text colour of the theme");
#endif
        check(!w.grab().isNull(), "window renders");
        w.showPage(QStringLiteral("overview"));
        settle();
        check(w.overviewCardsFit(), "the Overview cards keep their height after a theme change");
    }
    applyTheme(spn::ui::Theme::System);
    settle();
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
        ctx.buttonModesDir = (tmp.path() + QStringLiteral("/buttons")).toStdString();
        ctx.runtimeDir = (tmp.path() + QStringLiteral("/run")).toStdString();
        QDir().mkpath(tmp.path() + QStringLiteral("/modes"));
        QDir().mkpath(tmp.path() + QStringLiteral("/buttons"));
        QDir().mkpath(QString::fromUtf8(screenshotDir));
        {
            QFile mf(tmp.path() + QStringLiteral("/modes/slides.conf"));
            if (!mf.open(QIODevice::WriteOnly)) return 1;
            mf.write("# SAFETY: a mode file can press any key.\nNAME = Slides\nDESCRIPTION = Pages through slides\n"
                     "V_ACTION = step:pageup,pagedown\nH_ACTION = none\nBUTTON_1 = mode:next\nSCROLL_V_SENS = 2\n");
        }
        for (const auto &bmf : {std::make_pair("latch-nav", "NAME = Navigation latch\nDESCRIPTION = Press once to switch navigation on, again to switch it off\nACTION = toggle:nav\n"),
                                std::make_pair("next-mode", "NAME = Next mode\nACTION = mode:next\n")}) {
            QFile f(tmp.path() + QStringLiteral("/buttons/") + QString::fromLatin1(bmf.first) + QStringLiteral(".conf"));
            if (!f.open(QIODevice::WriteOnly)) return 1;
            f.write(bmf.second);
        }
        // Example profiles, so that the Overview shows cards.
        QFile conf(tmp.path() + QStringLiteral("/dofscroll.conf"));
        if (!conf.open(QIODevice::WriteOnly)) return 1;
        conf.write("[global]\n\n[device:SpaceMouse Pro]\nSCROLL_V_SENS = 9\n\n[app:firefox]\nMODE = timeline\nBUTTON_1 = key:space\nBUTTON_2 = buttonmode:latch-nav\nBUTTON_3 = buttonmode:gone\n\n"
                   "[app:blender]\nENABLED = 0\n\n[app:gimp]\n");
        conf.close();
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
                if (id == "profiles") w.openOverviewProfile(2);  // the firefox profile: mode and button modes
                settle();
                const QString file = QString::fromUtf8(screenshotDir) + QStringLiteral("/%1-%2-%3.png")
                                         .arg(QString::fromLatin1(sz.theme), QString::fromLatin1(sz.name),
                                              QString::fromStdString(id));
                if (!w.grab().save(file)) return 1;
                ++written;
            }
        }
        {   // the in-page mode editor
            ModesPage page(ctx);
            page.resize(1100, 700);
            page.show();
            settle();
            if (page.selectMode(QStringLiteral("slides")) && page.openEditor()) {
                settle();
                if (page.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/light-wide-mode-editor.png"))) ++written;
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
        ctx.buttonModesDir = (tmp.path() + QStringLiteral("/buttons")).toStdString();
        ctx.runtimeDir = (tmp.path() + QStringLiteral("/run")).toStdString();
        QDir().mkpath(tmp.path() + QStringLiteral("/modes"));
        QFile mode(tmp.path() + QStringLiteral("/modes/mine.conf"));
        if (!mode.open(QIODevice::WriteOnly)) return 1;
        mode.write("NAME = Mine\nBUTTON_1 = key:space\n");
        mode.close();
        // A configuration with profiles, so that the Overview has cards to show and to open.
        QFile conf(tmp.path() + QStringLiteral("/dofscroll.conf"));
        if (!conf.open(QIODevice::WriteOnly)) return 1;
        conf.write("[global]\n\n[device:SpaceMouse Pro]\nSCROLL_V_SENS = 9\n\n[app:firefox]\nMODE = timeline\n\n[app:blender]\nENABLED = 0\n");
        conf.close();

        QDir().mkpath(tmp.path() + QStringLiteral("/buttons"));
        QFile bm(tmp.path() + QStringLiteral("/buttons/latch.conf"));
        if (!bm.open(QIODevice::WriteOnly)) return 1;
        bm.write("NAME = Latch\nACTION = toggle:nav\n");
        bm.close();

        MainWindow w(ctx);
        w.show();
        if (!buttonModesSelfTest(ctx, tmp.path())) return 1;
        if (!modeEditorSelfTest(ctx, tmp.path())) return 1;
        ModesPage modes(ctx);  // standalone (own catalog): built-in normal, off, timeline + the file mode
        modes.show();
        Tray tray(&w, icon, ctx);
        // Must not crash; the result depends on the platform (offscreen: usually false).
        const bool avail = QSystemTrayIcon::isSystemTrayAvailable();
        std::printf("self-test: entries=%d profiles=%d modes=%d tray-modes=%d tray-entries=%d tray-available=%d\n",
                    w.pageCount(), w.profileCount(), modes.modeCount(), tray.modeMenuCount(),
                    tray.menuEntryCount(), avail ? 1 : 0);
        const bool ok = w.pageCount() == 7 && w.categoryCount() == 5 && w.profileCount() >= 0 && !w.profilePageHasModeSelector() && modes.modeCount() == 4 &&
                        tray.modeMenuCount() == 4 && tray.menuEntryCount() >= 8;
        if (!ok) return 1;
        if (!overviewSelfTest(w)) return 1;
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
