// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll-ui: Qt6 Widgets tray icon and settings window (see docs/ui-options.md).
#include <QApplication>
#include <QSessionManager>
#include <QDir>
#include <QCheckBox>
#include <QMouseEvent>
#include <QRadioButton>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QToolButton>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QLockFile>
#include <QStandardPaths>
#include <QProxyStyle>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QAccessible>
#include <QCloseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QThread>
#include <QAbstractScrollArea>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QClipboard>
#include <QDialog>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTreeView>
#include <QItemSelectionModel>
#include <QTreeWidget>
#include <QTimer>
#include <QTranslator>
#include <algorithm>
#include <atomic>
#include <memory>
#include <optional>
#include <cstdio>
#include <functional>
#include <cstring>
#include <ctime>

#include "driver_platform.hpp"
#include "main_window.hpp"
#include "ui/model/display_text.hpp"
#include "motion_view.hpp"
#include "vertical_tabs.hpp"
#include "pages/add_app_dialog.hpp"
#include "pages/add_device_dialog.hpp"
#include "pages/axes_page.hpp"
#include "button_row.hpp"
#include "daemon_starter.hpp"
#include "pages/setup_guide_dialog.hpp"
#include "common.hpp"
#include "pages/profiles_env.hpp"
#include "pages/button_modes_page.hpp"
#include "pages/cycle_editor_widget.hpp"
#include "pages/editor_page.hpp"
#include "pages/motion_editor.hpp"
#include "key_capture.hpp"
#include "key_combo.hpp"
#include "key_tree_picker.hpp"
#include <QColumnView>
#include "pages/profile_motions_view.hpp"
#include "pages/button_use_widget.hpp"
#include "autosaver.hpp"
#include "described_item_delegate.hpp"
#include "info_popup.hpp"
#include "pages/channels_editor.hpp"
#include "pages/device_page.hpp"
#include "pages/log_dialog.hpp"
#include "pages/mode_editor_widget.hpp"
#include "pages/extensions_page.hpp"
#include "pages/modes_page.hpp"
#include "platform_hooks.hpp"
#include "pages/profiles_page.hpp"
#include "pages/overview_page.hpp"
#include "qt_action_runner.hpp"
#include "theme.hpp"
#include "tray.hpp"
#include "ui/model/tray_controller.hpp"
#include "ui_i18n.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/first_start.hpp"
#include "ui/model/instance_paths.hpp"
#include "ui/model/restart_plan.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/theme_colors.hpp"
#include "ui/model/ui_links.hpp"
#include "ui/model/ui_progress.hpp"
#include "ui/model/ui_settings.hpp"
#include "ui_context.hpp"

static void printUsage() {
    std::printf("Usage: dofscroll-ui [--help] [--version] [--self-test] [--screenshots <dir>] [--restart]\n"
                "  Settings window and tray icon for dofscroll (Qt). Qt options such as\n"
                "  -platform are passed through.\n"
                "  -h, --help   print this text and exit\n"
                "  --version    print the version and the Qt version in use, then exit\n"
                "  --self-test  headless self-test with temporary directories\n"
                "  --screenshots <dir>  render every page at three sizes into PNG files, then exit\n"
                "  --restart    wait for a running instance to exit, then start (used by the tray)\n");
}

// Lets the event loop run until the layouts have settled: a resize or a palette change posts layout
// requests, which are handled by the next round, so one processEvents() is not enough for a picture.
static void settle() {
    for (int i = 0; i < 4; ++i) QApplication::processEvents();
}

// The wheel never changes a combo box, spin box or slider; it scrolls the surrounding scroll area.
// At the default window size no page needs to scroll: the content each page asks for fits the
// area next to the sidebar (the window is bigger than the largest page, minus the chrome).
static bool defaultSizeFitsSelfTest(MainWindow &w) {
    bool ok = true;
    w.show();
    const int chromeW = 260, chromeH = 190;  // sidebar, title, page tabs, status bar
    for (const std::string &id : w.pageIds()) {
        w.showPage(QString::fromStdString(id));
        settle();
        const QWidget *page = w.currentPage();
        if (!page) continue;
        const QSize hint = page->minimumSizeHint();  // what the layout needs at least (sizeHint depends on the platform font)
        if (hint.height() > spn::ui::tokens::kDefaultWindowHeight - chromeH ||
            hint.width() > spn::ui::tokens::kDefaultWindowWidth - chromeW) {
            std::fprintf(stderr, "default size self-test failed: page %s needs %dx%d\n", id.c_str(), hint.width(), hint.height());
            ok = false;
        }
    }
    return ok;
}

static bool wheelSelfTest() {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "wheel self-test failed: %s\n", what);
        ok = ok && cond;
    };
    installLook();
    QScrollArea area;
    auto *content = new QWidget;
    auto *lay = new QVBoxLayout(content);
    auto *combo = new QComboBox;
    combo->addItems({QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")});
    auto *spin = new QSpinBox;
    spin->setRange(0, 100);
    spin->setValue(50);
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 100);
    slider->setValue(50);
    lay->addWidget(combo);
    lay->addWidget(spin);
    lay->addWidget(slider);
    auto *filler = new QWidget;
    filler->setMinimumHeight(2000);  // the page is much taller than the viewport
    lay->addWidget(filler);
    area.setWidgetResizable(true);
    area.setWidget(content);
    area.resize(400, 300);
    area.show();
    settle();
    check(area.verticalScrollBar()->maximum() > 0, "the test page scrolls");
    for (QWidget *w : std::initializer_list<QWidget *>{combo, spin, slider}) {
        area.verticalScrollBar()->setValue(0);
        for (int delta : {-120, 120, -240}) {
            const QPointF local = QPointF(w->width() / 2.0, w->height() / 2.0);
            QWheelEvent e(local, w->mapToGlobal(local), QPoint(), QPoint(0, delta), Qt::NoButton, Qt::NoModifier,
                          Qt::ScrollUpdate, false);
            QApplication::sendEvent(w, &e);
        }
        check(combo->currentIndex() == 0, "the wheel does not change the combo box");
        check(spin->value() == 50, "the wheel does not change the spin box");
        check(slider->value() == 50, "the wheel does not change the slider");
        check(area.verticalScrollBar()->value() != 0, "the wheel scrolls the page instead");
    }
    std::printf("wheel self-test: %s\n", ok ? "ok" : "failed");
    return ok;
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
    const int kBuiltin = static_cast<int>(spn::loadButtonModes("", nullptr).modes.size());  // every built-in button mode is listed without a file
    check(page.buttonModeCount() == kBuiltin + 1, "one file plus the built-in button modes");
    check(page.selectButtonMode(QStringLiteral("latch-nav")) && page.catalog().find("latch-nav") &&
              page.catalog().find("latch-nav")->builtin,
          "a built-in button mode is listed");
    check(!page.stageEdit(QStringLiteral("x"), QString(), QStringLiteral("click")) && !page.pending(),
          "a built-in button mode cannot be edited in place");
    check(page.selectButtonMode(QStringLiteral("latch")), "select the file");
    check(page.editCurrent(QStringLiteral("Edited"), QStringLiteral("Changed"), QStringLiteral("key:space")),
          "edit and save in the page");
    QFile f(tmpPath + QStringLiteral("/buttons/latch.conf"));
    check(f.open(QIODevice::ReadOnly), "the file exists");
    const QByteArray text = f.readAll();
    f.close();  // an open handle blocks replacing the file on Windows ("Access is denied")
    check(text.contains("NAME = Edited") && text.contains("DESCRIPTION = Changed") && text.contains("ACTION = key:space"),
          "the file holds the edited values");
    check(!page.pending(), "saved: nothing pending");
    // a key chord is edited and saved like a key: the editor shows the modifier boxes ticked
    check(page.editCurrent(QStringLiteral("Edited"), QStringLiteral("Changed"), QStringLiteral("key:ctrl+0")), "save a chord");
    {
        QFile cf(tmpPath + QStringLiteral("/buttons/latch.conf"));
        const bool opened = cf.open(QIODevice::ReadOnly);
        check(opened && cf.readAll().contains("ACTION = key:ctrl+0"), "the file holds the chord");
        cf.close();  // see above: the next save replaces the file
        settle();
        int ticked = 0;
        for (QCheckBox *cb : page.findChildren<QCheckBox *>())
            if (cb->text() == QLatin1String("ctrl") && cb->isChecked()) ++ticked;
        check(ticked == 1, "the ctrl box is ticked for key:ctrl+0");
    }
    check(page.editCurrent(QStringLiteral("Edited"), QStringLiteral("Changed"), QStringLiteral("key:space")), "back to a plain key");
    // "Cycle through" (CYCLE list): only for the actions Next / Previous joystick mode
    {
        CycleEditorWidget *ce = page.cycleEditor();
        check(ce && !ce->isVisibleTo(&page), "no cycle section for a key action");
        check(page.editCurrent(QStringLiteral("Edited"), QStringLiteral("Changed"), QStringLiteral("mode:next")), "save mode:next");
        settle();
        auto fileText = [&]() {
            QFile cf(tmpPath + QStringLiteral("/buttons/latch.conf"));
            const bool opened = cf.open(QIODevice::ReadOnly);
            return opened ? cf.readAll() : QByteArray();
        };
        check(ce->isVisibleTo(&page) && !ce->model().onlySelected() && !fileText().contains("\nCYCLE"), "mode:next shows the section: all modes");
        check(ce->setChecked(QStringLiteral("timeline"), true) && ce->setChecked(QStringLiteral("normal"), true), "tick two modes");
        page.flush();
        check(fileText().contains("CYCLE = timeline, normal\n") && !page.pending(), "the file holds the CYCLE line");
        check(ce->shownRows().value(0).startsWith(QStringLiteral("1. ")) && ce->shownRows().value(1).startsWith(QStringLiteral("2. ")),
              "the order is shown as numbers");
        check(ce->moveDown(QStringLiteral("timeline")), "move down");
        page.flush();
        check(fileText().contains("CYCLE = normal, timeline\n"), "the file holds the new order");
        check(page.catalog().find("latch") && page.catalog().find("latch")->cycle.size() == 2 && page.catalog().find("latch")->warnings.empty(),
              "the file stays valid");
        // the name edit keeps the list; a joystick mode that is not loaded stays as "missing"
        check(page.editCurrent(QStringLiteral("Edited 2"), QStringLiteral("Changed"), QStringLiteral("mode:next")) &&
                  fileText().contains("CYCLE = normal, timeline\n"),
              "an edit of the name keeps the CYCLE list");
        ce->setOnlySelected(false);
        page.flush();
        check(!fileText().contains("\nCYCLE") && page.catalog().find("latch")->cycle.empty(), "back to all: no CYCLE line");
        check(ce->setChecked(QStringLiteral("timeline"), true), "ticking switches to only these");
        page.flush();
        check(fileText().contains("CYCLE = timeline\n"), "one mode written");
        check(page.editCurrent(QStringLiteral("Edited"), QStringLiteral("Changed"), QStringLiteral("key:space")) &&
                  !fileText().contains("\nCYCLE") && !ce->isVisibleTo(&page),
              "another action drops the CYCLE line and hides the section");
        // built-in button modes: read-only, with the explanation; the editable copy can be edited
        check(page.selectButtonMode(QStringLiteral("cycle-favorites")) && ce->isVisibleTo(&page) &&
                  ce->model().cycle() == std::vector<std::string>({"timeline", "normal"}) &&
                  !ce->setChecked(QStringLiteral("video"), true) && !page.pending(),
              "the built-in cycle-favorites shows its list read-only");
        check(page.createEditableCopy(QStringLiteral("cycle-favorites")) && page.currentId() == QStringLiteral("cycle-favorites"), "editable copy");
        ce = page.cycleEditor();
        check(ce->setChecked(QStringLiteral("normal"), false), "the copy can be edited");
        page.flush();
        QFile cp(tmpPath + QStringLiteral("/buttons/cycle-favorites.conf"));
        check(cp.open(QIODevice::ReadOnly) && cp.readAll().contains("CYCLE = timeline\n"), "the copy holds the changed CYCLE line");
        cp.close();
    }
    // a file that appears in the folder shows up (the page polls the folder stamp)
    QFile second(tmpPath + QStringLiteral("/buttons/second.conf"));
    check(second.open(QIODevice::WriteOnly) && second.write("ACTION = click\n") > 0, "write a second file");
    second.close();
    page.refreshIfChanged();
    check(page.buttonModeCount() == kBuiltin + 2, "a new file shows up in the list");
    QFile::remove(tmpPath + QStringLiteral("/buttons/second.conf"));
    page.refreshIfChanged();
    check(page.buttonModeCount() == kBuiltin + 1, "a removed file disappears from the list");
    return ok;
}

// Accessibility: every input control on every page has an accessible name (screen readers), and the tab
// order reaches the first control of each page. Reports the controls without a name.
static bool accessibilitySelfTest(MainWindow &w) {
    bool ok = true;
    for (const std::string &id : w.pageIds()) {
        w.showPage(QString::fromStdString(id));
        settle();
        QWidget *page = w.currentPage();
        if (!page) continue;
        int controls = 0;
        const QList<QWidget *> all = page->findChildren<QWidget *>();
        for (QWidget *c : all) {
            const bool input = qobject_cast<QComboBox *>(c) || qobject_cast<QLineEdit *>(c) || qobject_cast<QAbstractSpinBox *>(c) ||
                               qobject_cast<QAbstractSlider *>(c) || qobject_cast<QPlainTextEdit *>(c) ||
                               qobject_cast<QListWidget *>(c) || qobject_cast<QTreeWidget *>(c) || qobject_cast<QTreeView *>(c);
            if (!input || !c->isVisibleTo(page)) continue;
            // inner line edits of combo boxes and spin boxes belong to their parent control
            if (qobject_cast<QLineEdit *>(c) && (qobject_cast<QAbstractSpinBox *>(c->parentWidget()) || qobject_cast<QComboBox *>(c->parentWidget())))
                continue;
            if (c->objectName() == QStringLiteral("qt_scrollarea_viewport")) continue;
            ++controls;
            if (qobject_cast<QAbstractScrollArea *>(c) == nullptr && qobject_cast<QAbstractSlider *>(c) &&
                qobject_cast<QScrollBar *>(c))
                continue;  // scroll bars are labelled by the platform
            // The name a screen reader gets: the accessible name, or the label whose buddy the control is.
            QAccessibleInterface *iface = QAccessible::queryAccessibleInterface(c);
            if (!iface || iface->text(QAccessible::Name).isEmpty()) {
                std::fprintf(stderr, "accessibility self-test failed: page %s: %s without an accessible name\n", id.c_str(),
                             c->metaObject()->className());
                ok = false;
            }
        }
        (void)controls;
    }
    return ok;
}

// Extensions page (issue #129): the files of the user with a switch each; off renames "<id>.conf" to
// "<id>.conf.disabled", on renames it back, nothing is overwritten, Remove deletes.
static bool extensionsSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "extensions self-test failed: %s\n", what);
        ok = ok && cond;
    };
    auto write = [&](const QString &rel, const char *text) {
        QFile f(tmpPath + rel);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write(text);
        return true;
    };
    check(write(QStringLiteral("/modes/ext-test.conf"), "NAME = Extension test\nDESCRIPTION = Only for the self-test\nBUTTON_1 = key:space\n") &&
              write(QStringLiteral("/buttons/ext-latch.conf"), "NAME = Extension latch\nACTION = toggle:nav\n"),
          "write the test files");
    ExtensionsPage page(ctx);
    page.show();
    settle();
    check(page.exists(QStringLiteral("joystick"), QStringLiteral("ext-test")) && page.exists(QStringLiteral("button"), QStringLiteral("ext-latch")),
          "both kinds are listed");
    QCheckBox *sw = page.switchOf(QStringLiteral("joystick"), QStringLiteral("ext-test"));
    check(sw && sw->isChecked() && sw->accessibleName().contains(QStringLiteral("Extension test")), "the switch is on and has an accessible name");
    // switching off renames the file; the switch follows the folder
    check(page.setEnabled(QStringLiteral("joystick"), QStringLiteral("ext-test"), false) &&
              !QFile::exists(tmpPath + QStringLiteral("/modes/ext-test.conf")) &&
              QFile::exists(tmpPath + QStringLiteral("/modes/ext-test.conf.disabled")) &&
              !page.isEnabled(QStringLiteral("joystick"), QStringLiteral("ext-test")),
          "switching off renames the file");
    sw = page.switchOf(QStringLiteral("joystick"), QStringLiteral("ext-test"));
    check(sw && !sw->isChecked(), "the switch shows it as off");
    // the daemon's loader no longer sees it, and sees it again when it is on
    check(!spn::loadModes(ctx.modesDir.empty() ? std::string() : ctx.modesDir, nullptr).find("ext-test"), "the loader ignores a switched-off file");
    check(page.setEnabled(QStringLiteral("joystick"), QStringLiteral("ext-test"), true) &&
              QFile::exists(tmpPath + QStringLiteral("/modes/ext-test.conf")) && page.isEnabled(QStringLiteral("joystick"), QStringLiteral("ext-test")),
          "switching on renames it back");
    check(spn::loadModes(ctx.modesDir, nullptr).find("ext-test") != nullptr, "the loader reads it again");
    // never overwrites: a switched-off copy with the same id is in the way
    check(write(QStringLiteral("/buttons/ext-latch.conf.disabled"), "NAME = Another\nACTION = toggle:nav\n"), "write a conflicting file");
    page.refresh();
    check(!page.setEnabled(QStringLiteral("button"), QStringLiteral("ext-latch"), false) && !page.message().isEmpty() &&
              QFile::exists(tmpPath + QStringLiteral("/buttons/ext-latch.conf")),
          "a taken name is refused and the message says so");
    check(QFile::remove(tmpPath + QStringLiteral("/buttons/ext-latch.conf.disabled")), "remove the conflicting file");
    // removing deletes the file, with and without the question
    page.refresh();
    check(page.removeEntry(QStringLiteral("button"), QStringLiteral("ext-latch"), false) &&
              !QFile::exists(tmpPath + QStringLiteral("/buttons/ext-latch.conf")) && !page.exists(QStringLiteral("button"), QStringLiteral("ext-latch")),
          "Remove deletes the file");
    check(page.removeEntry(QStringLiteral("joystick"), QStringLiteral("ext-test"), false) &&
              !QFile::exists(tmpPath + QStringLiteral("/modes/ext-test.conf")),
          "Remove deletes the other file too");
    check(!page.setEnabled(QStringLiteral("joystick"), QStringLiteral("nothing-here"), true), "an unknown file is refused");
    // a file with exactly the text of a built-in joystick mode is a copy of a shipped example: flagged, and the loader ignores it
    if (const spn::BuiltinFile *shipped = spn::builtinModeFiles(); shipped && shipped->id) {
        const QString shippedId = QString::fromUtf8(shipped->id);
        check(write(QStringLiteral("/modes/") + shippedId + QStringLiteral(".conf"), shipped->text), "write a copy of a shipped example");
        page.refresh();
        check(page.isCopyOfShipped(QStringLiteral("joystick"), shippedId), "a copy of a shipped example is flagged");
        check(QFile::remove(tmpPath + QStringLiteral("/modes/") + shippedId + QStringLiteral(".conf")), "remove the copy");
        page.refresh();
    }
    std::printf("extensions self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Mode library: used-by lines, export, rename and the import of a dropped file (the confirmation dialog is skipped).
static bool modeLibrarySelfTest(UiContext ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "mode library self-test failed: %s\n", what);
        ok = ok && cond;
    };
    spn::ui::SettingsModel settings;
    spn::ProfileSet ps;
    const size_t a = ps.findOrAdd(spn::Section::APP, "blender");
    ps.sections[a].kv = {{"MODE", "mine"}, {"BUTTON_2", "toggle:mode:mine"}};
    settings.setProfiles(ps, ctx.configPath);
    ctx.settings = &settings;
    ModesPage page(ctx);
    page.show();
    settle();
    const QStringList uses = page.usedByLines(QStringLiteral("mine"));
    check(uses.size() == 1 && uses[0].startsWith(QStringLiteral("[app:blender]")) && uses[0].contains(QLatin1Char('2')),
          "the profile that uses the mode is listed");
    check(page.usedByLines(QStringLiteral("normal")).isEmpty(), "an unused mode has no profile");
    // every row of the list carries six chips (one per motion of the cap) and a muted flag per chip; the detail
    // panel of the selected joystick mode shows six motions, "Lift / press" fifth, the twist last
    for (const spn::ui::ModeCatalog::ModeEntry &e : page.catalog().modes())
        check(page.rowChips(QString::fromStdString(e.id)).size() == 6, "every list row shows six motion chips");
    check(page.rowChips(QStringLiteral("normal")).value(0).contains(QStringLiteral("Scroll")) &&
              page.rowChips(QStringLiteral("off")).value(0).contains(QStringLiteral("Off")) &&
              page.rowChips(QStringLiteral("normal")).value(4).contains(QStringLiteral("Unused")),
          "the chips say what the motion does in words (English test run)");
    for (const char *id : {"normal", "off", "timeline", "mine"}) {
        check(page.selectMode(QString::fromLatin1(id)), "select a joystick mode");
        settle();
        const QStringList cards = page.motionCardTexts();
        check(cards.size() == 6 && cards.value(0).startsWith(QStringLiteral("Vertical push")) &&
                  cards.value(4).startsWith(QStringLiteral("Lift / press")),
              "the detail panel shows the six motions");
    }
    const QString out = tmpPath + QStringLiteral("/exported-mine.conf");
    check(page.exportModeTo(QStringLiteral("mine"), out) && QFile::exists(out), "export writes a copy");
    // a dropped file goes through the same loader as Import; the id comes from the file name
    const int before = page.modeCount();
    check(page.importPath(out, false) && page.modeCount() == before + 1 && page.selectMode(QStringLiteral("exported-mine")),
          "the exported file can be imported again");
    QFile dropped(tmpPath + QStringLiteral("/dropped-one.conf"));
    check(dropped.open(QIODevice::WriteOnly), "write a file to drop");
    dropped.write("NAME = Dropped\nBUTTON_1 = key:space\n");
    dropped.close();
    check(page.importPath(dropped.fileName(), false) && page.selectMode(QStringLiteral("dropped-one")), "a dropped file is imported");
    QFile bad(tmpPath + QStringLiteral("/bad.conf"));
    check(bad.open(QIODevice::WriteOnly), "write an invalid file");
    bad.write(QByteArray(70000, 'x'));
    bad.close();
    const int n = page.modeCount();
    check(!page.importPath(bad.fileName(), false) && page.modeCount() == n, "an invalid file is refused");
    check(page.renameMode(QStringLiteral("dropped-one"), QStringLiteral("Dropped Two")) &&
              page.selectMode(QStringLiteral("dropped-two")) && !page.selectMode(QStringLiteral("dropped-one")) &&
              QFile::exists(tmpPath + QStringLiteral("/modes/dropped-two.conf")),
          "rename moves the file and the id");
    check(!page.renameMode(QStringLiteral("normal"), QStringLiteral("Other")), "a built-in cannot be renamed");
    // clean up what this test added
    for (const char *f : {"/modes/dropped-two.conf", "/modes/exported-mine.conf"})
        QFile::remove(tmpPath + QString::fromLatin1(f));
    return ok;
}

// Profiles page: sub-tabs, footer outside the scrolling area, search boxes, Revert, and the
// unsaved-changes guard (profile switch, page switch, window close) through its test seam.
static QLineEdit *lineEditNamed(QWidget *root, const QString &name) {
    for (QLineEdit *e : root->findChildren<QLineEdit *>())
        if (e->accessibleName() == name && e->isVisibleTo(root)) return e;
    return nullptr;
}

// Target size (WCAG 2.5.8, issue #104): every visible interactive control of every page is at least 24 x 24 px
// (the controls are 32 to 34 px high by design; this asserts it for all of them, with the window at its default
// size and at 150% font size).
static bool targetSizeSelfTest(MainWindow &w) {
    constexpr int kMin = 24;
    bool ok = true;
    // our own style sheet (Light and Dark) sizes the controls; the System theme leaves them to the platform
    for (spn::ui::Theme theme : {spn::ui::Theme::Light, spn::ui::Theme::Dark}) {
    applyTheme(theme);
    settle();
    for (const std::string &id : w.pageIds()) {
        w.showPage(QString::fromStdString(id));
        settle();
        QWidget *page = w.currentPage();
        if (!page) continue;
        for (QWidget *c : page->findChildren<QWidget *>()) {
            const bool interactive = qobject_cast<QAbstractButton *>(c) || qobject_cast<QComboBox *>(c) ||
                                     qobject_cast<QAbstractSpinBox *>(c) || qobject_cast<QLineEdit *>(c) ||
                                     (qobject_cast<QAbstractSlider *>(c) && !qobject_cast<QScrollBar *>(c));
            if (!interactive || !c->isVisibleTo(page)) continue;
            if (qobject_cast<QLineEdit *>(c) && (qobject_cast<QAbstractSpinBox *>(c->parentWidget()) || qobject_cast<QComboBox *>(c->parentWidget())))
                continue;  // the inner line edit belongs to its parent control
            if (qobject_cast<QToolButton *>(c) && qobject_cast<QTabBar *>(c->parentWidget()))
                continue;  // the scroll arrows of a tab bar that is too narrow for its tabs: part of the platform's tab bar
            if (c->width() < kMin || c->height() < kMin) {
                std::fprintf(stderr, "target size self-test failed: page %s: %s \"%s\" is %d x %d px\n", id.c_str(), c->metaObject()->className(),
                             c->accessibleName().toUtf8().constData(), c->width(), c->height());
                ok = false;
            }
        }
    }
    }
    applyTheme(spn::ui::Theme::System);
    settle();
    std::printf("target size self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Key picker (tree in columns): the popup opens with the current key chosen, arrows move like in the Finder column
// view, Enter chooses and emits the stored value, Esc closes without a choice; the chord editor keeps its text.
static bool keyTreePickerSelfTest(QWidget *host) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "key tree picker self-test failed: %s\n", what);
        ok = ok && cond;
    };
    auto *picker = makeKeyTreePicker(QStringLiteral("pageup"), host);
    picker->setAccessibleName(QStringLiteral("Test key"));
    picker->show();
    QStringList emitted;
    QObject::connect(picker, &KeyTreePicker::valueChosen, [&emitted](const QString &v) { emitted << v; });
    check(picker->text() == T("Page up") && picker->value() == QStringLiteral("pageup"), "the button shows the current choice");
    check(!picker->accessibleName().isEmpty() && picker->accessibleDescription().contains(QStringLiteral(">")), "accessible name and path");
    KeyTreePicker *active = picker;  // the picker whose popup gets the keys
    auto press = [&](int key) {
        QWidget *target = active->popupView();  // the column view has the focus; Esc bubbles up to the popup
        QKeyEvent down(QEvent::KeyPress, key, Qt::NoModifier);
        QApplication::sendEvent(target, &down);
        QKeyEvent up(QEvent::KeyRelease, key, Qt::NoModifier);
        QApplication::sendEvent(target, &up);
    };
    auto currentText = [&]() { return active->popupView() ? active->popupView()->currentIndex().data().toString() : QString(); };
    picker->openPopup();
    check(picker->popupOpen() && picker->popupView() != nullptr, "the popup opens");
    if (!picker->popupOpen() || !picker->popupView()) return false;
    check(currentText() == T("Page up"), "the current choice is preselected");
    press(Qt::Key_Down);
    check(currentText() == T("Page down"), "Down moves in the column");
    press(Qt::Key_Return);
    check(!picker->popupOpen() && emitted == QStringList{QStringLiteral("pagedown")} && picker->value() == QStringLiteral("pagedown") &&
              picker->text() == T("Page down"),
          "Enter chooses, closes and emits the stored value");
    // Left goes back to the subcategory, Up/Down change it, Right opens it again
    picker->openPopup();
    press(Qt::Key_Left);
    check(currentText() == T("Navigation"), "Left goes back to the category column");
    press(Qt::Key_Up);
    check(currentText() == T("Function keys"), "Up moves to the previous subcategory");
    press(Qt::Key_Right);
    check(currentText() == T("F1"), "Right opens the subcategory");
    press(Qt::Key_Return);
    check(emitted.size() == 2 && emitted.last() == QStringLiteral("f1") && !picker->popupOpen(), "Enter on a leaf of another subcategory");
    // Esc closes without a choice; Enter on a branch only opens it
    picker->openPopup();
    press(Qt::Key_Escape);
    check(!picker->popupOpen() && emitted.size() == 2, "Esc closes without a choice");
    picker->openPopup();
    press(Qt::Key_Left);
    press(Qt::Key_Return);
    check(emitted.size() == 2, "Enter on a branch chooses nothing");
    picker->closePopup();
    // the chord editor: same stored text as before
    std::string chord = "ctrl+shift+pageup";
    int changes = 0;
    QWidget *cp = chordPicker(host, host, &chord, QStringLiteral("Chord"), [&changes] { ++changes; });
    cp->show();
    auto *kp = cp->findChild<KeyTreePicker *>();
    check(kp != nullptr && kp->value() == QStringLiteral("pageup"), "the chord editor shows the key of the chord");
    if (kp) {
        active = kp;
        kp->openPopup();
        press(Qt::Key_Down);
        press(Qt::Key_Return);
        check(chord == "ctrl+shift+pagedown" && changes == 1, "the chord editor stores modifiers and key as before");
    }
    delete cp;
    delete picker;
    std::printf("key tree picker self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Keyboard (issue #104): the tab order of every page follows the reading order (no jump back up the page), every "i"
// info button can be reached with Tab and opens its popover with the keyboard, and Esc closes the popover.
static bool keyboardSelfTest(MainWindow &w) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "keyboard self-test failed: %s\n", what);
        ok = ok && cond;
    };
    constexpr int kRowTolerance = 16;  // controls whose tops differ by less are one row
    constexpr int kColumnGap = 160;    // controls whose left edges differ by more are in different columns
    int infoButtons = 0;
    // Global: a profile whose output is off hides the joystick mode and its info button
    if (ProfilesPage *pp = w.profilesPage()) pp->selectSection(0);
    for (const std::string &id : w.pageIds()) {
        w.showPage(QString::fromStdString(id));
        settle();
        QWidget *page = w.currentPage();
        if (!page) continue;
        // the Tab chain restricted to the controls of this page that take Tab focus
        std::vector<QWidget *> chain;
        QWidget *start = page->focusWidget() ? page->focusWidget() : page;
        for (QWidget *c = start->nextInFocusChain(); c && c != start; c = c->nextInFocusChain())
            if (page->isAncestorOf(c) && c->isVisibleTo(page) && c->isEnabled() && (c->focusPolicy() & Qt::TabFocus) &&
                !qobject_cast<QScrollBar *>(c) && c->objectName() != QStringLiteral("qt_scrollarea_viewport") &&
                !(qobject_cast<QLineEdit *>(c) && (qobject_cast<QAbstractSpinBox *>(c->parentWidget()) || qobject_cast<QComboBox *>(c->parentWidget()))))
                chain.push_back(c);
        if (chain.size() > 1) {
            // The chain is circular: start it at the control that is first on the page (top, then left).
            auto pos = [page](QWidget *c) { return c->mapTo(page, QPoint(0, 0)); };
            size_t first = 0;
            for (size_t i = 1; i < chain.size(); ++i)
                if (pos(chain[i]).y() < pos(chain[first]).y() - kRowTolerance ||
                    (std::abs(pos(chain[i]).y() - pos(chain[first]).y()) <= kRowTolerance && pos(chain[i]).x() < pos(chain[first]).x()))
                    first = i;
            std::rotate(chain.begin(), chain.begin() + long(first), chain.end());
            for (size_t i = 1; i < chain.size(); ++i) {
                const QPoint p0 = pos(chain[i - 1]), p1 = pos(chain[i]);
                // a move to another column (a pane to the right) may start at its top again
                const bool sameColumn = std::abs(p1.x() - p0.x()) < kColumnGap;
                const bool jumpsUp = sameColumn && p1.y() < p0.y() - kRowTolerance;
                const bool backwardsInRow = std::abs(p1.y() - p0.y()) <= kRowTolerance && p1.x() < p0.x() - 2;
                if (jumpsUp || backwardsInRow) {
                    std::fprintf(stderr, "keyboard self-test failed: page %s: Tab goes from %s \"%s\" (%d,%d) to %s \"%s\" (%d,%d)\n", id.c_str(),
                                 chain[i - 1]->metaObject()->className(), chain[i - 1]->accessibleName().toUtf8().constData(), p0.x(), p0.y(),
                                 chain[i]->metaObject()->className(), chain[i]->accessibleName().toUtf8().constData(), p1.x(), p1.y());
                    ok = false;
                }
            }
        }
        for (QToolButton *b : page->findChildren<QToolButton *>()) {
            if (b->objectName() != QStringLiteral("infoButton") || !b->isVisibleTo(page)) continue;
            ++infoButtons;
            check(b->focusPolicy() & Qt::TabFocus, "an info button takes Tab focus");
            check(!b->accessibleName().isEmpty(), "an info button has an accessible name");
        }
    }
    check(infoButtons > 0, "there are info buttons to test");
    // an info popover: opens by the keyboard (click() is what Space and Enter do), has a name, Esc closes it
    w.showPage(QStringLiteral("profiles"));
    if (ProfilesPage *pp = w.profilesPage()) pp->selectSection(0);
    settle();
    QToolButton *info = nullptr;
    for (QToolButton *b : w.currentPage()->findChildren<QToolButton *>())
        if (b->objectName() == QStringLiteral("infoButton") && b->isVisibleTo(w.currentPage())) info = b;
    check(info != nullptr, "the Profiles page has an info button");
    if (info) {
        info->click();
        settle();
        QWidget *popup = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (top->objectName() == QStringLiteral("infoPopup") && top->isVisible()) popup = top;
        // On a real Wayland session the popover often stays hidden: a grabbing popup needs an input
        // serial from the compositor, and a self-test never receives real input, so the grab is
        // refused (qpa.wayland: "Failed to create grabbing popup"). X11 and the offscreen platform
        // have no serial and run the check in full (CI), and on Wayland everything else on the
        // popover still runs whenever it maps. See issue #193.
        if (!popup && QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
            std::printf("keyboard self-test: info popover open check skipped on Wayland (no input serial for the grabbing popup)\n");
        } else {
            check(popup != nullptr && !InfoPopup::openText().isEmpty(), "the info button opens its popover");
        }
        if (popup) {
            check(!popup->accessibleName().isEmpty(), "the popover has an accessible name");
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(popup, &esc);
            settle();
            check(InfoPopup::openText().isEmpty(), "Esc closes the popover");
        }
    }
    std::printf("keyboard self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Sidebar tabs (issue #104): the current tab shows a 2 px focus ring while the bar has the keyboard focus, and
// the arrow keys move between the categories.
static bool sidebarFocusSelfTest(MainWindow &w) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "sidebar focus self-test failed: %s\n", what);
        ok = ok && cond;
    };
    QPalette lowAccent;  // an accent without 3:1 against the window: the text colour is used for the ring
    lowAccent.setColor(QPalette::Window, QColor(0x20, 0x20, 0x24));
    lowAccent.setColor(QPalette::WindowText, QColor(0xF2, 0xF2, 0xF2));
    lowAccent.setColor(QPalette::Highlight, QColor(0x30, 0x30, 0x40));
    const QList<QPalette> palettes = {QApplication::palette(), lowAccent};
    for (const QPalette &pal : palettes) {
        WestTabStyle style;
        const QColor ring = WestTabStyle::focusRingColor(pal);
        auto distance = [](const QColor &a, const QColor &b) {
            return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) + std::abs(a.blue() - b.blue());
        };
        auto edgePixel = [&](bool focus) {
            QImage img(200, 60, QImage::Format_ARGB32);
            img.fill(pal.color(QPalette::Window));
            QPainter painter(&img);
            QStyleOptionTab opt;
            opt.rect = QRect(0, 0, 200, 60);
            opt.palette = pal;
            opt.state = QStyle::State_Enabled | QStyle::State_Selected | (focus ? QStyle::State_HasFocus : QStyle::State_None);
            style.drawControl(QStyle::CE_TabBarTabShape, &opt, &painter, nullptr);
            painter.end();
            return img.pixelColor(1, 30);
        };
        check(distance(edgePixel(true), ring) < 60, "the current tab with focus is drawn with the ring colour");
        check(distance(edgePixel(false), ring) >= 60, "without focus there is no ring");
        const auto rgb = [](const QColor &c) { return spn::ui::Rgb{uint8_t(c.red()), uint8_t(c.green()), uint8_t(c.blue())}; };
        check(spn::ui::contrastRatio(rgb(ring), rgb(pal.color(QPalette::Window))) >= 3.0, "the ring has 3:1 against the window");
    }
    QTabBar *bar = nullptr;
    for (QTabBar *b : w.findChildren<QTabBar *>())
        if (b->shape() == QTabBar::RoundedWest) bar = b;
    check(bar != nullptr, "the sidebar tab bar exists");
    if (bar) {
        check(bar->focusPolicy() != Qt::NoFocus, "the sidebar tab bar can take the keyboard focus");
        bar->setCurrentIndex(0);
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(bar, &down);
        check(bar->currentIndex() == 1, "the Down key moves to the next category");
        QKeyEvent up(QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier);
        QApplication::sendEvent(bar, &up);
        check(bar->currentIndex() == 0, "the Up key moves back");
        QKeyEvent end(QEvent::KeyPress, Qt::Key_End, Qt::NoModifier);
        QApplication::sendEvent(bar, &end);
        check(bar->currentIndex() == bar->count() - 1, "End moves to the last category");
        QKeyEvent past(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
        QApplication::sendEvent(bar, &past);
        check(bar->currentIndex() == bar->count() - 1, "Down at the last category stays there");
        QKeyEvent home(QEvent::KeyPress, Qt::Key_Home, Qt::NoModifier);
        QApplication::sendEvent(bar, &home);
        check(bar->currentIndex() == 0, "Home moves to the first category");
    }
    std::printf("sidebar focus self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Joystick mode editor sub-tabs: Settings, Buttons, Overrides; no Save or Revert button, the last tab is kept
// over a rebuild.
static bool modeEditorTabsSelfTest(const UiContext &ctx) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "mode editor tabs self-test failed: %s\n", what);
        ok = ok && cond;
    };
    ModesPage page(ctx);
    page.show();
    settle();
    check(page.selectMode(QStringLiteral("mine")) && page.openEditor() && page.editorWidget(), "the editor opens");
    ModeEditorWidget *ed = page.editorWidget();
    check(ed->tabIds() == QStringList({QStringLiteral("motions"), QStringLiteral("settings"), QStringLiteral("buttons"), QStringLiteral("overrides")}),
          "Motions and buttons, Settings, Buttons and Overrides");
    check(ed->currentTabId() == QStringLiteral("motions"), "the first tab is Motions and buttons");
    for (const QString &name : {QStringLiteral("Save"), QStringLiteral("Revert")})
        for (QPushButton *c : ed->findChildren<QPushButton *>()) check(c->text() != name, "no Save or Revert button");
    check(!ed->pending(), "an unchanged joystick mode has nothing to write");
    ed->editor().form().overrides.emplace_back("SCROLL_V_SENS", "abc");
    ed->edited(true);  // refused: the invalid value is shown, nothing is scheduled
    check(!ed->pending() && !ed->problemText().isEmpty(), "an invalid edit is shown and not written");
    check(ed->selectTab(QStringLiteral("buttons")), "select the Buttons tab");
    ed->editor().form().overrides.pop_back();  // the user fixed it: the form is valid again
    ed->edited(false);
    settle();
    check(ed->currentTabId() == QStringLiteral("buttons") && ed->problemText().isEmpty(), "the tab stays");
    ed->flush();
    ed->selectTab(QStringLiteral("overrides"));
    QLineEdit *ov = lineEditNamed(ed, QStringLiteral("SCROLL_V_SENS"));
    check(ov && ov->text() == QStringLiteral("2"), "the Overrides tab shows the saved override");
    return ok;
}

// Motions and buttons tab (issue #143): the picture, selection by click and by keyboard, assigning through the
// inspector (split and collapse of push and tilt), reverse direction, lift/press, a button, and that every
// edit is written by the autosave.
static bool motionEditorSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "motion editor self-test failed: %s\n", what);
        ok = ok && cond;
    };
    auto fileText = [&](const QString &rel) {
        QFile f(tmpPath + rel);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    auto waitFor = [](const std::function<bool()> &done, int ms) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    };
    const QString rel = QStringLiteral("/modes/motionsdemo.conf");
    {
        QFile f(tmpPath + rel);
        if (!f.open(QIODevice::WriteOnly)) return false;
        f.write("# demo\nNAME = Motions demo\nV_ACTION = wheel_v\nBUTTON_1 = key:space\n");
    }
    ModesPage page(ctx);
    page.show();
    settle();
    page.refreshIfChanged();
    check(page.selectMode(QStringLiteral("motionsdemo")) && page.openEditor() && page.editorWidget(), "the editor opens");
    ModeEditorWidget *ed = page.editorWidget();
    if (!ed) return false;
    auto me = [&]() { return ed->motionEditor(); };
    auto radio = [&](const QString &label) -> QToolButton * {  // an action tile of the inspector
        for (QToolButton *r : me()->inspector()->findChildren<QToolButton *>())
            if (r->accessibleName() == label && r->isCheckable()) return r;
        return nullptr;
    };
    auto sel = [&](const QString &id) {
        const bool r = me()->select(id);
        settle();  // widgets added to a shown layout are shown by a queued call
        return r;
    };
    auto settleWrite = [&] { settle(); return waitFor([&] { return !ed->pending(); }, 3000); };
    check(ed->currentTabId() == QStringLiteral("motions") && me() && me()->picture(), "the picture is the first tab");
    // six motions and the buttons of the device (no device attached: a generic grid from the settings)
    int motions = 0, buttons = 0;
    for (const MotionPicture::Item &it : me()->picture()->items()) (it.id.startsWith(QLatin1String("m:")) ? motions : buttons)++;
    check(motions == 6 && buttons >= 1, "six motions and the buttons are drawn");
    for (const MotionPicture::Item &it : me()->picture()->items()) check(!it.text.isEmpty() && it.rect.width() >= 24 && it.rect.height() >= 24, "every item has words and a 24 px target");
    for (int i = 0; i < me()->picture()->items().size(); ++i)
        for (int j = 0; j < i; ++j)
            check(!me()->picture()->items()[i].rect.intersects(me()->picture()->items()[j].rect), "no two items overlap");

    check(me()->tourText().startsWith(QStringLiteral("Step 1 of 4")), "the guided tour starts the first time the tab opens");
    // select by click
    {
        MotionPicture *pic = me()->picture();
        const QPoint at = pic->itemRect(QStringLiteral("m:h_tilt")).center();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(at), QPointF(pic->mapToGlobal(at)), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(pic, &press);
        check(me()->selectedId() == QStringLiteral("m:h_tilt") && me()->inspectorTitle().contains(QStringLiteral("Horizontal tilt")), "a click selects a motion");
        check(me()->tourText().startsWith(QStringLiteral("Step 2 of 4")), "without a device a click completes step 1 of the tour");
    }
    // select by keyboard: arrows move between the items, Enter goes to the inspector
    {
        MotionPicture *pic = me()->picture();
        pic->setCurrent(QStringLiteral("m:v_push"));
        pic->setFocus();
        auto key = [&](int k) {
            QKeyEvent e(QEvent::KeyPress, k, Qt::NoModifier);
            QApplication::sendEvent(pic, &e);
        };
        key(Qt::Key_Right);
        check(me()->selectedId() == QStringLiteral("m:v_tilt"), "the right arrow goes to the tilt");
        key(Qt::Key_Down);
        check(me()->selectedId() == QStringLiteral("m:h_tilt"), "the down arrow goes down");
        key(Qt::Key_Left);
        key(Qt::Key_Up);
        check(me()->selectedId() == QStringLiteral("m:v_push"), "and back");
        key(Qt::Key_Return);
        settle();
    }

    // assign: tilt keys -> the channel splits; push the same -> back to the unsplit key
    sel(QStringLiteral("m:v_tilt"));
    QToolButton *keys = radio(QStringLiteral("Other keys"));
    check(keys != nullptr, "the inspector offers the action cards");
    if (keys) keys->click();
    check(settleWrite() && fileText(rel).contains(QStringLiteral("V_TILT_ACTION = keys:up,down")) && fileText(rel).contains(QStringLiteral("V_PUSH_ACTION = wheel_v")),
          "assigning keys to the tilt splits the channel and keeps the push");
    check(me()->tourText().startsWith(QStringLiteral("Step 3 of 4")), "assigning an action completes step 2 of the tour");
    check(fileText(rel).contains(QStringLiteral("# demo")), "comments stay");
    check(me()->selectedId() == QStringLiteral("m:v_tilt"), "the selection survives the rebuild");
    sel(QStringLiteral("m:v_push"));
    keys = radio(QStringLiteral("Other keys"));
    if (keys) keys->click();
    check(settleWrite() && fileText(rel).contains(QStringLiteral("V_ACTION = keys:up,down")) && !fileText(rel).contains(QStringLiteral("V_TILT_ACTION")) &&
              !fileText(rel).contains(QStringLiteral("V_PUSH_ACTION")),
          "the same action on push and tilt is the unsplit key");
    // the Settings tab shows the same
    {
        ed->selectTab(QStringLiteral("settings"));
        settle();
        QComboBox *vk = nullptr;
        for (QComboBox *cb : ed->findChildren<QComboBox *>())
            if (cb->accessibleName() == QStringLiteral("Vertical push") && cb->isVisibleTo(ed)) vk = cb;
        check(vk && vk->currentData().toInt() == int(spn::ChannelActionType::Keys), "the Settings tab shows the same action");
        ed->selectTab(QStringLiteral("motions"));
        settle();
    }
    // reverse direction
    sel(QStringLiteral("m:v_push"));
    QCheckBox *rev = nullptr;
    for (QCheckBox *cb : me()->inspector()->findChildren<QCheckBox *>())
        if (cb->accessibleName() == QStringLiteral("Vertical push: Reverse direction")) rev = cb;
    check(rev != nullptr, "the inspector has a reverse switch");
    if (rev) rev->click();
    check(settleWrite() && fileText(rel).contains(QStringLiteral("SCROLL_V_SIGN = -1")), "reverse writes the sign");
    sel(QStringLiteral("m:v_push"));
    for (QCheckBox *cb : me()->inspector()->findChildren<QCheckBox *>())
        if (cb->accessibleName() == QStringLiteral("Vertical push: Reverse direction")) rev = cb;
    check(rev && rev->isChecked(), "and the switch shows it");
    if (rev) rev->click();
    check(settleWrite() && !fileText(rel).contains(QStringLiteral("SCROLL_V_SIGN")), "switching it off removes the line");
    // lift / press: a warning in words, and it joins a channel mix
    sel(QStringLiteral("m:lift"));
    {
        bool warned = false;
        for (QLabel *l : me()->inspector()->findChildren<QLabel *>()) warned = warned || l->text().contains(QStringLiteral("false triggers"));
        check(warned, "lift / press says in words that it is prone to false triggers");
        QComboBox *where = nullptr;
        for (QComboBox *cb : me()->inspector()->findChildren<QComboBox *>())
            if (cb->accessibleName() == QStringLiteral("Add lift / press to")) where = cb;
        check(where != nullptr, "lift / press can be added to a motion");
        if (where) {
            where->setCurrentIndex(1);
            emit where->activated(1);
        }
        check(settleWrite() && fileText(rel).contains(QStringLiteral("SCROLL_V_MIX = z + rx + y")), "it is written as a term of the channel mix");
        check(!ed->problemText().isEmpty() == false, "the result is valid");
    }
    // back to not used
    sel(QStringLiteral("m:lift"));
    {
        QComboBox *where = nullptr;
        for (QComboBox *cb : me()->inspector()->findChildren<QComboBox *>())
            if (cb->accessibleName() == QStringLiteral("Add lift / press to")) where = cb;
        if (where) {
            where->setCurrentIndex(0);
            emit where->activated(0);
        }
        check(settleWrite() && !fileText(rel).contains(QStringLiteral("SCROLL_V_MIX")), "not used removes the mix again");
    }
    // the tiles: the lit one is what the motion does, and a ready choice writes its keys
    sel(QStringLiteral("m:v_push"));
    {
        QToolButton *lit = radio(QStringLiteral("Arrow keys"));
        check(lit && lit->isChecked(), "the tile of the motion's action is lit (up / down on a vertical motion: the arrow keys)");
        QToolButton *pages = radio(QStringLiteral("Page up / Page down"));
        check(pages != nullptr && !pages->isChecked(), "a ready choice for page up and down");
        if (pages) pages->click();
        check(settleWrite() && fileText(rel).contains(QStringLiteral("V_PUSH_ACTION = keys:pageup,pagedown")) && fileText(rel).contains(QStringLiteral("V_TILT_ACTION = keys:up,down")),
              "it writes the keys for the push; the tilt keeps its arrow keys");
        sel(QStringLiteral("m:v_push"));
        pages = radio(QStringLiteral("Page up / Page down"));
        check(pages && pages->isChecked(), "and is lit afterwards");
        QToolButton *arrows = radio(QStringLiteral("Arrow keys"));
        if (arrows) arrows->click();
        check(settleWrite() && fileText(rel).contains(QStringLiteral("V_ACTION = keys:up,down")) && !fileText(rel).contains(QStringLiteral("V_PUSH_ACTION")),
              "arrow keys on a vertical motion are up and down, and push and tilt are one again");
    }
    // press a key instead of choosing it: "Press a key..." takes the key and the modifiers
    {
        sel(QStringLiteral("m:v_push"));
        auto captureButton = [&](const QString &dir) -> KeyCaptureButton * {
            for (KeyCaptureButton *b : me()->inspector()->findChildren<KeyCaptureButton *>())
                if (b->accessibleName().contains(dir + QStringLiteral(": Press a key"))) return b;
            return nullptr;
        };
        KeyCaptureButton *neg = captureButton(QStringLiteral("negative"));
        check(neg != nullptr && captureButton(QStringLiteral("positive")) != nullptr, "each direction has a Press a key button");
        if (neg) {
            neg->click();
            check(neg->capturing() && neg->text().contains(QStringLiteral("Press the key")), "clicking it waits for the key, in words");
            QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            neg->press(&esc);
            check(!neg->capturing(), "Esc cancels");
            neg->click();
            QKeyEvent ctrlOnly(QEvent::KeyPress, Qt::Key_Control, Qt::ControlModifier);
            neg->press(&ctrlOnly);
            check(neg->capturing(), "a modifier alone keeps waiting");
#ifdef Q_OS_MACOS
            const Qt::KeyboardModifier ctrlKey = Qt::MetaModifier;  // Qt swaps Ctrl and Cmd on macOS; the field swaps them back
#else
            const Qt::KeyboardModifier ctrlKey = Qt::ControlModifier;
#endif
            QKeyEvent combo(QEvent::KeyPress, Qt::Key_PageUp, ctrlKey);
            neg->press(&combo);
        }
        check(settleWrite() && fileText(rel).contains(QStringLiteral("V_ACTION = keys:ctrl+pageup,down")), "the pressed combination is written");
        {   // the function keys up to F24 are known (F13 to F24 for macros)
            for (int n : {1, 12, 13, 24}) {
                QKeyEvent fk(QEvent::KeyPress, Qt::Key_F1 + n - 1, Qt::NoModifier);
                check(captureKeyName(&fk) == "f" + std::to_string(n), "the key F1 to F24 is known");
            }
        }
        {   // the complete list: keypad, punctuation, system and media keys are known
            struct K { int qt; Qt::KeyboardModifiers m; const char *name; };
            const K listed[] = {{Qt::Key_5, Qt::KeypadModifier, "kp_5"}, {Qt::Key_Enter, Qt::KeypadModifier, "kp_enter"},
                              {Qt::Key_Comma, Qt::NoModifier, "comma"}, {Qt::Key_QuoteLeft, Qt::NoModifier, "grave"},
                              {Qt::Key_CapsLock, Qt::NoModifier, "caps_lock"}, {Qt::Key_VolumeUp, Qt::NoModifier, "volume_up"},
                              {Qt::Key_MediaNext, Qt::NoModifier, "media_next"}};
            for (const K &k : listed) {
                QKeyEvent ev(QEvent::KeyPress, k.qt, k.m);
                check(captureKeyName(&ev) == k.name, "the key from the complete list is known");
            }
        }
        sel(QStringLiteral("m:v_push"));
        neg = captureButton(QStringLiteral("negative"));
        if (neg) {
            neg->click();
            QKeyEvent caps(QEvent::KeyPress, Qt::Key_Help, Qt::NoModifier);
            neg->press(&caps);
            check(!neg->capturing(), "a key that cannot be sent ends the capture");
            bool said = false;
            for (QLabel *l : me()->inspector()->findChildren<QLabel *>()) said = said || (l->text().contains(QStringLiteral("cannot send this key")) && l->isVisibleTo(me()));
            settle();
            for (QLabel *l : me()->inspector()->findChildren<QLabel *>()) said = said || (l->text().contains(QStringLiteral("cannot send this key")) && !l->isHidden());
            check(said, "and says so in words");
        }
        check(fileText(rel).contains(QStringLiteral("V_ACTION = keys:ctrl+pageup,down")), "the chord stays as it was");
        // back to up / down for the steps that follow: push and tilt both get the arrow keys, which is one action again
        QToolButton *arrows = radio(QStringLiteral("Arrow keys"));
        if (arrows) arrows->click();
        settleWrite();
        sel(QStringLiteral("m:v_tilt"));
        arrows = radio(QStringLiteral("Arrow keys"));
        if (arrows) arrows->click();
        check(settleWrite() && fileText(rel).contains(QStringLiteral("V_ACTION = keys:up,down")) && !fileText(rel).contains(QStringLiteral("V_PUSH_ACTION")), "back to the arrow keys");
    }
    {   // drag and drop: a drop on another motion swaps, with Ctrl it copies; lift / press and buttons take no part
        MotionPicture *pic = me()->picture();
        check(pic->canDrag(QStringLiteral("m:v_push")) && !pic->canDrag(QStringLiteral("m:lift")) && !pic->canDrag(QStringLiteral("b:1")),
              "only motions with an action can be dragged");
        pic->dropOn(QStringLiteral("m:v_push"), QStringLiteral("m:twist"), true);
        check(settleWrite() && fileText(rel).contains(QStringLiteral("TWIST_ACTION = keys:up,down")), "a copy drop gives the target the action");
        check(fileText(rel).contains(QStringLiteral("V_ACTION = keys:up,down")), "and the source keeps it");
        const QString before = fileText(rel);
        pic->dropOn(QStringLiteral("m:twist"), QStringLiteral("m:h_push"), false);
        check(settleWrite() && fileText(rel) != before, "a swap drop changes both motions");
        pic->dropOn(QStringLiteral("m:twist"), QStringLiteral("m:h_push"), false);
        check(settleWrite() && fileText(rel) == before, "and dropping again swaps them back");
        pic->dropOn(QStringLiteral("m:lift"), QStringLiteral("m:v_push"), false);
        pic->dropOn(QStringLiteral("m:v_push"), QStringLiteral("m:v_push"), false);
        check(settleWrite() && fileText(rel) == before, "lift / press and a drop on itself change nothing");
    }
    // the live sentence names the keys and a rate; every element has a "?" popover
    sel(QStringLiteral("m:v_tilt"));
    check(me()->liveSentence().contains(QStringLiteral("Vertical tilt: presses Up or Down, about")), "the sentence under the picture says what the motion does");
    {
        QToolButton *help = nullptr;
        for (QToolButton *tb : me()->inspector()->findChildren<QToolButton *>())
            if (tb->text() == QStringLiteral("?") && tb->accessibleName().contains(QStringLiteral("Vertical tilt"))) help = tb;
        check(help != nullptr, "the motion has a ? button");
        if (help) {
            help->click();
            check(InfoPopup::openText().contains(QStringLiteral("axis rx")) && InfoPopup::openText().contains(QStringLiteral("V_TILT_ACTION")),
                  "the popover says what it is, the axis and the file key");
            for (QWidget *w : QApplication::topLevelWidgets())
                if (w->objectName() == QLatin1String("infoPopup")) w->close();
        }
        int helps = 0;
        for (QToolButton *tb : me()->inspector()->findChildren<QToolButton *>()) helps += tb->text() == QStringLiteral("?");
        check(helps >= 1, "the motion has a ? button");
    }
    // a template fills the picture; a motion that does nothing gets a hint with its fix
    {
        QComboBox *tpl = nullptr;
        for (QComboBox *cb : me()->findChildren<QComboBox *>())
            if (cb->accessibleName() == QStringLiteral("Start from a template")) tpl = cb;
        check(tpl != nullptr && tpl->count() == 6, "the template gallery lists five templates");
        if (tpl) {
            const int i = tpl->findText(QStringLiteral("Timeline"));
            check(i >= 0, "a Timeline template");
            emit tpl->activated(i);
        }
        check(settleWrite() && fileText(rel).contains(QStringLiteral("V_ACTION = none")) && fileText(rel).contains(QStringLiteral("TWIST_ACTION = step:left,right")) &&
                  !fileText(rel).contains(QStringLiteral("V_TILT_ACTION")),
              "the template writes the channels and leaves the split");
        bool hint = false;
        for (const QString &h : me()->hintTexts()) hint = hint || h == QStringLiteral("Vertical push does nothing in this joystick mode");
        check(hint, "a motion that does nothing is named in a hint");
        QPushButton *fix = nullptr;
        for (QPushButton *b : me()->findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Assign it") && b->accessibleName().contains(QStringLiteral("Vertical push"))) fix = b;
        check(fix != nullptr, "the hint offers the fix as a button");
        if (fix) fix->click();
        check(me()->selectedId() == QStringLiteral("m:v_push"), "the fix selects the motion");
        // back to the wheel for the following steps
        QToolButton *wheel = radio(QStringLiteral("Scroll up and down"));
        settle();
        wheel = radio(QStringLiteral("Scroll up and down"));
        if (wheel) wheel->click();
        check(settleWrite() && fileText(rel).contains(QStringLiteral("V_PUSH_ACTION = wheel_v")) && fileText(rel).contains(QStringLiteral("V_TILT_ACTION = none")),
              "scroll on the push splits it from the tilt, which is still off");
    }
    // Detect by moving: the state in words, the motion you move and the button you press are selected
    {
        me()->setDetect(true);
        check(me()->detecting() && me()->detectText().contains(QStringLiteral("Start the daemon to detect by moving")), "Detect says in words that the daemon is needed");
        spn::RawAxes rest{};
        spn::RawAxes tilt{};
        tilt[spn::AX_RZ] = 300;
        me()->injectSample(rest, 0);
        for (int i = 0; i < 6; ++i) me()->injectSample(tilt, 0);
        check(me()->selectedId() == QStringLiteral("m:h_tilt"), "moving the cap selects the motion");
        sel(me()->selectedId());
        for (int i = 0; i < 6; ++i) me()->injectSample(rest, 0);
        me()->injectSample(rest, 0x1);
        check(me()->selectedId() == QStringLiteral("b:1"), "pressing a button selects it");
        me()->setDetect(false);
        check(!me()->detecting() && me()->detectText().isEmpty(), "switching it off clears the state");
        me()->injectSample(tilt, 0);  // ignored while off
        check(me()->selectedId() == QStringLiteral("b:1"), "an off Detect selects nothing");
    }
    // Try this joystick mode: global, with the way back
    {
        QPushButton *tryBtn = nullptr;
        for (QPushButton *b : me()->findChildren<QPushButton *>())
            if (b->text() == QStringLiteral("Try this joystick mode")) tryBtn = b;
        check(tryBtn != nullptr, "Try this joystick mode exists");
        if (tryBtn) tryBtn->click();
        settle();
        check(me()->tryText().contains(QStringLiteral("Trying: Motions demo on all profiles")), "Trying says it in words, on all profiles");
        QPushButton *back = nullptr;
        for (QPushButton *b : me()->findChildren<QPushButton *>())
            if (b->text().startsWith(QStringLiteral("Back to ")) && !b->isHidden()) back = b;
        check(back != nullptr, "and offers the way back");
        check(me()->tourText().contains(QStringLiteral("That is all")), "trying completes the tour");
        check(spn::ui::loadUiProgress(spn::ui::uiProgressPath(ctx.configPath)).motionTourSeen, "a finished tour is not shown again");
    }
    // a button: the inspector is the shared button row
    sel(QStringLiteral("b:1"));
    check(me()->inspectorTitle().contains(QStringLiteral("Button 1")), "a button is selected");
    check(!me()->inspector()->findChildren<QComboBox *>().isEmpty(), "the inspector has the shared button editor");
    QFile::remove(tmpPath + rel);
    page.refreshIfChanged();
    std::printf("motion editor self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Editor page (issue #143): the visual editor at full width with a joystick mode chooser; a built-in joystick mode is
// copied before it is edited.
static bool editorPageSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "editor page self-test failed: %s\n", what);
        ok = ok && cond;
    };
    auto waitFor = [](const std::function<bool()> &done, int ms) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    };
    EditorPage page(ctx);
    page.resize(1300, 800);
    page.show();
    settle();
    check(page.chooser() && page.chooser()->count() >= 5, "the chooser lists the joystick modes");
    check(page.selectMode(QStringLiteral("timeline")) && !page.canEdit(), "a built-in joystick mode is not a file");
    check(page.copyButton()->isVisibleTo(&page) && !page.editorWidget()->isVisibleTo(&page), "it offers a copy and shows no editor");
    check(page.note().contains(QStringLiteral("built-in")), "and says why");
    check(page.makeCopy() && page.canEdit() && page.currentId() != QStringLiteral("timeline"), "Make my own copy chooses a new file");
    settle();
    const QString id = page.currentId();
    check(QFile::exists(tmpPath + QStringLiteral("/modes/") + id + QStringLiteral(".conf")), "the copy is a file");
    ModeEditorWidget *ed = page.editorWidget();
    check(ed->isVisibleTo(&page) && ed->currentTabId() == QStringLiteral("motions") && ed->motionEditor() != nullptr, "the picture is shown at once");
    check(!page.copyButton()->isVisibleTo(&page), "the copy button is gone for a file");
    // an edit is written by itself
    ed->motionEditor()->select(QStringLiteral("m:twist"));
    settle();
    QToolButton *off = nullptr;
    for (QToolButton *b : ed->motionEditor()->inspector()->findChildren<QToolButton *>())
        if (b->accessibleName() == QStringLiteral("Do nothing") && b->isCheckable()) off = b;
    check(off != nullptr, "the action tiles are there");
    if (off) off->click();
    settle();
    check(waitFor([&] { return !ed->pending(); }, 3000), "written by itself");
    {
        QFile f(tmpPath + QStringLiteral("/modes/") + id + QStringLiteral(".conf"));
        check(f.open(QIODevice::ReadOnly) && QString::fromUtf8(f.readAll()).contains(QStringLiteral("TWIST_ACTION = none")), "the file holds the choice");
    }  // closed here: Windows cannot remove an open file
    // the other joystick modes of the chooser
    check(page.selectMode(QStringLiteral("normal")) && !page.canEdit(), "choosing a built-in joystick mode again");
    check(QFile::remove(tmpPath + QStringLiteral("/modes/") + id + QStringLiteral(".conf")), "the copy is removed again");
    std::printf("editor page self-test: %s\n", ok ? "ok" : "failed");
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


// Overview quick actions: documentation, log viewer, mode switch, tasks, filter, getting started, and
// the matching parts of the Channels page and the button rows. The runner records instead of opening
// anything; the status is fed by hand.
static bool quickActionsSelfTest(const UiContext &base, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "quick actions self-test failed: %s\n", what);
        ok = ok && cond;
    };
    using Kind = spn::ui::RecordingActionRunner::Kind;
    spn::ui::RecordingActionRunner rec;
    QtStrings strings;
    spn::ui::SettingsModel settings;
    std::vector<std::string> warnings;
    std::string err;
    UiContext ctx = base;
    ctx.runner = &rec;
    ctx.reload = spn::ui::reloadVia(rec);
    check(settings.load(ctx.configPath, &warnings, &err), "the configuration loads");
    settings.setReloadCallback(ctx.reload);
    spn::ui::StatusModel status;
    spn::ui::ModeCatalog modes(ctx.modesDir);
    ctx.settings = &settings;
    ctx.status = &status;
    ctx.modes = &modes;
    ctx.strings = &strings;
    QDir().mkpath(QString::fromStdString(ctx.runtimeDir));
    const QString progress = QString::fromStdString(spn::ui::uiProgressPath(ctx.configPath));
    QFile::remove(progress);

    OverviewPage page(ctx);
    page.resize(2400, 900);
    page.show();
    settle();

    // documentation: through the runner, a failure is shown
    check(page.findChild<QPushButton *>(QStringLiteral("documentation")) != nullptr, "Documentation button on the Overview");
    page.findChild<QPushButton *>(QStringLiteral("documentation"))->click();
    check(rec.count(Kind::OpenUrl) == 1 && rec.calls.back().path == spn::ui::docsUrl() && page.actionMessage().isEmpty(),
          "Documentation opens the docs address");
    rec.result = false;
    page.openDocumentation();
    check(!page.actionMessage().isEmpty(), "a failed Documentation is reported");
    rec.result = true;
    page.openDocumentation();
    check(page.actionMessage().isEmpty(), "the message goes away after success");

    // tasks open pages, "new profile" asks for the picker
    QStringList opened;
    int picker = 0;
    QObject::connect(&page, &OverviewPage::openPage, [&opened](const QString &id) { opened << id; });
    QObject::connect(&page, &OverviewPage::newProgramProfile, [&picker] { ++picker; });
    for (const char *name : {"task-tune", "task-axes", "task-modes"}) {
        auto *b = page.findChild<QPushButton *>(QString::fromLatin1(name));
        check(b != nullptr, "task button exists");
        if (b) b->click();
    }
    check(opened == QStringList({QStringLiteral("profile-channels"), QStringLiteral("axes"), QStringLiteral("modes")}),
          "the task buttons open the Global Channels tab, Axes and the joystick modes");
    page.findChild<QPushButton *>(QStringLiteral("new-program-profile"))->click();
    check(picker == 1, "New profile for the running program is requested");

    // mode switch: disabled without a daemon, then enabled and marked with the running mode
    page.refresh();
    check(page.modeCombo()->count() >= 3 && !page.modeCombo()->isEnabled(), "mode switch lists the modes but is disabled without a daemon");
    {
        bool allTips = page.modeCombo()->count() > 0;
        for (int i = 0; i < page.modeCombo()->count(); ++i) allTips = allTips && !page.modeCombo()->itemData(i, Qt::ToolTipRole).toString().isEmpty();
        check(allTips, "every entry of the Active joystick mode drop-down carries the description as tooltip");
    }
    {
        const QString now = QString::number(static_cast<long long>(std::time(nullptr)));
        const std::string text = "profile=global\ndevice=SpaceMouse Pro\nmode=timeline\nenabled=1\npaused=0\ndevices_seen=1\nupdated=" +
                                 now.toStdString() + "\n";
        status.update(text, true, static_cast<int64_t>(std::time(nullptr)));
    }
    page.refresh();
    check(page.modeCombo()->isEnabled() && page.modeCombo()->currentData().toString() == QStringLiteral("timeline"),
          "mode switch marks the mode the daemon runs");
    rec.calls.clear();
    page.switchMode(QStringLiteral("off"));
    QFile request(QString::fromStdString(spn::modeRequestPath(ctx.runtimeDir)));
    check(request.open(QIODevice::ReadOnly) && request.readAll().trimmed() == "off" && rec.count(Kind::RequestReload) == 1,
          "switching a mode writes the request and asks for a reload");
    request.close();
    check(page.actionMessage().isEmpty(), "no error after switching");
    rec.result = false;
    page.switchMode(QStringLiteral("timeline"));
    check(!page.actionMessage().isEmpty(), "a failed switch is reported");
    rec.result = true;
    page.switchMode(QStringLiteral("not a mode!"));
    check(!page.actionMessage().isEmpty(), "an invalid mode is refused");
    page.switchMode(QStringLiteral("normal"));

    // profile filter
    check(page.profileCardCount() == 3, "three profiles, no filter");
    page.setProfileFilter(QStringLiteral("FIREFOX"));
    check(page.profileCardCount() == 1, "the filter keeps the matching profile");
    page.setProfileFilter(QStringLiteral("timeline"));
    check(page.profileCardCount() == 1, "the filter looks at the mode");
    page.setProfileFilter(QStringLiteral("no such profile"));
    check(page.profileCardCount() == 0, "the filter can show nothing");
    page.setProfileFilter(QString());
    check(page.profileCardCount() == 3, "an empty filter shows all again");

    // a very wide window: the content stops growing and stays centered
    settle();
    check(page.contentWidth() <= spn::ui::tokens::kMaxContentWidth && page.contentWidth() > 0, "the Overview content has a maximum width");
    check(page.cardsFit() && page.cardsFillWidth(), "cards fit and fill the limited width");

    // getting started: the device step names the not running daemon; the axes step follows the progress file
    status.update("", false, static_cast<int64_t>(std::time(nullptr)));
    page.refresh();
    {
        const QStringList t = page.gettingStartedTexts();
        check(t.size() == 3 && t[0].contains(QStringLiteral("not running")) && t[1].startsWith(QStringLiteral("To do:")),
              "the device step hints at the daemon, the axes step is open");
    }
    std::string perr;
    check(spn::ui::markAxisAssistantApplied(spn::ui::uiProgressPath(ctx.configPath), &perr), "progress file written");
    page.refresh();
    check(page.gettingStartedTexts().size() == 3 && page.gettingStartedTexts()[1].startsWith(QStringLiteral("Done:")),
          "the axes step is done after the assistant was applied");
    QFile::remove(progress);

    // log viewer: the end of the file, refresh, copy; the missing file is explained
    const QString logPath = QString::fromStdString(spn::logFilePathIn(ctx.runtimeDir));
    QFile::remove(logPath);
    {
        LogDialog gone(ctx.runtimeDir, strings);
        check(gone.shownText().isEmpty() && !gone.note().isEmpty(), "no log file: the dialog says so");
    }
    {
        QFile lf(logPath);
        check(lf.open(QIODevice::WriteOnly), "log file created");
        lf.write("first line\nsecond line\n");
        lf.close();
        page.showLog();
        LogDialog *dlg = nullptr;
        for (QWidget *top : QApplication::topLevelWidgets())
            if (auto *d = qobject_cast<LogDialog *>(top)) dlg = d;
        check(dlg != nullptr, "Open log shows the log dialog");
        if (dlg) {
            check(dlg->shownText().contains(QStringLiteral("second line")), "the dialog shows the log");
            check(lf.open(QIODevice::WriteOnly | QIODevice::Append), "log file appended");
            lf.write("third line\n");
            lf.close();
            check(dlg->refresh() && dlg->shownText().contains(QStringLiteral("third line")), "Refresh reads the new lines");
            dlg->copyAll();
            check(QApplication::clipboard()->text() == dlg->shownText(), "Copy puts the text on the clipboard");
            check(LogDialog::showFor(&page, ctx.runtimeDir, strings) == dlg, "a second Show log raises the open dialog");
            dlg->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        }
    }
    QFile::remove(logPath);

    // In the window the request opens the running-program picker of the Profiles page directly
    {
        MainWindow mw(ctx);
        mw.show();
        settle();
        int closed = 0;
        QString className;
        QTimer closer;
        QObject::connect(&closer, &QTimer::timeout, [&] {
            if (QWidget *m = QApplication::activeModalWidget()) {
                className = QString::fromLatin1(m->metaObject()->className());
                ++closed;
                if (auto *d = qobject_cast<QDialog *>(m)) d->reject();
            }
        });
        closer.start(50);
        auto *ov = mw.findChild<OverviewPage *>();
        check(ov != nullptr, "the window has an Overview");
        if (ov) ov->findChild<QPushButton *>(QStringLiteral("new-program-profile"))->click();
        for (int i = 0; i < 100 && closed == 0; ++i) {
            QApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(20);
        }
        closer.stop();
        check(closed == 1 && className == QStringLiteral("AddAppDialog") && mw.currentPageId() == QStringLiteral("profiles"),
              "New profile for the running program opens the picker on the Profiles page");
    }

    // Channels tab (the editor): the config key in tooltip and accessible description, the filter, the width limit
    {
        auto *channels = new ChannelsEditor(ctx);
        channels->resize(2400, 900);
        channels->show();
        settle();
        QAbstractSpinBox *sensV = nullptr, *sensH = nullptr;
        int described = 0, spins = 0;
        for (auto *sp : channels->findChildren<QAbstractSpinBox *>()) {
            if (sp->objectName() == QStringLiteral("qt_spinbox_lineedit")) continue;
            ++spins;
            described += sp->accessibleDescription().contains(QStringLiteral("Config key: ")) &&
                         sp->toolTip().contains(QStringLiteral("Config key: "));
            if (sp->toolTip().contains(QStringLiteral("Config key: SCROLL_V_SENS"))) sensV = sp;
            if (sp->toolTip().contains(QStringLiteral("Config key: SCROLL_H_SENS"))) sensH = sp;
        }
        check(spins > 0 && described == spins, "every spin box row names its config key");
        check(sensV && sensH, "the sensitivity rows exist");
        if (sensV && sensH) {
            channels->setFilter(QStringLiteral("scroll_h_sens"));
            check(sensH->isVisibleTo(channels) && !sensV->isVisibleTo(channels), "the filter finds a row by its config key");
            channels->setFilter(QStringLiteral("Vertical scroll"));
            check(sensV->isVisibleTo(channels) && !sensH->isVisibleTo(channels), "the filter finds the rows of a group by its title");
            channels->setFilter(QStringLiteral("nothing like it"));
            check(!sensV->isVisibleTo(channels) && !sensH->isVisibleTo(channels), "a filter without a match hides every row");
            channels->setFilter(QString());
            check(sensV->isVisibleTo(channels) && sensH->isVisibleTo(channels), "an empty filter shows every row");
        }
        if (auto *area = channels->findChild<QScrollArea *>())
            check(area->widget()->width() <= spn::ui::tokens::kMaxContentWidth, "the Channels content has a maximum width");
        delete channels;
    }

    // the label of a button row names the number of buttons of the device
    {
        QWidget host;
        auto *grid = new QGridLayout(&host);
        spn::ui::BindingRow row;
        row.number = 3;
        row.text = "none";
        ButtonRowEnv env;
        env.strings = &strings;
        env.deviceButtons = 15;
        addButtonRow(&host, &host, grid, 0, row, env, ButtonRowHandlers());
        env.deviceButtons = 0;
        addButtonRow(&host, &host, grid, 1, row, env, ButtonRowHandlers());
        QStringList tips;
        for (auto *l : host.findChildren<QLabel *>())
            if (l->text() == QStringLiteral("Button 3")) tips << l->toolTip();
        check(tips.size() == 2 && tips[0] == QStringLiteral("This device has 15 buttons") && tips[1].isEmpty(),
              "the button label names the button count of the device, not for an unknown device");
    }
    (void)tmpPath;
    std::printf("quick actions self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Overview: it is the first page, shows one card per profile of the configuration, and a card opens
// its profile on the Profiles page.
// The widgets of a class without Q_OBJECT (motion_view.hpp) below a page: qobject_cast does not work for them.
template <class T>
static QList<T *> findAllOf(QWidget *root) {
    QList<T *> out;
    for (QWidget *c : root->findChildren<QWidget *>())
        if (T *t = dynamic_cast<T *>(c)) out.append(t);
    return out;
}

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
    {
        const QList<motionview::MotionStrip *> strips = findAllOf<motionview::MotionStrip>(w.currentPage());
        bool six = strips.size() == 3;
        for (const motionview::MotionStrip *st : strips) six = six && st->chips().size() == 6;
        check(six, "every Overview card shows the six motion chips");
    }
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

// Window state: size, position and page survive a close and a new window; a geometry that is off all
// screens is ignored and the minimum size holds; a hidden window comes back on the saved page; closing
// hides the window with a tray and quits without one.
// A resize or move is saved shortly afterwards, without closing the window.
static bool windowAutosaveSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    const std::string path = (tmpPath + QStringLiteral("/window-autosave/ui.conf")).toStdString();
    MainWindow w(ctx);
    w.enableStatePersistence(path);
    w.show();
    settle();
    w.resize(1111, 777);
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 1600) QApplication::processEvents(QEventLoop::AllEvents, 50);
    const spn::ui::UiSettings saved = spn::ui::loadUiSettings(path);
    if (!saved.window.valid || saved.window.w != 1111 || saved.window.h != 777) {
        std::fprintf(stderr, "window autosave self-test failed: the geometry was not saved after the resize\n");
        ok = false;
    }
    return ok;
}

static bool windowStateSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "window state self-test failed: %s\n", what);
        ok = ok && cond;
    };
    const std::string path = (tmpPath + QStringLiteral("/window-state/ui.conf")).toStdString();
    int quits = 0, notices = 0;
    std::vector<std::string> ids;
    {
        MainWindow a(ctx);
        ids = a.pageIds();
        check(ids.size() >= 3, "pages to choose from");
        QObject::connect(&a, &MainWindow::quitRequested, [&quits] { ++quits; });
        QObject::connect(&a, &MainWindow::hiddenToTray, [&notices] { ++notices; });
        a.enableStatePersistence(path);  // no file yet: defaults
        check(a.size() == MainWindow::defaultWindowSize(), "no file: the default size");
        a.show();
        check(a.showPage(QString::fromStdString(ids[2])), "select a page");
        a.resize(900, 540);
        a.move(30, 40);
        settle();
        // with a tray: closing hides the window and the process goes on
        check(a.trayAvailable() && a.trayNote().isEmpty(), "a tray is assumed by default, no note");
        a.close();
        check(!a.isVisible() && quits == 0, "with a tray, closing only hides the window");
        check(notices == 1, "the first close shows the tray notice");
        a.show();
        a.close();
        check(notices == 1, "later closes show no notice");
        const spn::ui::UiSettings s = spn::ui::loadUiSettings(path);
        check(s.window.valid && s.window.w == 900 && s.window.h == 540 && s.page == ids[2], "the state was saved on close");
        // without a tray: closing quits, and the note says so
        a.setTrayAvailable(false);
        check(!a.trayNote().isEmpty(), "the status bar says there is no tray");
        a.show();
        a.close();
        check(quits == 1 && !a.isVisible() && notices == 1, "without a tray, closing asks to quit, without a notice");
        a.setTrayAvailable(true);
        check(a.trayNote().isEmpty(), "the note goes away with a tray");
    }
    {
        MainWindow b(ctx);
        b.enableStatePersistence(path);
        check(b.currentPageId().toStdString() == ids[2], "the last page is selected again");
        check(b.size() == QSize(900, 540), "the size is restored");
        check(b.pos() == QPoint(30, 40), "the position is restored");
        // a hidden window comes back on the saved page when it is raised (tray click, second start)
        b.showPage(QString::fromStdString(ids[0]));
        check(spn::ui::loadUiSettings(path).page == ids[0], "a page change is saved at once");
        std::string err;
        spn::ui::updateUiSettings(path, [&](spn::ui::UiSettings &s) { s.page = ids[1]; }, &err);
        check(!b.isVisible(), "hidden before raising");
        b.bringToFront();
        check(b.currentPageId().toStdString() == ids[1], "raising a hidden window shows the saved page");
        b.hide();
    }
    // a geometry that is off all screens is ignored; a tiny one is raised to the minimum; junk is ignored
    auto sizeAfter = [&](const char *line, QPoint *where) {
        QFile f(QString::fromStdString(path));
        if (!f.open(QIODevice::WriteOnly)) return QSize();
        f.write(line);
        f.close();
        MainWindow c(ctx);
        c.enableStatePersistence(path);
        if (where) *where = c.pos();
        return c.size();
    };
    QPoint at;
    check(sizeAfter("window = 90000,90000,900,540\n", &at) == MainWindow::defaultWindowSize(), "off all screens: ignored");
    check(sizeAfter("window = 10,10,100,100\n", &at) == QSize(spn::ui::tokens::kMinWindowWidth, spn::ui::tokens::kMinWindowHeight) &&
              at == QPoint(10, 10),
          "the minimum size holds");
    check(sizeAfter("window = nonsense\npage = ..\n", &at) == MainWindow::defaultWindowSize(), "junk: defaults");
    {
        const QSize d = MainWindow::defaultWindowSize();
        check(d.width() >= spn::ui::tokens::kMinWindowWidth && d.height() >= spn::ui::tokens::kMinWindowHeight &&
                  d.width() <= spn::ui::tokens::kDefaultWindowWidth && d.height() <= spn::ui::tokens::kDefaultWindowHeight,
              "the default size is between the minimum and 1280x820");
    }
    std::printf("window state self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Selected rows must stay readable: render the lists and trees with a selected row in every theme
// (focused and unfocused) and compare the text pixels with the selection background.
static bool selectionContrastSelfTest(const UiContext &ctx) {
    bool ok = true;
    auto check = [&ok](bool cond, const QString &what) {
        if (!cond) std::fprintf(stderr, "selection contrast self-test failed: %s\n", qPrintable(what));
        ok = ok && cond;
    };
    auto ratio = [](const QColor &a, const QColor &b) {
        return spn::ui::contrastRatio({uint8_t(a.red()), uint8_t(a.green()), uint8_t(a.blue())},
                                      {uint8_t(b.red()), uint8_t(b.green()), uint8_t(b.blue())});
    };
    // the pixel of the band [y0, y1) of the image that is farthest from the background = the text colour
    auto textRatio = [&](const QImage &img, const QRect &band, const QColor &bg) {
        double best = 1.0;
        for (int y = band.top(); y < band.bottom() && y < img.height(); ++y)
            for (int x = band.left(); x < band.right() && x < img.width(); ++x) best = std::max(best, ratio(img.pixelColor(x, y), bg));
        return best;
    };
    auto checkList = [&](QAbstractItemView *view, const QModelIndex &idx, const QString &ctxName) {
        view->setCurrentIndex(idx);
        view->selectionModel()->select(idx, QItemSelectionModel::ClearAndSelect);
        for (bool focused : {true, false}) {
            if (focused) view->setFocus(); else view->clearFocus();
            settle();
            const QRect r = view->visualRect(idx);
            const QImage img = view->viewport()->grab().toImage();
            const QRect inner = r.adjusted(6, 2, -6, -2);
            const QColor bg = img.pixelColor(inner.left(), inner.center().y());
            const QString what = ctxName + (focused ? QStringLiteral(" (focused)") : QStringLiteral(" (not focused)"));
            // the text must be far from the selection colour; the muted line (below the name) too
            const QRect top(inner.left(), inner.top(), inner.width(), inner.height() / 2);
            check(textRatio(img, top, bg) >= 4.5, what + QStringLiteral(": name"));
            if (!view->model()->data(idx, kRoleDescription).toString().isEmpty()) {
                const QRect bottom(inner.left(), inner.center().y() + 2, inner.width(), inner.height() / 2 - 2);
                check(textRatio(img, bottom, bg) >= 4.0, what + QStringLiteral(": description"));
            }
        }
    };
    for (const char *themeId : {"light", "dark", "system"}) {
        applyTheme(spn::ui::themeFromId(themeId));
        settle();
        const QString t = QString::fromLatin1(themeId);
        {
            ModesPage page(ctx);
            page.resize(900, 600);
            page.show();
            settle();
            auto *list = page.findChild<QListWidget *>();
            check(list && list->count() > 1, t + QStringLiteral(": modes list"));
            if (list && list->count() > 1) {
                checkList(list, list->model()->index(1, 0), t + QStringLiteral(" joystick modes"));
                list->selectionModel()->clear();
            }
        }
        {
            ButtonModesPage page(ctx);
            page.resize(900, 600);
            page.show();
            settle();
            auto *list = page.findChild<QListWidget *>();
            check(list && list->count() > 0, t + QStringLiteral(": button modes list"));
            if (list && list->count() > 0) checkList(list, list->model()->index(0, 0), t + QStringLiteral(" button modes"));
        }
        {
            QTreeWidget tree;
            tree.setHeaderHidden(true);
            auto *top = new QTreeWidgetItem(&tree, QStringList(QStringLiteral("Programs")));
            new QTreeWidgetItem(top, QStringList(QStringLiteral("Firefox, blender")));
            tree.expandAll();
            tree.resize(300, 200);
            tree.show();
            settle();
            checkList(&tree, tree.model()->index(0, 0, tree.model()->index(0, 0)), t + QStringLiteral(" tree"));
        }
    }
    // the token pairs behind the style sheet
    for (spn::ui::Theme th : {spn::ui::Theme::Light, spn::ui::Theme::Dark}) {
        spn::ui::ThemeColors c;
        spn::ui::themeColors(th, &c);
        check(spn::ui::contrastRatio(c.selectedText, c.accentSoft) >= 7.0, QStringLiteral("selected text token"));
    }
    applyTheme(spn::ui::Theme::System);
    settle();
    std::printf("selection contrast self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Large fonts (the operating system's font scale at 150% and 200%) and narrow windows: no label or button
// may be cut off. Pages live in scroll areas, so a page that needs more room than the window gives scrolls
// instead of squashing its content: every visible label and button is at least as big as it asks for at its
// minimum. Scroll bars are only the fallback: at the normal font size in the normal window no page shows a
// horizontal scroll bar.
static bool fontScaleSelfTest(MainWindow &w) {
    bool ok = true;
    const QFont base = QApplication::font();
    // The default window of a normal screen (first start), not the window clamped to 90 % of the small virtual screen of a
    // CI machine: "at the normal font size in the normal window no page needs a horizontal scroll bar".
    const QSize baseSize(spn::ui::tokens::kDefaultWindowWidth, spn::ui::tokens::kDefaultWindowHeight);
    w.show();
    struct Case {
        double scale;
        int width;           // window width; 0 = the default, -1 = as wide as a user with this font would make it
        bool noScrollBar;    // a horizontal scroll bar is a failure
    };
    const Case cases[] = {{1.0, 0, true}, {1.5, -1, false}, {2.0, -1, false}, {1.0, 560, false}, {2.0, 560, false}};
    // the System theme (platform controls) and our own style sheet, whose lengths are in em (issue #104)
    for (spn::ui::Theme theme : {spn::ui::Theme::System, spn::ui::Theme::Light})
    for (const Case &cs : cases) {
        const double scale = cs.scale;
        QApplication::setFont(base);
        applyTheme(theme);
        QFont f = QApplication::font();
        f.setPointSizeF(f.pointSizeF() * scale);
        QApplication::setFont(f);
        // someone with a larger font also uses a wider window (up to a 1920 px screen)
        const int width = cs.width > 0 ? cs.width : cs.width == 0 ? baseSize.width() : std::min(int(scale * baseSize.width()), 1920);
        w.resize(width, baseSize.height());
        settle();
        for (const std::string &id : w.pageIds()) {
            w.showPage(QString::fromStdString(id));
            settle();
            QWidget *page = w.currentPage();
            if (!page) continue;
            // layout quality: by default (normal font, normal window) a form page needs no horizontal scroll bar
            for (QScrollArea *area : page->findChildren<QScrollArea *>()) {
                if (!area->isVisibleTo(page) || !area->widgetResizable()) continue;
                if (cs.noScrollBar && area->horizontalScrollBar()->maximum() > 0) {
                    std::fprintf(stderr, "font scale self-test failed: x%.1f page %s: a form needs a horizontal scroll bar (%d px too wide)\n",
                                 scale, id.c_str(), area->horizontalScrollBar()->maximum());
                    ok = false;
                    // which controls reach beyond the visible width (for the log of a failing run)
                    for (QWidget *c : area->widget()->findChildren<QWidget *>()) {
                        const int right = c->mapTo(area->widget(), QPoint(c->width(), 0)).x();
                        if (c->isVisibleTo(area->widget()) && right > area->viewport()->width())
                            std::fprintf(stderr, "  too wide: %s \"%s\" \"%s\" min %d right %d viewport %d\n", c->metaObject()->className(),
                                         qPrintable(c->accessibleName().left(40)),
                                         qPrintable(qobject_cast<QLabel *>(c) ? qobject_cast<QLabel *>(c)->text().left(40) : QString()),
                                         c->minimumSizeHint().width(), right, area->viewport()->width());
                    }
                }
            }
            for (QWidget *c : page->findChildren<QWidget *>()) {
                if (!c->isVisibleTo(page)) continue;
                const auto *lab = qobject_cast<QLabel *>(c);
                const bool text = qobject_cast<QPushButton *>(c) || qobject_cast<QCheckBox *>(c) || (lab && !lab->text().isEmpty());
                if (!text) continue;
                const QSize hint = c->minimumSizeHint();
                const bool wraps = lab && lab->wordWrap();
                // inside a scroll area the widget is laid out at its hint: a smaller size means it is cut off
                if (c->height() < hint.height() - 2 || (!wraps && c->width() < hint.width() - 2)) {
                    std::fprintf(stderr, "font scale self-test failed: x%.1f width %d page %s: %s \"%s\" is %dx%d, needs %dx%d\n", scale, width,
                                 id.c_str(), c->metaObject()->className(), qPrintable(lab ? lab->text().left(30) : qobject_cast<QAbstractButton *>(c) ? qobject_cast<QAbstractButton *>(c)->text().left(30) + QStringLiteral(" / ") + c->accessibleName().left(30) : c->objectName()),
                                 c->width(), c->height(), hint.width(), hint.height());
                    ok = false;
                }
            }
        }
    }
    QApplication::setFont(base);
    applyTheme(spn::ui::Theme::System);
    w.resize(baseSize);
    settle();
    std::printf("font scale self-test: %s\n", ok ? "ok" : "failed");
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

// ---- helpers of the autosave self-tests ----

// A window with its own temporary configuration and a runner that records the reload requests.
struct EditorRig {
    QTemporaryDir dir;
    spn::ui::RecordingActionRunner rec;
    UiContext ctx;
    std::unique_ptr<MainWindow> win;
    explicit EditorRig(const QByteArray &conf, const std::function<void(const QString &)> &prepare = nullptr) {
        const QString p = dir.path();
        ctx.runner = &rec;
        ctx.configPath = (p + QStringLiteral("/dofscroll.conf")).toStdString();
        ctx.modesDir = (p + QStringLiteral("/modes")).toStdString();
        ctx.buttonModesDir = (p + QStringLiteral("/buttons")).toStdString();
        ctx.runtimeDir = (p + QStringLiteral("/run")).toStdString();
        QDir().mkpath(p + QStringLiteral("/modes"));
        QDir().mkpath(p + QStringLiteral("/buttons"));
        QDir().mkpath(p + QStringLiteral("/run"));
        QFile f(p + QStringLiteral("/dofscroll.conf"));
        if (f.open(QIODevice::WriteOnly)) f.write(conf);
        f.close();
        if (prepare) prepare(p);  // e.g. the migration to one file per profile, as the first start does it
        ctx.reload = spn::ui::reloadVia(rec);
        win = std::make_unique<MainWindow>(ctx);
        win->show();
        settle();
    }
    QString confPath() const { return QString::fromStdString(ctx.configPath); }
    QString confText() const {
        QFile f(confPath());
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    }
    // The file as the daemon would read it.
    spn::ui::SettingsModel disk() const {
        spn::ui::SettingsModel m;
        std::string err;
        if (!m.load(ctx.configPath, nullptr, &err))
            std::fprintf(stderr, "profile editor self-test: cannot read %s: %s\n", ctx.configPath.c_str(), err.c_str());
        return m;
    }
    int reloads() const { return rec.count(spn::ui::RecordingActionRunner::Kind::RequestReload); }
    Autosaver *saver() { return win->autosaver(); }
    // Waits (event loop, the real timer) until nothing is pending any more.
    bool waitSaved(int ms = 3000) {
        QElapsedTimer t;
        t.start();
        while (saver()->pending() && t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
        QApplication::processEvents();
        if (saver()->failed())
            std::fprintf(stderr, "profile editor self-test: autosave failed: %s\n", saver()->statusText().toUtf8().constData());
        return !saver()->pending();
    }
};

template <class W>
static W *findNamed(QWidget *root, const QString &name) {
    for (W *w : root->findChildren<W *>())
        if (w->accessibleName() == name && w->isVisibleTo(root)) return w;
    return nullptr;
}

static QPushButton *buttonTexted(QWidget *root, const QString &text) {
    for (QPushButton *b : root->findChildren<QPushButton *>())
        if (b->text() == text) return b;
    return nullptr;
}

// The text the user would type: sets it and emits what the line edit emits for typing.
static void typeInto(QLineEdit *e, const QString &text) {
    e->setText(text);
    Q_EMIT e->textEdited(text);
}

static const spn::Section *sectionOf(const spn::ui::SettingsModel &m, spn::Section::Kind kind, const std::string &pattern) {
    const size_t i = kind == spn::Section::GLOBAL ? 0 : m.profiles().find(kind, pattern);
    return i == spn::ProfileSet::npos ? nullptr : &m.profiles().sections[i];
}
static const std::string *keyIn(const spn::Section *s, const std::string &key) {
    if (!s) return nullptr;
    for (const spn::KV &kv : s->kv)
        if (kv.first == key) return &kv.second;
    return nullptr;
}
// The value of a key as the file on disk holds it. A copy: the pointers of sectionOf()/keyIn() point into the
// model they were taken from, and a temporary model is gone at the end of the statement.
static std::optional<std::string> diskKey(const EditorRig &rig, spn::Section::Kind kind, const std::string &pattern,
                                          const std::string &key) {
    const spn::ui::SettingsModel m = rig.disk();
    if (const std::string *v = keyIn(sectionOf(m, kind, pattern), key)) return *v;
    return std::nullopt;
}

// ---- autosave: the profile editor ----
// Every change is written by itself (a switch at once, typed text after a pause), invalid values are not
// written, the tabs stay when the profile changes, and the Channels tab edits the selected profile.
static bool profileEditorSelfTest(const std::function<bool(MainWindow &)> &accessibility) {
    using spn::Section;
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "profile editor self-test failed: %s\n", what);
        ok = ok && cond;
    };
    EditorRig rig("[global]\n\n[device:SpaceMouse Pro]\nSCROLL_V_SENS = 9\n\n[app:firefox]\nMODE = timeline\n\n[app:blender]\nENABLED = 0\n");
    MainWindow &w = *rig.win;
    w.showPage(QStringLiteral("profiles"));
    settle();
    ProfilesPage *page = w.profilesPage();
    check(page != nullptr, "the window has a Profiles page");
    if (!page) return false;
    ChannelsEditor *ch = page->channelsEditor();
    const QStringList allTabs = {QStringLiteral("general"), QStringLiteral("channels"), QStringLiteral("motions"),
                                 QStringLiteral("overrides")};

    // the list: Global first, then the devices, then the programs, always next to the tabs
    const QStringList labels = page->profileListLabels();
    check(labels.size() >= 6 && labels[0] == QStringLiteral("Global") && labels[2].startsWith(QStringLiteral("SpaceMouse Pro")) &&
              labels[4].startsWith(QStringLiteral("firefox")),
          "the list shows Global, then the devices, then the programs in the order they are applied");
    check(labels[2] == QStringLiteral("SpaceMouse Pro (1)") && labels[4] == QStringLiteral("firefox (1)"),
          "the list shows how many settings a profile overrides");

    // the same tabs for every profile; the header says which profile is edited
    for (size_t s : {size_t(0), size_t(1), size_t(2), size_t(3)}) {
        check(page->selectSection(s) && page->tabIds() == allTabs, "General, Channels, Motions and buttons and Overrides for every profile");
        // the General tab shows what the profile does with the same six-motion panel as the Joystick modes page
        const QList<motionview::MotionGrid *> grids = findAllOf<motionview::MotionGrid>(page);
        if (s == 3) {  // blender has its output off: nothing is sent, so the panel is hidden with the joystick mode
            check(grids.isEmpty(), "with the output off the General tab shows no motions");
        } else {
            check(!grids.isEmpty() && grids.last()->cardTexts().size() == 6 && grids.last()->cardTexts().value(0).startsWith(QStringLiteral("Vertical push")),
                  "the General tab of every profile with output on shows the six motions");
        }
    }
    // Motions and buttons (issue #143, phase 4): the picture of the selected profile, tuning as overrides of the profile,
    // buttons through the shared row, inheritance in words
    {
        check(page->selectSection(2) && page->selectTab(QStringLiteral("motions")), "program profile, Motions and buttons tab");
        settle();
        ProfileMotionsView *mv = page->motionsView();
        check(mv != nullptr && mv->picture() != nullptr, "the tab has the picture");
        if (mv) {
            int motions = 0;
            for (const MotionPicture::Item &it : mv->picture()->items()) motions += it.id.startsWith(QLatin1String("m:"));
            check(motions == 6, "six motions are drawn");
            check(mv->select(QStringLiteral("m:v_push")), "select the vertical push");
            settle();
            check(mv->inspectorTitle().contains(QStringLiteral("Vertical push")), "the inspector names the motion");
            QLineEdit *sens = nullptr;
            for (QLineEdit *e : mv->inspector()->findChildren<QLineEdit *>())
                if (e->accessibleName() == QStringLiteral("Sensitivity")) sens = e;
            check(sens != nullptr, "the inspector has the sensitivity of the channel");
            bool inheritance = false;
            for (QLabel *l : mv->inspector()->findChildren<QLabel *>())
                inheritance = inheritance || l->text().startsWith(QStringLiteral("Inherited")) || l->text().startsWith(QStringLiteral("Set in this profile"));
            check(inheritance, "where the value comes from is said in words");
            auto originOf = [&](const QString &prefix) {
                for (QLabel *l : mv->inspector()->findChildren<QLabel *>())
                    if (l->text().startsWith(prefix)) return true;
                return false;
            };
            if (sens) {
                sens->setText(QStringLiteral("7"));
                emit sens->textEdited(QStringLiteral("7"));
                check(originOf(QStringLiteral("Set in this profile")), "typing writes an override of the profile and says so");
                mv->setSection(2, true);  // as after the autosave: rebuilt from the model
                settle();
                QPushButton *inherit = nullptr;
                for (QPushButton *b : mv->inspector()->findChildren<QPushButton *>())
                    if (b->accessibleName() == QStringLiteral("Sensitivity: Inherit")) inherit = b;
                check(inherit != nullptr && inherit->isEnabled(), "an override has an Inherit button");
                if (inherit) inherit->click();
                settle();
                check(originOf(QStringLiteral("Inherited")), "Inherit removes the override");
            }
            // a button: the shared row (the Global profile always has the binding editor)
            check(page->selectSection(0) && page->selectTab(QStringLiteral("motions")), "Global, Motions and buttons tab");
            settle();
            QString firstButton;
            for (const MotionPicture::Item &it : mv->picture()->items())
                if (it.id.startsWith(QLatin1String("b:")) && firstButton.isEmpty()) firstButton = it.id;
            if (!firstButton.isEmpty()) {
                check(mv->select(firstButton), "select a button");
                settle();
                check(mv->inspectorTitle().contains(QStringLiteral("Button")) && !mv->inspector()->findChildren<QComboBox *>().isEmpty(), "a button shows the shared row");
            }
            check(mv->select(QStringLiteral("m:lift")), "select lift / press");
            settle();
            // lift / press has its own action list (nothing by default, no analog keys) and its LIFT_* tuning fields
            QComboBox *liftAct = mv->inspector()->findChild<QComboBox *>(QStringLiteral("motionAction"));
            check(liftAct != nullptr && liftAct->findData(int(spn::ChannelActionType::Keys)) < 0 &&
                      liftAct->findData(int(spn::ChannelActionType::Step)) >= 0 && liftAct->currentData().toInt() == int(spn::ChannelActionType::Default),
                  "lift / press offers its own choices: nothing is the default, no analog keys");
            bool liftTuning = false;
            for (QLineEdit *e : mv->inspector()->findChildren<QLineEdit *>()) liftTuning = liftTuning || e->toolTip() == QStringLiteral("LIFT_DEADZONE");
            check(liftTuning, "lift / press has its dead zone field");
            if (liftAct) {
                const int idx = liftAct->findData(int(spn::ChannelActionType::WheelV));
                liftAct->setCurrentIndex(idx);
                emit liftAct->activated(idx);
                settle();
                QLabel *words = mv->inspector()->findChild<QLabel *>(QStringLiteral("motionStateWords"));
                check(words && words->text().contains(QStringLiteral("Set in Global")), "lift / press can be given an action in Global and says so");
                QPushButton *back = mv->inspector()->findChild<QPushButton *>(QStringLiteral("motionInherit"));
                check(back && back->isEnabled(), "a lift set here has Use the default");
                if (back) back->click();
                settle();
            }

            // Every motion and button of a program profile can be changed on its own: another action, Clear, Switch on and the way
            // back to inheriting (motion_edit_model.hpp); the actions are written as V_PUSH_ACTION ... TWIST_ACTION of the profile.
            check(page->selectSection(2) && page->selectTab(QStringLiteral("motions")), "program profile firefox, Motions and buttons tab");
            settle();
            auto itemText = [&](const char *id) {
                for (const MotionPicture::Item &it : mv->picture()->items())
                    if (it.id == QLatin1String(id)) return it.text;
                return QString();
            };
            auto find = [&](const char *name) { return mv->inspector()->findChild<QPushButton *>(QLatin1String(name)); };
            auto stateWords = [&]() {
                QLabel *l = mv->inspector()->findChild<QLabel *>(QStringLiteral("motionStateWords"));
                return l ? l->text() : QString();
            };
            // another action: the twist zooms in this program (set here), then back to what it inherited
            check(mv->select(QStringLiteral("m:twist")), "select the twist of the program profile");
            settle();
            QComboBox *act = mv->inspector()->findChild<QComboBox *>(QStringLiteral("motionAction"));
            check(act != nullptr, "a motion has the list of actions");
            if (act) {
                const int idx = act->findData(int(spn::ChannelActionType::Zoom));
                act->setCurrentIndex(idx);
                emit act->activated(idx);
                settle();
                check(itemText("m:twist").startsWith(QStringLiteral("Zoom")) && stateWords().contains(QStringLiteral("Set in this profile")),
                      "another action for the twist is set here and said in words");
                check(find("motionInherit") && find("motionInherit")->isEnabled(), "a motion set here has Use the inherited setting");
                if (find("motionInherit")) find("motionInherit")->click();
                settle();
                check(!stateWords().contains(QStringLiteral("Set in this profile")) && (stateWords().startsWith(QStringLiteral("On.")) || stateWords().startsWith(QStringLiteral("Off."))),
                      "Use the inherited setting goes back to inheriting");
            }
            // clear only the tilt of the horizontal direction: the push keeps its action
            const QString pushBefore = [&] { return itemText("m:h_push"); }();
            check(mv->select(QStringLiteral("m:h_tilt")), "select the horizontal tilt");
            settle();
            check(find("motionClear") && find("motionClear")->isEnabled(), "a motion can be cleared");
            if (find("motionClear")) find("motionClear")->click();
            settle();
            check(itemText("m:h_tilt") == QStringLiteral("Off") && stateWords().startsWith(QStringLiteral("Cleared in this profile")),
                  "Clear switches the tilt off in this profile and says so");
            check(itemText("m:h_push") == pushBefore && itemText("m:h_push") != QStringLiteral("Off"), "the push of the same direction keeps its action");
            check(mv->select(QStringLiteral("m:h_tilt")) && find("motionSwitchOn") && !find("motionSwitchOn")->isHidden(), "a cleared motion offers Switch on");
            if (find("motionSwitchOn")) find("motionSwitchOn")->click();
            settle();
            check(itemText("m:h_tilt") != QStringLiteral("Off") && !stateWords().contains(QStringLiteral("Cleared")), "Switch on makes the tilt work again");
            // a motion the joystick mode switched off can be switched on in the program profile
            check(mv->select(QStringLiteral("m:v_push")), "select the vertical push");
            settle();
            if (find("motionSwitchOn") && !find("motionSwitchOn")->isHidden()) {
                find("motionSwitchOn")->click();
                settle();
                check(itemText("m:v_push") != QStringLiteral("Off"), "Switch on gives a motion the mode switched off an action in this profile");
            }
            // a button: Clear writes an explicit "nothing" for this profile, Inherit takes it back
            QString btn;
            for (const MotionPicture::Item &it : mv->picture()->items())
                if (it.id.startsWith(QLatin1String("b:")) && btn.isEmpty()) btn = it.id;
            if (!btn.isEmpty()) {
                const QByteArray bid = btn.toLatin1();
                check(mv->select(btn), "select a button of the program profile");
                settle();
                check(find("buttonClear") != nullptr, "a button has Clear");
                if (find("buttonClear")) find("buttonClear")->click();
                settle();
                check(itemText(bid.constData()) == QStringLiteral("Cleared"), "Clear writes an explicit nothing for the button and says so");
                QToolButton *back = nullptr;
                for (QToolButton *t : mv->inspector()->findChildren<QToolButton *>())
                    if (t->text() == QStringLiteral("Inherit")) back = t;
                check(back != nullptr, "a cleared button has Inherit");
                if (back) back->click();
                settle();
                check(itemText(bid.constData()) != QStringLiteral("Cleared"), "Inherit takes the explicit nothing back");
            }
        }
    }
    // Button modes page: the device buttons of the Global profile, bind the selected button mode to one of them
    {
        ButtonModesPage *bp = w.findChild<ButtonModesPage *>();
        check(bp != nullptr && bp->useWidget() != nullptr, "the Button modes page has the buttons picture");
        if (bp && bp->useWidget()) {
            ButtonUseWidget *uw = bp->useWidget();
            check(!bp->currentId().isEmpty() || bp->buttonModeCount() == 0 || bp->selectButtonMode(QStringLiteral("next-mode")), "a button mode is selected");
            QString button;
            for (const MotionPicture::Item &it : uw->picture()->items())
                if (it.id.startsWith(QLatin1String("b:")) && button.isEmpty()) button = it.id;
            check(!button.isEmpty() && uw->select(button), "a button can be selected in the picture");
            for (const MotionPicture::Item &it : uw->picture()->items()) check(!it.id.startsWith(QLatin1String("m:")), "no motions on the Button modes page");
            settle();
            check(uw->useButton()->isEnabled() && uw->useButton()->toolTip().startsWith(QStringLiteral("Use \"")), "Use <button mode> on button n is offered");
            uw->useButton()->click();
            settle();
            check(uw->status().startsWith(QStringLiteral("Used on buttons: ")) && uw->status().endsWith(QStringLiteral("(Global profile)")), "binding it is said in words");
            check(uw->unbindButton()->isEnabled(), "and can be undone");
            uw->unbindButton()->click();
            settle();
            check(uw->status().startsWith(QStringLiteral("Not used on a button")), "Unbind removes it");
        }
    }
    check(page->selectSection(0) && page->headerName() == QStringLiteral("Global") &&
              page->headerKind() == QStringLiteral("Global profile") && page->headerHasIcon() &&
              page->headerNote().startsWith(QStringLiteral("Applies to every device and program")),
          "the header of Global: icon, name, kind and the inheritance sentence");
    check(page->selectSection(1) && page->headerName() == QStringLiteral("SpaceMouse Pro") &&
              page->headerKind() == QStringLiteral("Device profile") &&
              page->headerNote().startsWith(QStringLiteral("Everything you do not change here is inherited from Global")),
          "the header of a device profile");
    check(page->selectSection(3) && page->headerName() == QStringLiteral("blender") &&
              page->headerKind() == QStringLiteral("Program profile"),
          "the header of a program profile");

    // the tab stays when another profile is selected
    for (const QString &tab : allTabs) {
        check(page->selectTab(tab), "select a tab");
        // (not the blender profile: its output is off, which hides every tab but General)
        for (size_t s : {size_t(0), size_t(2), size_t(1)})
            check(page->selectSection(s) && page->currentTabId() == tab, "the tab stays for every profile");
    }
    page->selectTab(QStringLiteral("general"));

    // no Save, Revert or Discard button, and no unsaved-changes dialog exists anywhere
    for (const QString &name : {QStringLiteral("Save"), QStringLiteral("Revert"), QStringLiteral("Discard"), QStringLiteral("Save changes")})
        check(buttonTexted(&w, name) == nullptr, "no Save or Revert button in the window");

    // the Channels tab edits the selected profile
    check(page->selectSection(1) && page->selectTab(QStringLiteral("channels")), "device profile, Channels tab");
    auto *sens = qobject_cast<QSpinBox *>(ch->editorFor("SCROLL_V_SENS"));
    check(sens && sens->value() == 9 && ch->section() == 1, "the Channels tab shows the value of the selected profile");
    check(ch->originFor("SCROLL_V_SENS").startsWith(QStringLiteral("Set in this profile")), "the origin says it is set in this profile");
    check(ch->originFor("SCROLL_H_SENS").startsWith(QStringLiteral("Inherited from Global")), "an inherited value says where it comes from");
    check(ch->resetFor("SCROLL_V_SENS")->isEnabled() && !ch->resetFor("SCROLL_H_SENS")->isEnabled(), "Reset only where the profile has its own value");
    const int reloadsBefore = rig.reloads();
    if (sens) sens->setValue(11);  // a committed value: written at once (next event loop turn)
    check(rig.saver()->pending(), "the change is scheduled");
    check(rig.waitSaved(), "and written");
    {
        const spn::ui::SettingsModel d = rig.disk();
        const std::string *v = keyIn(sectionOf(d, Section::DEVICE, "SpaceMouse Pro"), "SCROLL_V_SENS");
        check(v && *v == "11", "the file holds the new value of the device profile");
        check(rig.reloads() == reloadsBefore + 1, "the daemon was told once");
        check(d.effective(0).scroll_v.sensitivity == 5, "Global is untouched");
    }
    // Global: plain values, and "N profiles override this"
    check(page->selectSection(0), "Global");
    check(ch->section() == 0 && ch->originFor("SCROLL_V_SENS").contains(QStringLiteral("1 profile overrides this")),
          "Global says that a profile overrides the value");
    check(!ch->originFor("SCROLL_H_SENS").contains(QStringLiteral("override")), "no hint where no profile overrides");
    auto *gsens = qobject_cast<QSpinBox *>(ch->editorFor("SCROLL_H_SENS"));
    if (gsens) gsens->setValue(7);
    check(rig.waitSaved(), "Global written");
    check(diskKey(rig, Section::GLOBAL, "", "SCROLL_H_SENS") &&
              *diskKey(rig, Section::GLOBAL, "", "SCROLL_H_SENS") == "7",
          "the file holds the value of Global");
    check(page->selectSection(3) && qobject_cast<QSpinBox *>(ch->editorFor("SCROLL_H_SENS"))->value() == 7 &&
              ch->originFor("SCROLL_H_SENS").startsWith(QStringLiteral("Inherited from Global")),
          "another profile inherits it and says so");
    // Reset removes the key
    check(page->selectSection(1), "device profile again");
    ch->resetFor("SCROLL_V_SENS")->click();
    check(rig.waitSaved() && !diskKey(rig, Section::DEVICE, "SpaceMouse Pro", "SCROLL_V_SENS"),
          "Reset to inherited removes the key from the file");
    // setting a value back to 0 is written, not dropped
    sens = qobject_cast<QSpinBox *>(ch->editorFor("SCROLL_V_SENS"));
    if (sens) sens->setValue(0);
    check(rig.waitSaved(), "0 written");
    {
        std::optional<std::string> v = diskKey(rig, Section::DEVICE, "SpaceMouse Pro", "SCROLL_V_SENS");
        check(v && *v == "0", "a value of 0 is written");
    }

    // text: written after a pause (600 ms), an invalid text is not written
    check(page->selectSection(0), "Global for the text fields");
    auto *mix = qobject_cast<QLineEdit *>(ch->editorFor("SCROLL_V_MIX"));
    check(mix != nullptr, "the mix field exists");
    if (mix) {
        const QString before = rig.confText();
        const int reloadsBeforeText = rig.reloads();
        typeInto(mix, QStringLiteral("q + z"));  // invalid: no such axis
        check(!rig.saver()->pending() && rig.confText() == before && rig.reloads() == reloadsBeforeText,
              "an invalid text is not written");
        bool problemShown = false;
        for (QLabel *l : ch->findChildren<QLabel *>())
            problemShown = problemShown || (l->text().contains(QStringLiteral("Invalid value: ")) && spn::ui::hasMark(l->text().toUtf8().toStdString()));
        check(problemShown, "the problem is shown next to the field, with its symbol");
        check(mix->property("invalid").toBool(), "the field is marked invalid");
        QElapsedTimer t;
        t.start();
        typeInto(mix, QStringLiteral("z + 0.5*rx"));
        check(rig.saver()->pending() && rig.confText() == before, "a valid text waits for the pause");
        check(rig.waitSaved(), "written after the pause");
        check(t.elapsed() >= 500, "the pause is about 600 ms");
        std::optional<std::string> v = diskKey(rig, Section::GLOBAL, "", "SCROLL_V_MIX");
        check(v && v->find("0.5") != std::string::npos, "the file holds the typed mix");
        check(!mix->property("invalid").toBool(), "the mark goes away");
        check(rig.reloads() == reloadsBeforeText + 1, "one reload for the whole typing");
    }
    // typing and switching the profile at once: nothing is lost
    if (mix) {
        typeInto(mix, QStringLiteral("z - rx"));
        check(rig.saver()->pending(), "typed text is pending");
        page->selectSection(1);  // the profile switch writes it at once
        check(!rig.saver()->pending(), "the profile switch flushed it");
        std::optional<std::string> v = diskKey(rig, Section::GLOBAL, "", "SCROLL_V_MIX");
        check(v && v->find("- rx") != std::string::npos, "the text typed before the switch is on disk");
    }
    // invert: on and off are both written
    check(page->selectSection(2), "program profile");
    auto *sign = qobject_cast<QCheckBox *>(ch->editorFor("SCROLL_V_SIGN"));
    if (sign) {
        sign->setChecked(true);
        check(rig.waitSaved(), "sign written");
        std::optional<std::string> v = diskKey(rig, Section::APP, "firefox", "SCROLL_V_SIGN");
        check(v && *v == "-1", "the inverted direction is written");
        sign->setChecked(false);
        check(rig.waitSaved(), "sign written again");
        v = diskKey(rig, Section::APP, "firefox", "SCROLL_V_SIGN");
        check(v && *v == "1", "unchecking is written (the profile overrides the inherited direction)");
    }
    // the joystick mode: choosing "Same as Global" removes the key
    check(page->selectSection(2) && page->selectTab(QStringLiteral("general")), "General tab of firefox");
    {
        auto *mode = findNamed<QComboBox>(page, QStringLiteral("Joystick mode"));
        check(mode && mode->currentText().contains(QStringLiteral("imeline")), "the program profile shows its joystick mode");
        check(mode && mode->itemText(0) == QStringLiteral("Same as Global"), "the first choice is 'Same as Global'");
        if (mode) QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 0));
        check(rig.waitSaved() && !diskKey(rig, Section::APP, "firefox", "MODE"), "'Same as Global' removes MODE from the file");
        settle();
        mode = findNamed<QComboBox>(page, QStringLiteral("Joystick mode"));
        if (mode) QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 2));  // another joystick mode
        check(rig.waitSaved() && diskKey(rig, Section::APP, "firefox", "MODE"), "a joystick mode is written");
    }
    // Global takes a joystick mode too
    check(page->selectSection(0), "Global General tab");
    {
        settle();
        auto *mode = findNamed<QComboBox>(page, QStringLiteral("Joystick mode"));
        check(mode && mode->itemText(0) != QStringLiteral("Same as Global"), "Global has no 'Same as Global' choice");
        check(mode && mode->count() >= 3, "Global offers the joystick modes");
        if (mode) QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 2));
        check(rig.waitSaved(), "Global mode written");
        std::optional<std::string> v = diskKey(rig, Section::GLOBAL, "", "MODE");
        check(v && *v != "normal", "the joystick mode of Global is in the file");
        QLabel *badge = nullptr;
        for (QLabel *l : page->findChildren<QLabel *>())
            if (l->objectName() == QStringLiteral("modeOrigin")) badge = l;
        check(badge && badge->text().contains(QStringLiteral("override")), "Global says which profiles choose another joystick mode");
        mode = findNamed<QComboBox>(page, QStringLiteral("Joystick mode"));
        if (mode) QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 0));  // back to normal
        check(rig.waitSaved(), "Global mode written back");
    }
    // descriptions: every joystick mode in the drop-down carries its description as tooltip; the "i" button explains
    check(page->selectSection(2) && page->selectTab(QStringLiteral("general")), "General tab for the descriptions");
    {
        settle();
        auto *mode = findNamed<QComboBox>(page, QStringLiteral("Joystick mode"));
        check(mode != nullptr, "the joystick mode drop-down exists");
        if (mode) {
            bool allTips = true, sawOff = false;
            for (int i = 0; i < mode->count(); ++i) {
                const QString tip = mode->itemData(i, Qt::ToolTipRole).toString();
                allTips = allTips && !tip.isEmpty();
                sawOff = sawOff || (mode->itemText(i).startsWith(QStringLiteral("Off")) && tip.contains(QStringLiteral("Sends nothing")));
            }
            check(allTips, "every entry of the joystick mode drop-down has a tooltip");
            check(sawOff, "the tooltip of a built-in entry is its description");
            check(mode->itemData(0, Qt::ToolTipRole).toString().startsWith(QStringLiteral("Uses the joystick mode of Global")),
                  "'Same as Global' says which joystick mode it is");
        }
        QToolButton *info = findNamed<QToolButton>(page, QStringLiteral("About this joystick mode"));
        check(info != nullptr, "the 'i' button exists next to the drop-down");
        if (info) {
            info->click();
            settle();
            const QString text = InfoPopup::openText();
            // Same Wayland limitation as in keyboardSelfTest: without an input serial the grabbing
            // popup is refused, the popup is closed again during settle() and deleted
            // (WA_DeleteOnClose), so there is no text left. The content check runs whenever there is
            // text (X11, offscreen, CI) and whenever the popover maps on Wayland. See issue #193.
            if (text.isEmpty() && QGuiApplication::platformName().startsWith(QStringLiteral("wayland"))) {
                std::printf(
                    "profile editor self-test: info popover content check skipped on Wayland (no input serial for the grabbing popup)\n");
            } else {
                check(text.contains(QStringLiteral("Vertical push:")) && text.contains(QStringLiteral("Twist:")) &&
                          text.contains(QStringLiteral("Built in")) && text.contains(QStringLiteral("Sends nothing")) && text.startsWith(QStringLiteral("Off")),
                      "the info popover names the joystick mode, where it comes from and what its channels do");
            }
            for (QWidget *top : QApplication::topLevelWidgets())
                if (top->objectName() == QLatin1String("infoPopup")) top->close();
        }
    }
    // a button that switches the joystick mode: the entries of its argument drop-down have tooltips
    check(page->selectSection(1) && page->selectButton(1), "button 1 on the Motions and buttons tab for the descriptions");
    {
        settle();
        auto *act = findNamed<QComboBox>(page, QStringLiteral("Action of button 1"));
        int sw = -1;
        for (int i = 0; act && i < act->count(); ++i)
            if (act->itemText(i) == QStringLiteral("Switch to joystick mode")) sw = i;
        check(sw >= 0, "Switch to joystick mode is offered");
        if (act && sw >= 0) QMetaObject::invokeMethod(act, "activated", Q_ARG(int, sw));
        rig.waitSaved();
        settle();
        auto *arg = findNamed<QComboBox>(page, QStringLiteral("Joystick mode of button 1"));
        check(arg != nullptr, "the joystick mode of the button can be chosen");
        if (arg) {
            bool allTips = arg->count() >= 3;
            for (int i = 0; i < arg->count(); ++i) allTips = allTips && !arg->itemData(i, Qt::ToolTipRole).toString().isEmpty();
            check(allTips, "every joystick mode of the button's drop-down has a tooltip");
        }
        act = findNamed<QComboBox>(page, QStringLiteral("Action of button 1"));
        if (act) QMetaObject::invokeMethod(act, "activated", Q_ARG(int, 0));  // back to inherit
        rig.waitSaved();
    }
    // enabled: a switch per profile, written both ways, Reset removes the key
    check(page->selectSection(3) && page->selectTab(QStringLiteral("general")), "General tab of blender");
    {
        settle();
        auto *en = findNamed<QCheckBox>(page, QStringLiteral("Send events"));
        check(en && !en->isChecked(), "blender is switched off");
        check(findNamed<QComboBox>(page, QStringLiteral("Joystick mode")) == nullptr && !page->tabVisible(QStringLiteral("motions")),
              "with output off the joystick mode and the Motions and buttons tab are hidden");
        if (en) en->setChecked(true);
        check(rig.waitSaved(), "enabled written");
        settle();  // the page is rebuilt: the joystick mode and the tabs come back
        std::optional<std::string> v = diskKey(rig, Section::APP, "blender", "ENABLED");
        check(v && *v == "1", "switching on is written");
        check(findNamed<QComboBox>(page, QStringLiteral("Joystick mode")) != nullptr && page->tabVisible(QStringLiteral("motions")),
              "with output on the joystick mode and the Motions and buttons tab are shown");
        en = findNamed<QCheckBox>(page, QStringLiteral("Send events"));
        {
            auto *modeBox = findNamed<QComboBox>(page, QStringLiteral("Joystick mode"));
            check(en && modeBox && en->mapTo(page, QPoint(0, 0)).y() < modeBox->mapTo(page, QPoint(0, 0)).y(),
                  "the output switch is above the joystick mode");
        }
        if (en) en->setChecked(false);
        check(rig.waitSaved(), "disabled written");
        settle();
        v = diskKey(rig, Section::APP, "blender", "ENABLED");
        check(v && *v == "0", "switching off is written");
        QToolButton *reset = nullptr;
        for (QToolButton *b : page->findChildren<QToolButton *>())
            if (b->accessibleName() == QStringLiteral("Reset to inherited ENABLED")) reset = b;
        check(reset && reset->isEnabled(), "Reset to inherited exists for the output switch");
        if (reset) reset->click();
        check(rig.waitSaved() && !diskKey(rig, Section::APP, "blender", "ENABLED"), "Reset removes ENABLED");
    }
    // Global has no output switch on the profile page: it is on the Settings page
    check(page->selectSection(0), "Global");
    settle();
    check(findNamed<QCheckBox>(page, QStringLiteral("Send events")) == nullptr, "no setting appears in two places: the output switch is not in Global");

    // buttons: the effective action and its origin, 'Inherit' first, written and removed again
    check(page->selectSection(1) && page->selectButton(1), "button 1 on the Motions and buttons tab of the device profile");
    settle();
    {
        auto *origin = findNamed<QLabel>(page, QStringLiteral("Origin of button 1"));
        check(origin && origin->text().startsWith(QStringLiteral("Inherited from Global")), "a button row says where its action comes from");
        auto *act = findNamed<QComboBox>(page, QStringLiteral("Action of button 1"));
        check(act && act->itemText(0) == QStringLiteral("Default (inherit from Global)"), "'Default (inherit)' is the first choice");
        int click = -1;
        if (act)
            for (int i = 0; i < act->count(); ++i)
                if (act->itemText(i) == QStringLiteral("Left click")) click = i;
        check(click >= 0, "the click action is offered");
        if (act && click >= 0) QMetaObject::invokeMethod(act, "activated", Q_ARG(int, click));
        check(rig.waitSaved(), "button written");
        settle();
        {   // every row shows the shared button chip; a change of the action changes its text
            int chips = 0;
            QString first;
            for (auto *c : findAllOf<motionview::ButtonChips>(page))
                if (c->objectName() == QLatin1String("buttonChip") && c->isVisibleTo(page)) {
                    ++chips;
                    if (c->accessibleName() == QStringLiteral("Chip of button 1")) first = c->chipTexts().value(0);
                }
            check(chips > 0 && first == QStringLiteral("1 Click"), "every button row shows its chip, and it follows the drop-down");
        }
        std::optional<std::string> v = diskKey(rig, Section::DEVICE, "SpaceMouse Pro", "BUTTON_1");
        check(v && *v == "click", "the button action is written");
        settle();
        origin = findNamed<QLabel>(page, QStringLiteral("Origin of button 1"));
        check(origin && origin->text().startsWith(QStringLiteral("Set in this profile")), "the row now says it is set in this profile");
        act = findNamed<QComboBox>(page, QStringLiteral("Action of button 1"));
        if (act) QMetaObject::invokeMethod(act, "activated", Q_ARG(int, 0));  // Default (inherit)
        check(rig.waitSaved() && !diskKey(rig, Section::DEVICE, "SpaceMouse Pro", "BUTTON_1"),
              "Default (inherit) removes the binding from the file");
    }

    // the Overrides tab: typed values are validated, written after a pause; Remove removes the key
    check(page->selectSection(1) && page->selectTab(QStringLiteral("overrides")), "Overrides tab");
    settle();
    {
        auto *edit = findNamed<QLineEdit>(page, QStringLiteral("SCROLL_V_SENS"));
        check(edit != nullptr, "the override of the device profile is listed");
        if (edit) {
            const QString before = rig.confText();
            typeInto(edit, QStringLiteral("abc"));
            check(!rig.saver()->pending() && rig.confText() == before, "an invalid override text is not written");
            typeInto(edit, QStringLiteral("13"));
            check(rig.waitSaved(), "valid text written");
            std::optional<std::string> v = diskKey(rig, Section::DEVICE, "SpaceMouse Pro", "SCROLL_V_SENS");
            check(v && *v == "13", "the file holds the override");
        }
        QToolButton *rm = nullptr;
        for (QToolButton *b : page->findChildren<QToolButton *>())
            if (b->accessibleName() == QStringLiteral("Remove SCROLL_V_SENS")) rm = b;
        if (rm) rm->click();
        check(rig.waitSaved() && !diskKey(rig, Section::DEVICE, "SpaceMouse Pro", "SCROLL_V_SENS"),
              "Remove removes the override from the file");
    }

    // restore defaults of a profile: only its own channel values go
    check(page->selectSection(1) && page->selectTab(QStringLiteral("channels")), "Channels tab");
    ch->editorFor("SCROLL_H_DEADZONE");
    if (auto *dz = qobject_cast<QSpinBox *>(ch->editorFor("SCROLL_H_DEADZONE"))) dz->setValue(33);
    check(rig.waitSaved() && diskKey(rig, Section::DEVICE, "SpaceMouse Pro", "SCROLL_H_DEADZONE"), "deadzone written");
    check(ch->restoreDefaults() && rig.waitSaved() && !diskKey(rig, Section::DEVICE, "SpaceMouse Pro", "SCROLL_H_DEADZONE"),
          "Restore defaults removes the values of the profile");

    // the search box applies to the Channels tab too
    page->setSettingsFilter(QStringLiteral("scroll_h_sens"));
    check(ch->visibleRowCount() == 1, "the settings search finds one channel row");
    page->setSettingsFilter(QString());
    check(ch->visibleRowCount() == ch->rowCount(), "an empty search shows every row");

    // the search also finds buttons on the Motions and buttons tab: by BUTTON_<n>, none for a text that matches nothing
    {
        auto buttonItems = [&] {
            int n = 0;
            for (const MotionPicture::Item &it : page->motionsView()->picture()->items()) n += it.id.startsWith(QLatin1String("b:"));
            return n;
        };
        check(page->selectSection(1) && page->selectTab(QStringLiteral("motions")), "Motions and buttons tab for the search");
        settle();
        const int all = buttonItems();
        page->setSettingsFilter(QStringLiteral("button_3"));
        settle();
        check(all > 1 && buttonItems() >= 1 && buttonItems() < all, "the search keeps only the matching buttons");
        page->setSettingsFilter(QStringLiteral("no-such-setting"));
        settle();
        check(buttonItems() == 0, "a search without a match shows no button");
        page->setSettingsFilter(QString());
        settle();
        check(buttonItems() == all, "an empty search shows every button again");
    }

    // export of the selected profile
    check(page->selectSection(3), "blender");
    {
        const QString file = rig.dir.path() + QStringLiteral("/export-blender.conf");
        check(page->exportSelected(file), "export writes a file");
        spn::ProfileSet ps;
        check(spn::loadProfiles(file.toStdString(), &ps, nullptr) && ps.sections.size() == 2 && ps.sections[1].kind == Section::APP &&
                  ps.sections[1].pattern == "blender",
              "the exported file holds the section of the profile");
        check(page->selectSection(0) && page->exportSelected(rig.dir.path() + QStringLiteral("/export-global.conf")) &&
                  rig.dir.path().size() > 0,
              "Global can be exported too");
    }

    // Overview task: 'Tune scroll and zoom' opens the Channels tab of Global
    w.showPage(QStringLiteral("overview"));
    settle();
    if (auto *tune = w.findChild<QPushButton *>(QStringLiteral("task-tune"))) tune->click();
    settle();
    check(w.currentPageId() == QStringLiteral("profiles") && page->selectedSection() == 0 && page->currentTabId() == QStringLiteral("channels"),
          "'Tune scroll and zoom' opens the Channels tab of the Global profile");

    // accessible names of every input on every tab
    for (const QString &id : allTabs) {
        page->selectTab(id);
        settle();
        check(accessibility(w), "every input of the tab has an accessible name");
    }

    // a page switch writes what is typed
    w.showPage(QStringLiteral("profiles"));
    page->selectSection(0);
    page->selectTab(QStringLiteral("channels"));
    settle();
    if (mix) {
        typeInto(qobject_cast<QLineEdit *>(ch->editorFor("SCROLL_H_MIX")), QStringLiteral("x + 0.25*ry"));
        check(rig.saver()->pending(), "typed text pending before the page switch");
        w.showPage(QStringLiteral("overview"));
        check(!rig.saver()->pending(), "the page switch flushed it");
        std::optional<std::string> v = diskKey(rig, Section::GLOBAL, "", "SCROLL_H_MIX");
        check(v && v->find("0.25") != std::string::npos, "the text is on disk after the page switch");
    }
    // closing the window writes what is typed
    w.showPage(QStringLiteral("profiles"));
    if (auto *e2 = qobject_cast<QLineEdit *>(ch->editorFor("ZOOM_MIX"))) {
        typeInto(e2, QStringLiteral("rz"));
        check(rig.saver()->pending(), "typed text pending before the close");
        QCloseEvent ev;
        QApplication::sendEvent(&w, &ev);
        check(!rig.saver()->pending(), "closing the window flushed it");
        std::optional<std::string> v = diskKey(rig, Section::GLOBAL, "", "ZOOM_MIX");
        check(v && *v == "rz", "the text is on disk after the close");
        w.show();
        settle();
    }
    // quitting writes what is typed
    w.showPage(QStringLiteral("profiles"));
    if (auto *e3 = qobject_cast<QLineEdit *>(ch->editorFor("ZOOM_MIX"))) {
        typeInto(e3, QStringLiteral("rx"));
        check(w.flushPending() && !rig.saver()->pending(), "the quit path flushes");
        std::optional<std::string> v = diskKey(rig, Section::GLOBAL, "", "ZOOM_MIX");
        check(v && *v == "rx", "the text is on disk after the flush of the quit path");
    }
    std::printf("profile editor self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// ---- autosave: the joystick mode editor and the button mode page ----
// Valid changes are written by themselves, an invalid form is not, closing and switching write the last
// change, and there is no Save, Revert or Discard button.
static bool modeFilesAutosaveSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "mode files autosave self-test failed: %s\n", what);
        ok = ok && cond;
    };
    auto fileText = [&](const QString &rel) {
        QFile f(tmpPath + rel);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    auto waitFor = [](const std::function<bool()> &done, int ms) {
        QElapsedTimer t;
        t.start();
        while (!done() && t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
        return done();
    };
    {
        ModesPage page(ctx);
        page.show();
        settle();
        // the list shows the description of every joystick mode in a second line; none says "No description"
        {
            auto *list = page.findChild<QListWidget *>();
            check(list && list->count() >= 4, "the joystick mode list exists");
            bool normalOk = false, mineOk = false, allSet = true;
            for (int i = 0; list && i < list->count(); ++i) {
                const QString desc = list->item(i)->data(kRoleDescription).toString();
                allSet = allSet && !desc.isEmpty();
                if (list->item(i)->text().startsWith(QStringLiteral("normal")) || list->item(i)->text().startsWith(QStringLiteral("Normal")))
                    normalOk = !desc.isEmpty() && desc != QStringLiteral("No description");
                if (list->item(i)->text().startsWith(QStringLiteral("Mine"))) mineOk = desc == QStringLiteral("No description");
            }
            check(allSet && normalOk && mineOk, "every list row has a description line ('No description' where there is none)");
            check(list && list->itemDelegate() != nullptr, "the list has its own row painter (name, badge, description, six motion chips)");
        }
        check(page.selectMode(QStringLiteral("mine")) && page.openEditor() && page.editorWidget(), "the editor opens");
        ModeEditorWidget *ed = page.editorWidget();
        ed->selectTab(QStringLiteral("settings"));
        {
            QLineEdit *desc = lineEditNamed(ed, QStringLiteral("Description"));
            check(desc && desc->placeholderText() == QStringLiteral("No description"), "the description field is always there");
        }
        for (const QString &name : {QStringLiteral("Save"), QStringLiteral("Revert"), QStringLiteral("Discard")})
            check(buttonTexted(&page, name) == nullptr, "no Save or Revert button in the joystick mode editor");
        // typed text: written after a pause
        ed->editor().form().name = "Typed name";
        ed->edited(true);
        check(ed->pending() && !fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("Typed name")), "typed text waits for the pause");
        check(waitFor([&] { return !ed->pending(); }, 3000) && fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("NAME = Typed name")),
              "and is written after it");
        check(page.originOf(QStringLiteral("mine")) == QStringLiteral("Your file") && page.originOf(QStringLiteral("normal")) == QStringLiteral("Built in"),
              "the list says where each joystick mode comes from");
        // a drop-down (button binding) is written at once
        ed->editor().form().setButton(3, "key:enter");
        ed->edited(false);
        check(waitFor([&] { return !ed->pending(); }, 2000) && fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("BUTTON_3 = key:enter")),
              "a button binding is written");
        // removing the binding removes the line
        ed->editor().form().setButton(3, "none");
        ed->edited(false);
        check(waitFor([&] { return !ed->pending(); }, 2000) && !fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("BUTTON_3")),
              "removing a binding removes it from the file");
        // an invalid form is not written and says why
        const QString before = fileText(QStringLiteral("/modes/mine.conf"));
        ed->editor().form().overrides.emplace_back("SCROLL_H_SENS", "abc");
        ed->edited(true);
        check(!ed->pending() && !ed->problemText().isEmpty(), "an invalid form is not scheduled and shows the problem");
        QThread::msleep(700);
        QApplication::processEvents();
        check(fileText(QStringLiteral("/modes/mine.conf")) == before, "the file stays as it was");
        check(!ed->flush(), "a flush of an invalid form reports it");
        ed->editor().form().overrides.back().second = "3";
        ed->edited(true);
        check(ed->pending(), "fixed: written again");
        // closing writes the last change at once
        ed->editor().form().description = "Closed with a pending change";
        ed->edited(true);
        QPushButton *close = buttonTexted(ed, QStringLiteral("Close editor"));
        check(close != nullptr, "Close editor exists");
        if (close) close->click();
        check(!ed->pending() && fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("Closed with a pending change")),
              "Close editor writes the pending change");
        // switching to another joystick mode writes it too
        check(page.selectMode(QStringLiteral("mine")) && page.openEditor(), "reopen");
        ed->editor().form().name = "Switch edit";
        ed->edited(true);
        page.selectMode(QStringLiteral("timeline"));
        check(!ed->pending() && fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("NAME = Switch edit")),
              "selecting another joystick mode writes the pending change");
        // the file changed on disk meanwhile: the question has a test seam, "no" keeps the file
        check(page.selectMode(QStringLiteral("mine")) && page.openEditor(), "reopen again");
        {
            QFile f(tmpPath + QStringLiteral("/modes/mine.conf"));
            check(f.open(QIODevice::Append) && f.write("# edited elsewhere\n") > 0, "change the file behind the editor");
        }
        int asked = 0;
        ed->setOverwriteAnswer([&asked] {
            ++asked;
            return false;
        });
        ed->editor().form().name = "Not overwriting";
        ed->edited(false);
        check(waitFor([&] { return !ed->pending(); }, 2000) && asked == 1 &&
                  !fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("Not overwriting")) &&
                  fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("edited elsewhere")),
              "a file changed elsewhere is not overwritten without a yes");
        ed->setOverwriteAnswer([] { return true; });
        ed->edited(false);
        check(waitFor([&] { return !ed->pending(); }, 2000) && fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("NAME = Not overwriting")),
              "with a yes it is");
        // restore the original name for the later tests
        ed->editor().form().name = "Mine";
        ed->edited(false);
        ed->flush();
        // split push and tilt (issue #100): the switch, the two action rows, the tuning fields, autosave
        {
            check(page.selectMode(QStringLiteral("mine")) && page.openEditor(), "reopen for the split test");
            ed->selectTab(QStringLiteral("settings"));
            auto splitSwitch = [&](const QString &channel) -> QCheckBox * {
                for (QCheckBox *cb : ed->findChildren<QCheckBox *>())
                    if (cb->accessibleName() == channel + QStringLiteral(": Split push and tilt")) return cb;
                return nullptr;
            };
            QCheckBox *vs = splitSwitch(QStringLiteral("Vertical push"));
            QCheckBox *hs = splitSwitch(QStringLiteral("Horizontal push"));
            check(vs && hs && !vs->isChecked() && !hs->isChecked(), "both channels have a 'Split push and tilt' switch, off by default");
            if (vs) vs->click();
            check(waitFor([&] { return !ed->pending(); }, 2000) && fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("V_PUSH_ACTION = default")) &&
                      fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("V_TILT_ACTION = default")) &&
                      !fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("H_PUSH_ACTION")),
                  "the switch writes both V keys and leaves H alone");
            // the push and tilt action rows exist with their own pickers
            QComboBox *pushKind = nullptr, *tiltKind = nullptr;
            for (QComboBox *cb : ed->findChildren<QComboBox *>()) {
                if (cb->accessibleName() == QStringLiteral("Vertical push")) pushKind = cb;
                if (cb->accessibleName() == QStringLiteral("Vertical tilt")) tiltKind = cb;
            }
            check(pushKind && tiltKind, "split: a push and a tilt action row");
            if (tiltKind) {
                const int idx = tiltKind->findData(int(spn::ChannelActionType::Off));
                check(idx >= 0, "the tilt row offers 'Off'");
                tiltKind->setCurrentIndex(idx);
                emit tiltKind->activated(idx);
            }
            check(waitFor([&] { return !ed->pending(); }, 2000) && fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("V_TILT_ACTION = none")),
                  "a part action is written");
            // per-part tuning is a field of the part
            QLineEdit *sens = lineEditNamed(ed, QStringLiteral("Vertical tilt: Sensitivity"));
            check(sens != nullptr, "split: tuning field of the tilt part");
            if (sens) {
                sens->setText(QStringLiteral("8"));
                emit sens->textEdited(QStringLiteral("8"));
            }
            check(waitFor([&] { return !ed->pending(); }, 3000) && fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("SCROLL_V_TILT_SENS = 8")),
                  "the tuning field writes the per-part key");
            if (sens) {
                sens->setText(QStringLiteral("abc"));
                emit sens->textEdited(QStringLiteral("abc"));
            }
            check(!ed->pending() && !ed->problemText().isEmpty(), "an invalid tuning value is shown and not written");
            if (sens) {
                sens->clear();
                emit sens->textEdited(QString());
            }
            check(waitFor([&] { return !ed->pending(); }, 3000) && ed->problemText().isEmpty() &&
                      !fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("SCROLL_V_TILT_SENS")),
                  "an empty tuning field removes the key (follows the channel)");
            // switching it off removes the keys again
            vs = splitSwitch(QStringLiteral("Vertical push"));
            if (vs) vs->click();
            check(waitFor([&] { return !ed->pending(); }, 2000) && !fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("V_PUSH_ACTION")) &&
                      !fileText(QStringLiteral("/modes/mine.conf")).contains(QStringLiteral("V_TILT_ACTION")),
                  "switching the split off removes the keys");
            ed->flush();
        }
    }
    {   // the button mode page
        ButtonModesPage page(ctx);
        page.show();
        settle();
        for (const QString &name : {QStringLiteral("Save"), QStringLiteral("Revert"), QStringLiteral("Discard")})
            check(buttonTexted(&page, name) == nullptr, "no Save or Revert button on the button modes page");
        check(page.selectButtonMode(QStringLiteral("latch")), "select the button mode");
        {
            QFile nodesc(tmpPath + QStringLiteral("/buttons/nodesc.conf"));
            check(nodesc.open(QIODevice::WriteOnly) && nodesc.write("ACTION = click\n") > 0, "a button mode without a description");
            nodesc.close();
            page.refreshIfChanged();
            auto *list = page.findChild<QListWidget *>();
            bool said = false;
            for (int i = 0; list && i < list->count(); ++i)
                if (list->item(i)->text().startsWith(QStringLiteral("nodesc")))
                    said = list->item(i)->data(kRoleDescription).toString() == QStringLiteral("No description");
            check(said, "a button mode without a description says 'No description' in the list");
            QFile::remove(tmpPath + QStringLiteral("/buttons/nodesc.conf"));
            page.refreshIfChanged();
            page.selectButtonMode(QStringLiteral("latch"));
            QLineEdit *desc = lineEditNamed(&page, QStringLiteral("Description"));
            check(desc && desc->placeholderText() == QStringLiteral("No description"), "the description field is always there");
        }
        check(page.stageEdit(QStringLiteral("Typed"), QStringLiteral("d"), QStringLiteral("toggle:nav")), "type into the form");
        {
            auto *list = page.findChild<QListWidget *>();
            check(list && (list->item(0)->data(kRoleDescription).toString() == QStringLiteral("d") || page.pending()),
                  "the description shows in the list once written");
        }
        check(page.pending(), "the change is pending");
        check(waitFor([&] { return !page.pending(); }, 3000) && fileText(QStringLiteral("/buttons/latch.conf")).contains(QStringLiteral("NAME = Typed")),
              "written after the pause");
        check(page.originOf(QStringLiteral("latch")) == QStringLiteral("Your file"), "the list says the button mode is your file");
        // export: a copy of the file, never over an existing one
        check(buttonTexted(&page, QStringLiteral("Export...")) != nullptr, "Export... exists on the button modes page");
        {
            const QString copy = tmpPath + QStringLiteral("/exported-latch.conf");
            QFile::remove(copy);
            check(page.exportButtonModeTo(QStringLiteral("latch"), copy) && QFile::exists(copy), "export writes a copy of the button mode file");
            check(!page.exportButtonModeTo(QStringLiteral("latch"), copy) && !page.lastError().isEmpty(), "export never overwrites");
            QFile::remove(copy);
        }
        // a drop-down is written at once (a key action comes with the default key, so it is complete)
        auto *kind = findNamed<QComboBox>(&page, QStringLiteral("Action"));
        check(kind != nullptr, "the action drop-down exists");
        if (kind) {
            int key = -1;
            for (int i = 0; i < kind->count(); ++i)
                if (kind->itemText(i) == QStringLiteral("Press key")) key = i;
            check(key >= 0, "Press key is offered");
            if (key >= 0) QMetaObject::invokeMethod(kind, "activated", Q_ARG(int, key));
            check(waitFor([&] { return !page.pending(); }, 2000) && fileText(QStringLiteral("/buttons/latch.conf")).contains(QStringLiteral("ACTION = key:space")),
                  "the action is written at once");
        }
        // typing and switching: nothing is lost
        QFile other(tmpPath + QStringLiteral("/buttons/other.conf"));
        check(other.open(QIODevice::WriteOnly) && other.write("NAME = Other\nACTION = click\n") > 0, "a second button mode");
        other.close();
        page.refreshIfChanged();
        check(page.selectButtonMode(QStringLiteral("other")), "select it");
        check(page.stageEdit(QStringLiteral("Other edited"), QString(), QStringLiteral("click")) && page.pending(), "type into it");
        page.selectButtonMode(QStringLiteral("latch"));
        check(!page.pending() && fileText(QStringLiteral("/buttons/other.conf")).contains(QStringLiteral("NAME = Other edited")),
              "selecting another button mode writes the pending change");
        QFile::remove(tmpPath + QStringLiteral("/buttons/other.conf"));
        page.refreshIfChanged();
        // restore
        page.selectButtonMode(QStringLiteral("latch"));
        page.editCurrent(QStringLiteral("Latch"), QString(), QStringLiteral("toggle:nav"));
    }
    std::printf("mode files autosave self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// "New from example...": the examples that ship with dofscroll and are not in the folder yet are copied
// into the folder of the user (both pages), and every list entry says where it comes from.
static bool examplesSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "examples self-test failed: %s\n", what);
        ok = ok && cond;
    };
    QDir().mkpath(tmpPath + QStringLiteral("/ex/modes"));
    QDir().mkpath(tmpPath + QStringLiteral("/ex/buttons"));
    for (const auto &f : {std::make_pair("/ex/modes/ex-one.conf", "NAME = Example one\nV_ACTION = none\n"),
                          std::make_pair("/ex/buttons/ex-btn.conf", "NAME = Example button\nACTION = click\n")}) {
        QFile file(tmpPath + QString::fromLatin1(f.first));
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write(f.second);
    }
    qputenv("DOFSCROLL_EXAMPLES", (tmpPath + QStringLiteral("/ex")).toUtf8());
    {
        ModesPage page(ctx);
        page.show();
        settle();
        check(page.modeCount() >= 4, "the built-in joystick modes and the files are listed");
        check(page.exampleIds().contains(QStringLiteral("ex-one")), "the example is offered");
        check(buttonTexted(&page, QStringLiteral("New from example...")) != nullptr, "New from example... exists");
        check(page.installExample(QStringLiteral("ex-one")) && QFile::exists(tmpPath + QStringLiteral("/modes/ex-one.conf")), "install a joystick mode example");
        check(!page.exampleIds().contains(QStringLiteral("ex-one")) && page.originOf(QStringLiteral("ex-one")) == QStringLiteral("Your file"),
              "it is not offered again and is now your file");
        check(page.currentId() == QStringLiteral("ex-one"), "the new joystick mode is selected");
        check(!page.installExample(QStringLiteral("ex-one")) && !page.lastError().isEmpty(), "installing twice is refused");
        check(page.originOf(QStringLiteral("normal")) == QStringLiteral("Built in"), "built-in joystick modes say so");
        QFile::remove(tmpPath + QStringLiteral("/modes/ex-one.conf"));
    }
    {
        ButtonModesPage page(ctx);
        page.show();
        settle();
        check(page.exampleIds().contains(QStringLiteral("ex-btn")), "the button mode example is offered");
        check(buttonTexted(&page, QStringLiteral("New from example...")) != nullptr, "New from example... exists on the button modes page");
        check(page.installExample(QStringLiteral("ex-btn")) && QFile::exists(tmpPath + QStringLiteral("/buttons/ex-btn.conf")), "install a button mode example");
        check(page.originOf(QStringLiteral("ex-btn")) == QStringLiteral("Your file") && !page.exampleIds().contains(QStringLiteral("ex-btn")),
              "it is your file now");
        QFile::remove(tmpPath + QStringLiteral("/buttons/ex-btn.conf"));
    }
    qunsetenv("DOFSCROLL_EXAMPLES");
    std::printf("examples self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Runs `act` (which opens a modal dialog and blocks in its event loop) and calls `handle` on the dialog once it is up;
// `handle` must accept or reject it. false when no dialog appeared.
static bool withModal(const std::function<void()> &act, const std::function<void(QDialog *)> &handle) {
    bool handled = false;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        if (handled) return;
        if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
            handled = true;
            handle(d);
        }
    });
    poll.start(30);
    act();
    poll.stop();
    return handled;
}

// Take over from other profiles (A), any action on any motion (B) and the profile builder (C): the dialogs of the Motions and
// buttons tab, of the Joystick-mode editor and "New profile ...", with the model underneath (borrow_model.hpp, profile_builder_model.hpp).
static bool borrowBuildSelfTest() {
    using spn::Section;
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "take over / profile builder self-test failed: %s\n", what);
        ok = ok && cond;
    };
    EditorRig rig("[global]\n\n[device:SpaceMouse Pro]\nSCROLL_V_SENS = 9\n\n[app:firefox]\nMODE = timeline\n\n"
                  "[app:blender]\nV_TILT_ACTION = zoom\nSCROLL_V_TILT_SENS = 8\nSCROLL_V_TILT_SIGN = -1\nBUTTON_2 = key:space\n");
    MainWindow &w = *rig.win;
    w.showPage(QStringLiteral("profiles"));
    settle();
    ProfilesPage *page = w.profilesPage();
    check(page != nullptr, "the window has a Profiles page");
    if (!page) return false;
    ProfileMotionsView *mv = page->motionsView();
    auto find = [&](const char *name) { return mv->inspector()->findChild<QPushButton *>(QLatin1String(name)); };
    auto itemText = [&](const char *id) {
        for (const MotionPicture::Item &it : mv->picture()->items())
            if (it.id == QLatin1String(id)) return it.text;
        return QString();
    };

    // A: take the tilt over from the program "blender" into "firefox"
    check(page->selectSection(2) && page->selectTab(QStringLiteral("motions")) && mv != nullptr, "program profile firefox, Motions and buttons tab");
    if (!mv) return false;
    check(mv->select(QStringLiteral("m:v_tilt")), "select the vertical tilt");
    settle();
    check(find("motionTakeOver") != nullptr, "a motion has Take over from ...");
    QString words;
    bool listed = false;
    const bool opened = withModal([&] { if (find("motionTakeOver")) find("motionTakeOver")->click(); }, [&](QDialog *d) {
        QComboBox *src = d->findChild<QComboBox *>(QStringLiteral("takeOverSource"));
        QLabel *wl = d->findChild<QLabel *>(QStringLiteral("takeOverWords"));
        int pick = -1;
        for (int i = 0; src && i < src->count(); ++i) {
            if (src->itemText(i) == QStringLiteral("Program profile: blender")) pick = i;
            listed = listed || src->itemText(i).startsWith(QStringLiteral("Joystick mode: "));
            check(src->itemText(i) != QStringLiteral("Program profile: firefox"), "not the profile itself");
        }
        check(pick >= 0 && src, "the sources list the other profiles");
        if (src && pick >= 0) src->setCurrentIndex(pick);
        words = wl ? wl->text() : QString();
        QCheckBox *tune = d->findChild<QCheckBox *>(QStringLiteral("takeOverTuning"));
        check(tune && !tune->isChecked(), "a motion's dialog has 'Also take over the tuning', off by default");
        d->accept();
    });
    check(opened && listed, "the dialog lists the joystick modes too");
    check(words.contains(QStringLiteral("zoom")) && words.contains(QStringLiteral("copies")), "the dialog says in words what is copied");
    settle();
    check(rig.waitSaved(), "taking a motion over is saved by itself");
    {
        const spn::ui::SettingsModel disk = rig.disk();
        const std::string *v = keyIn(sectionOf(disk, Section::APP, "firefox"), "V_TILT_ACTION");
        check(v && *v == "zoom", "V_TILT_ACTION = zoom is copied into the profile");
        check(!keyIn(sectionOf(disk, Section::APP, "firefox"), "V_PUSH_ACTION"), "the push of the same direction is not touched");
        check(!keyIn(sectionOf(disk, Section::APP, "blender"), "V_PUSH_ACTION"), "the source is not touched");
    }
    check(itemText("m:v_tilt").startsWith(QStringLiteral("Zoom")), "the tilt of the program now zooms, in the picture");
    {
        const spn::ui::SettingsModel disk = rig.disk();
        check(!keyIn(sectionOf(disk, Section::APP, "firefox"), "SCROLL_V_TILT_SENS"), "by default the tuning is not copied");
    }

    // A with the tuning: the check box copies sensitivity, dead zone, direction, curve and smoothing of that motion as well
    QString tuneWords;
    const bool openedTuned = withModal([&] { if (find("motionTakeOver")) find("motionTakeOver")->click(); }, [&](QDialog *d) {
        QComboBox *src = d->findChild<QComboBox *>(QStringLiteral("takeOverSource"));
        QLabel *wl = d->findChild<QLabel *>(QStringLiteral("takeOverWords"));
        QCheckBox *tune = d->findChild<QCheckBox *>(QStringLiteral("takeOverTuning"));
        for (int i = 0; src && i < src->count(); ++i)
            if (src->itemText(i) == QStringLiteral("Program profile: blender")) src->setCurrentIndex(i);
        check(tune != nullptr, "the check box is there");
        if (tune) tune->setChecked(true);
        tuneWords = wl ? wl->text() : QString();
        d->accept();
    });
    check(openedTuned && tuneWords.contains(QStringLiteral("sensitivity 8")) && tuneWords.contains(QStringLiteral("reversed")),
          "with the check box the dialog lists the tuning in words");
    settle();
    check(rig.waitSaved(), "taking a motion over with its tuning is saved by itself");
    {
        const spn::ui::SettingsModel disk = rig.disk();
        const auto *sec = sectionOf(disk, Section::APP, "firefox");
        const std::string *sens = keyIn(sec, "SCROLL_V_TILT_SENS"), *sign = keyIn(sec, "SCROLL_V_TILT_SIGN");
        check(sens && *sens == "8" && sign && *sign == "-1" && keyIn(sec, "SCROLL_V_TILT_DEADZONE") && keyIn(sec, "SCROLL_V_TILT_SMOOTH_MS"),
              "the tuning of the tilt is copied as explicit keys");
        check(!keyIn(sec, "SCROLL_V_PUSH_SENS") && !keyIn(sectionOf(disk, Section::APP, "blender"), "SCROLL_V_TILT_DEADZONE"),
              "other motions and the source are not touched");
    }

    // B: the list of actions of any motion offers Zoom, with the words about what it does from a tilt
    check(mv->select(QStringLiteral("m:h_tilt")), "select the horizontal tilt");
    settle();
    {
        QComboBox *act = mv->inspector()->findChild<QComboBox *>(QStringLiteral("motionAction"));
        check(act != nullptr, "the tilt has the list of actions");
        if (act) {
            const int idx = act->findData(int(spn::ChannelActionType::Zoom));
            check(idx >= 0 && act->model()->flags(act->model()->index(idx, 0)).testFlag(Qt::ItemIsEnabled), "Zoom is offered for a tilt");
            act->setCurrentIndex(idx);
            emit act->activated(idx);
            settle();
            QLabel *note = mv->inspector()->findChild<QLabel *>(QStringLiteral("motionChoiceNote"));
            check(note && !note->isHidden() && note->text().contains(QStringLiteral("own channel")) && note->text().contains(QStringLiteral("pause")),
                  "zoom from a tilt is explained in words");
            check(itemText("m:h_tilt").startsWith(QStringLiteral("Zoom")), "the horizontal tilt zooms");
        }
    }

    // A for a button: from another profile (and the list also offers the button modes)
    QString btn;
    for (const MotionPicture::Item &it : mv->picture()->items())
        if (it.id.startsWith(QLatin1String("b:")) && btn.isEmpty()) btn = it.id;
    check(!btn.isEmpty() && mv->select(btn), "select a button");
    settle();
    check(find("buttonTakeOver") != nullptr, "a button has Take over from ...");
    bool sawButtonMode = false;
    withModal([&] { if (find("buttonTakeOver")) find("buttonTakeOver")->click(); }, [&](QDialog *d) {
        QComboBox *src = d->findChild<QComboBox *>(QStringLiteral("takeOverSource"));
        int pick = -1;
        for (int i = 0; src && i < src->count(); ++i) {
            sawButtonMode = sawButtonMode || src->itemText(i).startsWith(QStringLiteral("Button mode: "));
            if (src->itemText(i) == QStringLiteral("Program profile: blender")) pick = i;
        }
        if (src && pick >= 0) src->setCurrentIndex(pick);
        d->accept();
    });
    check(sawButtonMode, "a button can also be taken over from a button mode");
    settle();
    check(rig.waitSaved(), "taking a button over is saved by itself");
    check(keyIn(sectionOf(rig.disk(), Section::APP, "firefox"), "BUTTON_" + btn.mid(2).toStdString()) != nullptr,
          "BUTTON_<n> is an explicit key of the profile now");

    // C: New profile ...
    QString modeId;
    {
        QPushButton *np = page->findChild<QPushButton *>(QStringLiteral("newProfile"));
        check(np != nullptr, "the Profiles page has New profile ...");
        const bool built = withModal([&] { if (np) np->click(); }, [&](QDialog *d) {
            auto *create = d->findChild<QPushButton *>(QStringLiteral("builderCreate"));
            auto *name = d->findChild<QLineEdit *>(QStringLiteral("builderName"));
            auto *mode = d->findChild<QComboBox *>(QStringLiteral("builderMode"));
            auto *copy = d->findChild<QCheckBox *>(QStringLiteral("builderModeCopy"));
            check(create && name && mode && copy, "the builder has its fields");
            if (!(create && name && mode && copy)) {
                d->reject();
                return;
            }
            check(!create->isEnabled(), "without a name nothing can be created");
            name->setText(QStringLiteral("FireFox"));
            check(!create->isEnabled() && d->findChild<QLabel *>(QStringLiteral("builderProblem"))->text().contains(QStringLiteral("already exists")),
                  "a name that exists is refused (matching ignores the case) and the reason is shown");
            name->setText(QStringLiteral("gimp"));
            check(create->isEnabled(), "a new name can be created");
            int photo = -1;
            for (int i = 1; i < mode->count(); ++i)
                if (mode->itemText(i).contains(QStringLiteral("(photo)"))) photo = i;
            check(photo > 0, "the joystick modes are listed");
            check(!copy->isEnabled(), "no copy without a joystick mode");
            mode->setCurrentIndex(photo);
            check(copy->isEnabled(), "a joystick mode can be followed or copied");
            copy->setChecked(true);
            check(d->findChild<QLabel *>(QStringLiteral("builderModeWords"))->text().contains(QStringLiteral("own copy")), "the copy is said in words");
            if (auto *add = d->findChild<QPushButton *>(QStringLiteral("builderAddMotion"))) add->click();
            auto *motion = d->findChild<QComboBox *>(QStringLiteral("builderMotion"));
            auto *source = d->findChild<QComboBox *>(QStringLiteral("builderMotionSource"));
            check(motion && source, "a motion can be taken over from another profile");
            if (motion && source) {
                motion->setCurrentIndex(motion->findData(QStringLiteral("v_tilt")));
                for (int i = 0; i < source->count(); ++i)
                    if (source->itemText(i) == QStringLiteral("Program profile: blender")) source->setCurrentIndex(i);
            }
            if (auto *add = d->findChild<QPushButton *>(QStringLiteral("builderAddButton"))) add->click();
            auto *bnum = d->findChild<QSpinBox *>(QStringLiteral("builderButtonNumber"));
            auto *bmode = d->findChild<QComboBox *>(QStringLiteral("builderButtonMode"));
            check(bnum && bmode, "a button mode can be chosen per button");
            if (bnum && bmode) {
                bnum->setValue(3);
                for (int i = 0; i < bmode->count(); ++i)
                    if (bmode->itemData(i).toString() == QStringLiteral("latch-nav")) bmode->setCurrentIndex(i);
            }
            create->click();
        });
        check(built, "the builder opened");
        settle();
        check(rig.waitSaved(), "the new profile is saved by itself");
        const spn::ui::SettingsModel disk = rig.disk();
        const spn::Section *g = sectionOf(disk, Section::APP, "gimp");
        check(g != nullptr, "the profile exists in the configuration");
        if (g) {
            const std::string *mode = keyIn(g, "MODE");
            check(mode && *mode != "photo" && mode->find("photo") == 0, "an own copy of the joystick mode is named, not the original");
            if (mode) {
                modeId = QString::fromStdString(*mode);
                check(QFile::exists(QString::fromStdString(rig.ctx.modesDir) + QLatin1Char('/') + modeId + QStringLiteral(".conf")), "the copy is a file in the own folder");
            }
            const std::string *tilt = keyIn(g, "V_TILT_ACTION");
            check(tilt && *tilt == "zoom", "the taken-over motion is an explicit key");
            const std::string *b3 = keyIn(g, "BUTTON_3");
            check(b3 && *b3 == "buttonmode:latch-nav", "the button mode is followed by name");
        }
        check(page->selectedSection() != 0 && page->headerName() == QStringLiteral("gimp"), "the new profile is selected");
        // a normal profile afterwards: editable on the Motions and buttons tab
        check(page->selectTab(QStringLiteral("motions")), "Motions and buttons tab of the new profile");
        check(mv->select(QStringLiteral("m:v_tilt")), "the tilt of the new profile");
        settle();
        check(find("motionInherit") && find("motionInherit")->isEnabled(), "the taken-over motion can be set back to inheriting");
        if (find("motionInherit")) find("motionInherit")->click();
        settle();
        check(rig.waitSaved(), "editing the new profile is saved");
        check(!keyIn(sectionOf(rig.disk(), Section::APP, "gimp"), "V_TILT_ACTION"), "Use the inherited setting removes it again");
    }

    // A in the Joystick-mode editor: the tilt of the program "blender" is taken into the own copy the builder made
    if (!modeId.isEmpty()) {
        ModesPage *mp = w.findChild<ModesPage *>();
        check(mp != nullptr, "the window has a Joystick modes page");
        if (mp) {
            w.showPage(QStringLiteral("modes"));
            settle();
            mp->refreshIfChanged();
            check(mp->selectMode(modeId) && mp->openEditor() && mp->editorWidget(), "the editor opens the own copy");
            ModeEditorWidget *ed = mp->editorWidget();
            if (ed && ed->motionEditor()) {
                MotionEditor *me = ed->motionEditor();
                check(me->select(QStringLiteral("m:h_push")), "select a motion in the joystick mode");
                settle();
                auto *take = me->inspector()->findChild<QPushButton *>(QStringLiteral("modeTakeOverMotion"));
                check(take != nullptr, "the joystick mode editor has Take over from ... for a motion");
                withModal([&] { if (take) take->click(); }, [&](QDialog *d) {
                    QComboBox *src = d->findChild<QComboBox *>(QStringLiteral("takeOverSource"));
                    for (int i = 0; src && i < src->count(); ++i) {
                        check(src->itemText(i) != QStringLiteral("Joystick mode: ") + modeId, "a mode is not offered itself");
                        if (src->itemText(i) == QStringLiteral("Program profile: firefox")) src->setCurrentIndex(i);  // firefox zooms with its horizontal tilt
                    }
                    d->accept();
                });
                settle();
                ed->flush();
                QFile f(QString::fromStdString(rig.ctx.modesDir) + QLatin1Char('/') + modeId + QStringLiteral(".conf"));
                check(f.open(QIODevice::ReadOnly) && ed->problemText().isEmpty(), "taking a motion over keeps the file valid");
                check(me->select(QStringLiteral("b:1")), "select a button in the joystick mode");
                settle();
                check(me->inspector()->findChild<QPushButton *>(QStringLiteral("modeTakeOverButton")) != nullptr,
                      "the joystick mode editor has Take over from ... for a button");
            }
        }
    }
    std::printf("take over / profile builder self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// The old NAV_BUTTON / CLICK_BUTTON settings are moved to BUTTON_<n> when the window starts, the file is
// written, the user is told once, and what cannot be moved stays and is listed.
static bool legacyButtonsSelfTest() {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "legacy buttons self-test failed: %s\n", what);
        ok = ok && cond;
    };
    EditorRig rig("[global]\nNAV_BUTTON = 2\nNAV_BUTTON_ENABLED = 1\nCLICK_BUTTON = 1\nCLICK_BUTTON_ENABLED = 0\nBUTTON_4 = key:space\n");
    const QString text = rig.confText();
    check(!text.contains(QStringLiteral("NAV_BUTTON")) && !text.contains(QStringLiteral("CLICK_BUTTON")), "the old keys are gone from the file");
    check(text.contains(QStringLiteral("BUTTON_3 = nav")) && text.contains(QStringLiteral("BUTTON_4 = key:space")), "the navigation button became BUTTON_3 = nav");
    check(rig.reloads() >= 1, "the daemon was told");
    check(rig.win->saveStatus().contains(QStringLiteral("Moved the old navigation/click button settings to the Motions and buttons tab")),
          "the user is told once in the status line");
    ProfilesPage *page = rig.win->profilesPage();
    check(page != nullptr, "Profiles page");
    if (page) {
        rig.win->showPage(QStringLiteral("profiles"));
        page->selectTab(QStringLiteral("motions"));
        settle();
        QLabel *note = nullptr;
        for (QLabel *l : page->findChildren<QLabel *>())
            if (l->objectName() == QStringLiteral("legacyButtonNotes")) note = l;
        check(note && note->text().contains(QStringLiteral("BUTTON_3")), "the Motions and buttons tab lists what was moved");
        check(findNamed<QSpinBox>(page, QStringLiteral("Navigation button")) == nullptr, "there is no navigation button setting any more");
    }
    // a button above the count of the device: kept untouched and listed
    EditorRig rig2("[global]\nNAV_BUTTON = 40\nNAV_BUTTON_ENABLED = 1\n");
    check(rig2.confText().contains(QStringLiteral("NAV_BUTTON = 40")), "an unmovable setting stays in the file");
    ProfilesPage *p2 = rig2.win->profilesPage();
    if (p2) {
        rig2.win->showPage(QStringLiteral("profiles"));
        p2->selectTab(QStringLiteral("motions"));
        settle();
        QLabel *note = nullptr;
        for (QLabel *l : p2->findChildren<QLabel *>())
            if (l->objectName() == QStringLiteral("legacyButtonNotes")) note = l;
        check(note && note->text().contains(QStringLiteral("Not moved")), "and is listed as a note");
    }
    std::printf("legacy buttons self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// ---- the split layout: one file per profile (docs/plan-config-split.md) ----
// Editing, importing, moving, renaming and removing profiles writes the files of exactly those profiles, a
// defective file is shown in words and never overwritten, and a file changed by hand is not overwritten.
static bool profileFilesSelfTest() {
    using spn::Section;
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "profile files self-test failed: %s\n", what);
        ok = ok && cond;
    };
    EditorRig rig("[global]\n\n[app:firefox]\nZOOM_SENS = 3\n\n[app:vlc]\nZOOM_SENS = 4\n");
    MainWindow &w = *rig.win;
    w.showPage(QStringLiteral("profiles"));
    settle();
    ProfilesPage *page = w.profilesPage();
    check(page != nullptr, "the window has a Profiles page");
    if (!page) return false;
    const QString dir = QFileInfo(rig.confPath()).absolutePath();
    auto path = [&](const QString &rel) { return dir + QLatin1Char('/') + rel; };
    auto text = [&](const QString &rel) {
        QFile f(path(rel));
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    auto put = [&](const QString &rel, const QByteArray &data) {
        QFile f(path(rel));
        const bool wrote = f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
        f.close();
        return wrote;
    };
    auto exists = [&](const QString &rel) { return QFileInfo::exists(path(rel)); };
    auto wait = [&](int ms = 3000) {  // until nothing is pending; a failed save is checked by the caller
        QElapsedTimer t;
        t.start();
        while (rig.saver()->pending() && t.elapsed() < ms) QApplication::processEvents(QEventLoop::AllEvents, 20);
        QApplication::processEvents();
        return !rig.saver()->pending();
    };
    auto labelIndex = [&](const QString &start) {
        const QStringList l = page->profileListLabels();
        for (int i = 0; i < l.size(); ++i)
            if (l[i].startsWith(start)) return i;
        return -1;
    };
    auto selectByName = [&](const QString &name) {
        for (size_t s = 1; s < 16; ++s)
            if (page->selectSection(s) && page->headerName() == name) return true;
        return false;
    };
    auto editDeadzone = [&](int v) {
        page->selectTab(QStringLiteral("channels"));
        auto *dz = qobject_cast<QSpinBox *>(page->channelsEditor()->editorFor("SCROLL_H_DEADZONE"));
        if (dz) dz->setValue(v);
        return dz != nullptr;
    };

    // the old single file: editing one profile moves only that profile into its own file
    check(selectByName(QStringLiteral("firefox")) && editDeadzone(33), "edit firefox");
    check(wait() && !rig.saver()->failed() && exists(QStringLiteral("apps/firefox.conf")) &&
              text(QStringLiteral("apps/firefox.conf")).contains(QStringLiteral("SCROLL_H_DEADZONE = 33")),
          "the edited profile is written into apps/firefox.conf");
    check(!exists(QStringLiteral("apps/vlc.conf")) && text(QStringLiteral("dofscroll.conf")).contains(QStringLiteral("[app:vlc]")) &&
              !text(QStringLiteral("dofscroll.conf")).contains(QStringLiteral("[app:firefox]")),
          "the other profile stays in dofscroll.conf, the moved one is gone from it");
    const QByteArray firefoxFile = text(QStringLiteral("apps/firefox.conf")).toUtf8();

    // Import: a new profile gets a file of its own, its PRIORITY is not taken over (it is applied before the others)
    check(put(QStringLiteral("import-gimp.conf"), "[app:gimp]\nZOOM_SENS = 2\nPRIORITY = 500\n") &&
              page->importFrom(path(QStringLiteral("import-gimp.conf")), false) && wait() && exists(QStringLiteral("apps/gimp.conf")) &&
              !text(QStringLiteral("apps/gimp.conf")).contains(QStringLiteral("PRIORITY = 500")),
          "Import writes apps/gimp.conf");
    check(text(QStringLiteral("apps/firefox.conf")).toUtf8() == firefoxFile, "an import writes no other file");
    // a name that exists: kept unless the user chooses Replace
    check(put(QStringLiteral("import-gimp2.conf"), "[app:GIMP]\nZOOM_SENS = 9\n") &&
              !page->importFrom(path(QStringLiteral("import-gimp2.conf")), false) && wait() &&
              text(QStringLiteral("apps/gimp.conf")).contains(QStringLiteral("ZOOM_SENS = 2")),
          "Keep leaves the existing profile alone");
    check(page->importFrom(path(QStringLiteral("import-gimp2.conf")), true) && wait() &&
              text(QStringLiteral("apps/gimp.conf")).contains(QStringLiteral("ZOOM_SENS = 9")) &&
              text(QStringLiteral("apps/gimp.conf")).contains(QStringLiteral("[app:gimp]")),
          "Replace changes the existing profile (its own name stays)");
    // a file that is no profile is refused with a message, nothing is written
    check(put(QStringLiteral("import-global.conf"), "[global]\nSCROLL_V_SENS = 3\n") &&
              !page->importFrom(path(QStringLiteral("import-global.conf")), true) && !rig.saver()->pending(),
          "a Global file is not imported");
    // export is a copy of the profile file, and Import takes it again
    check(selectByName(QStringLiteral("gimp")) && page->exportSelected(path(QStringLiteral("export-gimp.conf"))) &&
              text(QStringLiteral("export-gimp.conf")) == text(QStringLiteral("apps/gimp.conf")),
          "Export copies the profile file");

    // the order: the list follows it, Move up and Move down change PRIORITY, the words say it
    // (vlc still lives in dofscroll.conf and ties with firefox on 0: the profiles of the old file come first)
    check(labelIndex(QStringLiteral("gimp")) < labelIndex(QStringLiteral("vlc")) &&
              labelIndex(QStringLiteral("vlc")) < labelIndex(QStringLiteral("firefox")),
          "the list shows the profiles in the order they are applied (an imported profile first)");
    check(selectByName(QStringLiteral("gimp")) && page->orderNote().contains(QStringLiteral("Applied first")),
          "the order is said in words");
    check(page->moveSelected(false) && wait() && !rig.saver()->failed(), "Move down");
    check(labelIndex(QStringLiteral("vlc")) < labelIndex(QStringLiteral("gimp")) &&
              labelIndex(QStringLiteral("gimp")) < labelIndex(QStringLiteral("firefox")),
          "the profile moved in the list");
    check(page->selectedSection() != 0 && page->headerName() == QStringLiteral("gimp") &&
              page->orderNote().contains(QStringLiteral("Applied after \"vlc\"")),
          "the moved profile stays selected");
    {
        const spn::ui::SettingsModel disk = rig.disk();
        const size_t g = disk.profiles().find(Section::APP, "gimp"), f = disk.profiles().find(Section::APP, "firefox"),
                     v = disk.profiles().find(Section::APP, "vlc");
        check(g != spn::ProfileSet::npos && f != spn::ProfileSet::npos && v != spn::ProfileSet::npos &&
                  disk.priority(v) < disk.priority(g) && disk.priority(g) < disk.priority(f) && !disk.inSingleFile(v),
              "the new order is on disk (PRIORITY), vlc moved into its file");
    }
    check(page->moveSelected(false) && wait() && labelIndex(QStringLiteral("vlc")) < labelIndex(QStringLiteral("gimp")),
          "Move down again");

    // rename: the new file is written, the old one removed, the order stays
    check(selectByName(QStringLiteral("vlc")) && page->renameSelected(QStringLiteral("VLC media player")) && wait() &&
              !rig.saver()->failed(),
          "rename vlc");
    check(!exists(QStringLiteral("apps/vlc.conf")) &&
              text(QStringLiteral("apps/vlc media player.conf")).contains(QStringLiteral("[app:VLC media player]")) &&
              text(QStringLiteral("apps/vlc media player.conf")).contains(QStringLiteral("ZOOM_SENS = 4")),
          "the new file was written and the old one removed");
    check(labelIndex(QStringLiteral("VLC media player")) < labelIndex(QStringLiteral("gimp")), "a rename keeps the place in the order");

    // remove: the file goes, without any question about unsaved changes
    check(selectByName(QStringLiteral("gimp")) && page->removeSelected() && wait() && !exists(QStringLiteral("apps/gimp.conf")) &&
              exists(QStringLiteral("apps/vlc media player.conf")) && exists(QStringLiteral("apps/firefox.conf")),
          "Remove deletes only that profile's file");

    // a defective file: its own entry in the list with the file name, explained in words, never overwritten
    const QByteArray broken = "[app:b]\n[app:c]\nZOOM_SENS = 1\n";
    check(put(QStringLiteral("apps/broken.conf"), broken), "write a defective file");
    {
        QElapsedTimer t;
        t.start();
        while (page->defectNotes().isEmpty() && t.elapsed() < 4000)  // the window follows the directory
            QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    check(page->defectNotes().contains(QStringLiteral("broken.conf")) && page->defectNotes().contains(QStringLiteral("defective")) &&
              labelIndex(QStringLiteral("broken.conf (defective)")) >= 0,
          "the defective file is listed by name, in words");
    check(selectByName(QStringLiteral("firefox")) && editDeadzone(44) && wait() && !rig.saver()->failed() &&
              text(QStringLiteral("apps/broken.conf")).toUtf8() == broken,
          "editing another profile never overwrites the defective file");
    QFile::remove(path(QStringLiteral("apps/broken.conf")));
    {
        QElapsedTimer t;
        t.start();
        while (!page->defectNotes().isEmpty() && t.elapsed() < 4000) QApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    check(page->defectNotes().isEmpty() && labelIndex(QStringLiteral("broken.conf")) < 0,
          "the entry goes when the file is repaired or removed");

    // a file changed by hand is never overwritten: the profile is reloaded and the problem shown next to it
    check(selectByName(QStringLiteral("firefox")) && editDeadzone(55), "edit firefox again");
    const QByteArray hand = "[app:firefox]\nZOOM_SENS = 8\n# by hand\n";
    check(put(QStringLiteral("apps/firefox.conf"), hand), "change the file by hand");
    check(wait() && rig.saver()->failed() && rig.saver()->statusText().contains(QStringLiteral("changed outside")),
          "the autosave says so");
    check(text(QStringLiteral("apps/firefox.conf")).toUtf8() == hand, "the hand-edited file was not overwritten");
    settle();
    check(selectByName(QStringLiteral("firefox")) && page->headerNote().contains(QStringLiteral("changed outside")),
          "the message is shown next to the profile");

    // the migration at the first start: the profiles come from their files, in the order of the old file
    EditorRig mig("[global]\n\n[app:zeta]\nZOOM_SENS = 3\n\n[app:alpha]\nZOOM_SENS = 4\n",
                  [](const QString &d) { spn::migrateToSplit(d.toStdString()); });
    mig.win->showPage(QStringLiteral("profiles"));
    settle();
    ProfilesPage *mp = mig.win->profilesPage();
    check(mp != nullptr && QFileInfo::exists(mig.dir.path() + QStringLiteral("/apps/zeta.conf")) &&
              !mig.confText().contains(QStringLiteral("[app:zeta]")),
          "the migration moved the profiles into files");
    if (mp) {
        const QStringList l = mp->profileListLabels();
        int z = -1, a = -1;
        for (int i = 0; i < l.size(); ++i) {
            if (l[i].startsWith(QStringLiteral("zeta"))) z = i;
            if (l[i].startsWith(QStringLiteral("alpha"))) a = i;
        }
        check(z >= 0 && a > z, "the list keeps the order of the old file");
    }
    std::printf("profile files self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// ---- audit: every editable control is persisted ----
// Table-driven: change the control programmatically, wait for the autosave (the real timer), read the file
// from disk like the daemon would and check the value; a cleared or unchecked state must be written (or
// the key removed), and no other key of the profile may change. The table is printed.
static bool autosaveAuditSelfTest(const std::function<bool(MainWindow &)> &) {
    using spn::Section;
    bool ok = true;
    int rows = 0;
    auto record = [&](const QString &element, bool persisted, const QString &how) {
        ++rows;
        std::printf("audit: %-58s persisted: %-3s (%s)\n", qPrintable(element), persisted ? "yes" : "NO", qPrintable(how));
        if (!persisted) {
            std::fprintf(stderr, "audit self-test failed: %s is not persisted\n", qPrintable(element));
            ok = false;
        }
    };
    EditorRig rig("[global]\nSCROLL_V_SENS = 5\nZOOM_LATERAL = x y\n\n[device:Dev]\n\n[app:audit-app]\nSCROLL_V_SIGN = -1\n");
    MainWindow &w = *rig.win;
    w.showPage(QStringLiteral("profiles"));
    ProfilesPage *page = w.profilesPage();
    ChannelsEditor *ch = page->channelsEditor();
    const size_t kGlobal = 0, kApp = rig.disk().profiles().find(Section::APP, "audit-app");

    // reads: the value of a key in a profile of the file, and the keys of the profile other than `except`
    auto valueOf = [&](size_t sec, const std::string &key) -> std::string {
        const spn::ui::SettingsModel d = rig.disk();
        const spn::Section &s = d.profiles().sections[sec];
        for (const spn::KV &kv : s.kv)
            if (kv.first == key) return kv.second;
        return std::string("<absent>");
    };
    auto snapshot = [&](size_t sec) {
        const spn::ui::SettingsModel d = rig.disk();
        return d.profiles().sections[sec].kv;
    };
    auto othersUnchanged = [&](const std::vector<spn::KV> &before, size_t sec, const std::string &key) {
        const std::vector<spn::KV> after = snapshot(sec);
        for (const spn::KV &b : before) {
            if (b.first == key) continue;
            bool found = false;
            for (const spn::KV &a : after)
                if (a.first == b.first && a.second == b.second) found = true;
            if (!found) return false;
        }
        return true;
    };
    auto go = [&](size_t sec, const char *tab) {
        w.showPage(QStringLiteral("profiles"));
        page->selectSection(sec);
        page->selectTab(QString::fromLatin1(tab));
        settle();
    };
    // one case: act, wait, compare the key in the file (expected "<absent>" = the key must be gone)
    auto expect = [&](const QString &element, size_t sec, const std::string &key, const std::string &want,
                      const std::function<void()> &act, bool prefix = false, bool groupReset = false) {
        const std::vector<spn::KV> before = snapshot(sec);
        act();
        const bool saved = rig.waitSaved();
        const std::string got = valueOf(sec, key);
        const bool match = prefix ? got.rfind(want, 0) == 0 : got == want;
        const bool others = groupReset || othersUnchanged(before, sec, key);
        record(element, saved && match && others,
               QStringLiteral("%1 = %2%3").arg(qs8(key), qs8(got), others ? QString() : QStringLiteral(", but another key changed")));
    };

    const struct { size_t sec; const char *name; } profiles[] = {{kGlobal, "Global"}, {kApp, "program profile"}};
    for (const auto &p : profiles) {
        const QString who = QString::fromLatin1(p.name);
        const size_t sec = p.sec;
        go(sec, "channels");
        // numbers: 0 and a value back to its default are written
        expect(who + QStringLiteral(": Sensitivity 0"), sec, "SCROLL_V_SENS", "0", [&] { qobject_cast<QSpinBox *>(ch->editorFor("SCROLL_V_SENS"))->setValue(0); });
        expect(who + QStringLiteral(": Sensitivity back to the default 5"), sec, "SCROLL_V_SENS", "5",
               [&] { qobject_cast<QSpinBox *>(ch->editorFor("SCROLL_V_SENS"))->setValue(5); });
        expect(who + QStringLiteral(": Deadzone 0"), sec, "SCROLL_H_DEADZONE", "0", [&] { qobject_cast<QSpinBox *>(ch->editorFor("SCROLL_H_DEADZONE"))->setValue(0); });
        expect(who + QStringLiteral(": Deadzone 40"), sec, "SCROLL_H_DEADZONE", "40", [&] { qobject_cast<QSpinBox *>(ch->editorFor("SCROLL_H_DEADZONE"))->setValue(40); });
        // [global] only holds keys that were set: a control that already shows the default writes nothing
        expect(who + QStringLiteral(": Smoothing 0"), sec, "ZOOM_SMOOTH_MS", sec == kGlobal ? "<absent>" : "0", [&] { qobject_cast<QDoubleSpinBox *>(ch->editorFor("ZOOM_SMOOTH_MS"))->setValue(0); });
        expect(who + QStringLiteral(": Smoothing 30"), sec, "ZOOM_SMOOTH_MS", "30", [&] { qobject_cast<QDoubleSpinBox *>(ch->editorFor("ZOOM_SMOOTH_MS"))->setValue(30); });
        expect(who + QStringLiteral(": Entry time 120"), sec, "ZOOM_ENTER_MS", "120", [&] { qobject_cast<QSpinBox *>(ch->editorFor("ZOOM_ENTER_MS"))->setValue(120); });
        expect(who + QStringLiteral(": Entry time 0"), sec, "ZOOM_ENTER_MS", "0", [&] { qobject_cast<QSpinBox *>(ch->editorFor("ZOOM_ENTER_MS"))->setValue(0); });
        // a different value than the one Global has just got: a spin box that does not change writes nothing
        const double ratio = sec == kGlobal ? 2.0 : 3.0, step = sec == kGlobal ? 55 : 65;
        expect(who + QStringLiteral(": Exit ratio"), sec, "ZOOM_EXIT_RATIO", sec == kGlobal ? "2" : "3",
               [&] { qobject_cast<QDoubleSpinBox *>(ch->editorFor("ZOOM_EXIT_RATIO"))->setValue(ratio); });
        expect(who + QStringLiteral(": Step trigger"), sec, "STEP_TRIGGER", sec == kGlobal ? "55" : "65",
               [&] { qobject_cast<QDoubleSpinBox *>(ch->editorFor("STEP_TRIGGER"))->setValue(step); });
        // direction: both ways
        expect(who + QStringLiteral(": Invert direction on"), sec, "SCROLL_H_SIGN", "-1", [&] { qobject_cast<QCheckBox *>(ch->editorFor("SCROLL_H_SIGN"))->setChecked(true); });
        expect(who + QStringLiteral(": Invert direction off (only the direction changes)"), sec, "SCROLL_H_SIGN", "1",
               [&] { qobject_cast<QCheckBox *>(ch->editorFor("SCROLL_H_SIGN"))->setChecked(false); });
        // curve: exponent, then points, then the points field cleared
        expect(who + QStringLiteral(": Curve exponent"), sec, "ZOOM_CURVE", "2.5", [&] { qobject_cast<QDoubleSpinBox *>(ch->editorFor("ZOOM_CURVE"))->setValue(2.5); });
        expect(who + QStringLiteral(": Curve exponent back to linear (1)"), sec, "ZOOM_CURVE", "1", [&] { qobject_cast<QDoubleSpinBox *>(ch->editorFor("ZOOM_CURVE"))->setValue(1.0); });
        {
            QLineEdit *pts = nullptr;
            for (QLineEdit *e : ch->findChildren<QLineEdit *>())
                if (e->toolTip().contains(QStringLiteral("SCROLL_V_CURVE")) && e->placeholderText().startsWith(QStringLiteral("empty"))) pts = e;
            if (pts) {
                expect(who + QStringLiteral(": Curve points typed"), sec, "SCROLL_V_CURVE", "0:0", [&] { typeInto(pts, QStringLiteral("0:0 100:1 350:3")); }, true);
                expect(who + QStringLiteral(": Curve points cleared (back to the exponent)"), sec, "SCROLL_V_CURVE", "1", [&] { typeInto(pts, QString()); });
            } else {
                record(who + QStringLiteral(": Curve points"), false, QStringLiteral("field not found"));
            }
        }
        expect(who + QStringLiteral(": Axis mix typed"), sec, "SCROLL_H_MIX", "x + 0.5*ry",
               [&] { typeInto(qobject_cast<QLineEdit *>(ch->editorFor("SCROLL_H_MIX")), QStringLiteral("x + 0.5*ry")); });
        // zoom detection: deselect every axis, select one again
        expect(who + QStringLiteral(": Zoom must dominate: all axes deselected"), sec, "ZOOM_LATERAL", "",
               [&] {
                   for (QCheckBox *c : ch->editorFor("ZOOM_LATERAL")->findChildren<QCheckBox *>()) c->setChecked(false);
               });
        {
            const spn::ui::SettingsModel d = rig.disk();
            record(who + QStringLiteral(": ... and reads back as no axis"), d.effective(sec).zoom_lateral.empty(), QStringLiteral("loaded from disk"));
        }
        expect(who + QStringLiteral(": Zoom must dominate: one axis selected"), sec, "ZOOM_LATERAL", "z",
               [&] {
                   for (QCheckBox *c : ch->editorFor("ZOOM_LATERAL")->findChildren<QCheckBox *>())
                       if (c->text() == QStringLiteral("z")) c->setChecked(true);
               });
        // Reset per row removes the key of a profile (Global: back to the default value)
        if (sec == kApp) {
            expect(who + QStringLiteral(": Reset to inherited removes the key"), sec, "SCROLL_H_DEADZONE", "<absent>",
                   [&] { ch->resetFor("SCROLL_H_DEADZONE")->click(); });
            expect(who + QStringLiteral(": Reset this group removes its keys"), sec, "ZOOM_SMOOTH_MS", "<absent>", [&] {
                for (QPushButton *c : ch->findChildren<QPushButton *>())
                    if (c->accessibleName() == QStringLiteral("Reset this group - Twist / zoom")) {
                        c->click();
                        break;
                    }
            }, false, true);
            expect(who + QStringLiteral(": Restore defaults removes every channel key"), sec, "SCROLL_V_SIGN", "<absent>", [&] { ch->restoreDefaults(); }, false, true);
        } else {
            expect(who + QStringLiteral(": Reset removes the key, the built-in default applies (Deadzone)"), sec,
                   "SCROLL_H_DEADZONE", "<absent>",
                   [&] { ch->resetFor("SCROLL_H_DEADZONE")->click(); });
        }
    }

    // joystick mode and the output switch of a profile
    go(kApp, "general");
    expect(QStringLiteral("program profile: Joystick mode"), kApp, "MODE", "timeline", [&] {
        auto *mode = findNamed<QComboBox>(page, QStringLiteral("Joystick mode"));
        int idx = -1;
        for (int i = 0; i < mode->count(); ++i)
            if (mode->itemText(i).contains(QStringLiteral("imeline"))) idx = i;
        QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, idx));
    });
    settle();
    expect(QStringLiteral("program profile: Joystick mode 'Same as Global' removes MODE"), kApp, "MODE", "<absent>", [&] {
        QMetaObject::invokeMethod(findNamed<QComboBox>(page, QStringLiteral("Joystick mode")), "activated", Q_ARG(int, 0));
    });
    settle();
    expect(QStringLiteral("program profile: Output switched off"), kApp, "ENABLED", "0",
           [&] { findNamed<QCheckBox>(page, QStringLiteral("Send events"))->setChecked(false); });
    settle();  // the page is rebuilt (the joystick mode and the tabs are hidden)
    expect(QStringLiteral("program profile: Output switched on again"), kApp, "ENABLED", "1",
           [&] { findNamed<QCheckBox>(page, QStringLiteral("Send events"))->setChecked(true); });
    settle();
    go(kGlobal, "general");
    expect(QStringLiteral("Global: Joystick mode"), kGlobal, "MODE", "off", [&] {
        auto *mode = findNamed<QComboBox>(page, QStringLiteral("Joystick mode"));
        int idx = -1;
        for (int i = 0; i < mode->count(); ++i)
            if (mode->itemText(i).startsWith(QStringLiteral("Off"))) idx = i;
        QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, idx));
    });
    settle();
    expect(QStringLiteral("Global: Joystick mode back to normal"), kGlobal, "MODE", "normal", [&] {
        QMetaObject::invokeMethod(findNamed<QComboBox>(page, QStringLiteral("Joystick mode")), "activated", Q_ARG(int, 0));
    });

    // buttons: an action, another one, and 'Inherit' / Unbind
    for (const auto &p : profiles) {
        const size_t sec = p.sec;
        const QString who = QString::fromLatin1(p.name);
        go(sec, "motions");
        page->selectButton(2);
        settle();
        auto act = [&](const QString &text) {
            auto *combo = findNamed<QComboBox>(page, QStringLiteral("Action of button 2"));
            int idx = -1;
            for (int i = 0; combo && i < combo->count(); ++i)
                if (combo->itemText(i) == text) idx = i;
            if (combo && idx >= 0) QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, idx));
        };
        if (sec == kGlobal) {
            // plain clicks sit behind "Advanced" in Global: open it first, as the user does
            for (QGroupBox *g : page->findChildren<QGroupBox *>())
                if (g->isCheckable()) g->setChecked(true);
            settle();
        }
        expect(who + QStringLiteral(": Button 2 = navigation mode"), sec, "BUTTON_2", "nav", [&] { act(QStringLiteral("Navigation (arrow keys while held)")); });
        settle();
        expect(who + QStringLiteral(": Button 2 = right click"), sec, "BUTTON_2", "rightclick", [&] { act(QStringLiteral("Right click")); });
        settle();
        if (sec == kGlobal) {
            expect(who + QStringLiteral(": Button 2 'Nothing' (Unbind) is written"), sec, "BUTTON_2", "none", [&] { act(QStringLiteral("Nothing")); });
        } else {
            expect(who + QStringLiteral(": Button 2 Default (inherit) removes the binding"), sec, "BUTTON_2", "<absent>",
                   [&] { act(QStringLiteral("Default (inherit from Global)")); });
        }
    }

    // overrides tab: Add override and edit and remove
    go(kApp, "overrides");
    expect(QStringLiteral("program profile: Add override"), kApp, "SCROLL_V_MIX", "z",
           [&] {
               if (QPushButton *b = buttonTexted(page, QStringLiteral("Add override"))) b->click();
           }, true);
    settle();

    // the Settings page
    w.showPage(QStringLiteral("settings"));
    settle();
    {
        QWidget *sp = w.currentPage();
        auto *out = findNamed<QCheckBox>(sp, QStringLiteral("Output"));
        expect(QStringLiteral("Settings: Output off"), kGlobal, "ENABLED", "0", [&] { out->setChecked(false); });
        expect(QStringLiteral("Settings: Output on"), kGlobal, "ENABLED", "1", [&] { out->setChecked(true); });
        auto *tick = findNamed<QSpinBox>(sp, QStringLiteral("Tick rate (Hz)"));
        expect(QStringLiteral("Settings: Tick rate"), kGlobal, "TICK_HZ", "120", [&] { tick->setValue(120); });
        expect(QStringLiteral("Settings: Tick rate back to the default 250"), kGlobal, "TICK_HZ", "250", [&] { tick->setValue(250); });
        if (auto *be = findNamed<QComboBox>(sp, QStringLiteral("Output backend"))) {
            expect(QStringLiteral("Settings: Output backend"), kGlobal, "BACKEND", "x11", [&] {
                be->setCurrentIndex(be->findData(QStringLiteral("x11")));
                QMetaObject::invokeMethod(be, "activated", Q_ARG(int, be->currentIndex()));
            });
            expect(QStringLiteral("Settings: Output backend back to auto"), kGlobal, "BACKEND", "auto", [&] {
                be->setCurrentIndex(be->findData(QStringLiteral("auto")));
                QMetaObject::invokeMethod(be, "activated", Q_ARG(int, be->currentIndex()));
            });
        }
        // Input path (Windows and macOS only; Linux has no such control) autosaves like the other daemon settings
        if (auto *in = findNamed<QComboBox>(sp, QStringLiteral("Input path"))) {
            expect(QStringLiteral("Settings: Input path"), kGlobal, "INPUT", "driver", [&] {
                in->setCurrentIndex(in->findData(QStringLiteral("driver")));
                QMetaObject::invokeMethod(in, "activated", Q_ARG(int, in->currentIndex()));
            });
            expect(QStringLiteral("Settings: Input path back to automatic"), kGlobal, "INPUT", "auto", [&] {
                in->setCurrentIndex(in->findData(QStringLiteral("auto")));
                QMetaObject::invokeMethod(in, "activated", Q_ARG(int, in->currentIndex()));
            });
        } else {
#ifndef DOFSCROLL_DRIVER_UI
            record(QStringLiteral("Settings: no Input path control on Linux"), true, QStringLiteral("spacenavd is the driver there"));
#else
            record(QStringLiteral("Settings: Input path control"), false, QStringLiteral("missing"));
#endif
        }
        // the About box names Qt and its licence; the 3Dconnexion note only where a driver interface is used
        if (auto *about = sp->findChild<QGroupBox *>(QStringLiteral("about-box"))) {
            QString text;
            for (auto *l : about->findChildren<QLabel *>()) text += l->text() + QLatin1Char('\n');
            const bool hasDriverNote = text.contains(QStringLiteral("Not affiliated with or endorsed by 3Dconnexion"));
#ifdef DOFSCROLL_DRIVER_UI
            const bool wantNote = true;
#else
            const bool wantNote = false;
#endif
            record(QStringLiteral("Settings: About box (Qt, LGPL, 3Dconnexion note)"),
                   text.contains(QStringLiteral("Qt ")) && text.contains(QStringLiteral("LGPL")) && hasDriverNote == wantNote,
                   QStringLiteral("Qt and licence notes; driver note only on Windows and macOS"));
        } else {
            record(QStringLiteral("Settings: About box"), false, QStringLiteral("missing"));
        }
        // language and appearance go to ui.conf at once
        auto *lang = findNamed<QComboBox>(sp, QStringLiteral("Language"));
        auto *theme = findNamed<QComboBox>(sp, QStringLiteral("Theme"));
        const spn::ui::UiSettings keep = spn::ui::loadUiSettings(spn::ui::uiSettingsPath());
        if (lang) {
            lang->setCurrentIndex(lang->findData(int(spn::ui::Language::German)));
            QMetaObject::invokeMethod(lang, "activated", Q_ARG(int, lang->currentIndex()));
            record(QStringLiteral("Settings: Language (ui.conf)"), spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).language == spn::ui::Language::German,
                   QStringLiteral("written at once"));
            lang->setCurrentIndex(lang->findData(int(spn::ui::Language::English)));
            QMetaObject::invokeMethod(lang, "activated", Q_ARG(int, lang->currentIndex()));
            record(QStringLiteral("Settings: Language back to English (ui.conf)"), spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).language == spn::ui::Language::English,
                   QStringLiteral("written at once"));
        }
        if (theme) {
            theme->setCurrentIndex(theme->findData(int(spn::ui::Theme::Dark)));
            QMetaObject::invokeMethod(theme, "activated", Q_ARG(int, theme->currentIndex()));
            record(QStringLiteral("Settings: Theme (ui.conf)"), spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).theme == spn::ui::Theme::Dark, QStringLiteral("written at once"));
            theme->setCurrentIndex(theme->findData(int(spn::ui::Theme::System)));
            QMetaObject::invokeMethod(theme, "activated", Q_ARG(int, theme->currentIndex()));
            applyTheme(spn::ui::Theme::System);
        }
        (void)keep;
    }

    // the tray's Enabled item writes ENABLED of Global (its own model, atomic, then reload)
    {
        spn::ui::RecordingActionRunner trayRunner;
        QtStrings strings;
        spn::ui::TrayController tray(spn::ui::TrayController::Paths{rig.ctx.configPath, rig.ctx.modesDir, rig.ctx.runtimeDir}, trayRunner, strings);
        tray.refresh(0);
        spn::ui::TrayEntry toggle;
        for (const spn::ui::TrayEntry &e : tray.menu().entries)
            if (e.action == spn::ui::TrayAction::ToggleEnabled) toggle = e;
        // the window has no pending edit: it follows the file after the tray wrote it
        const std::string was = valueOf(kGlobal, "ENABLED");
        tray.perform(toggle);
        const std::string now = valueOf(kGlobal, "ENABLED");
        record(QStringLiteral("Tray: Enabled toggle"), now != was && now != "<absent>", QStringLiteral("ENABLED = %1").arg(qs8(now)));
        // the window follows the file (no stale overwrite later) and its Settings page shows the same
        auto *out = findNamed<QCheckBox>(w.currentPage(), QStringLiteral("Output"));
        const bool wantOn = now == "1";
        QElapsedTimer t;
        t.start();
        while (out && out->isChecked() != wantOn && t.elapsed() < 4000) QApplication::processEvents(QEventLoop::AllEvents, 50);
        record(QStringLiteral("Tray: the window follows the change of the file"), out && out->isChecked() == wantOn, QStringLiteral("Output switch on the Settings page"));
    }
    std::printf("audit: %d controls checked\n", rows);
    std::printf("autosave audit self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// A runner that remembers "Start at login" like the system would (the guide reads the state back).
namespace {
struct GuideRunner : spn::ui::RecordingActionRunner {
    std::atomic<int> autostart{static_cast<int>(spn::autostart::AutostartState::Disabled)};
    bool setAutostart(bool enable, std::string *err) override {
        autostart = static_cast<int>(enable ? spn::autostart::AutostartState::Enabled : spn::autostart::AutostartState::Disabled);
        return RecordingActionRunner::setAutostart(enable, err);
    }
};
}  // namespace

static QString guideStatusText(const QString &extra) {
    return QStringLiteral("profile=global\nenabled=1\nupdated=%1\n").arg(static_cast<qlonglong>(std::time(nullptr))) + extra;
}

// First-run guide and the automatic start of the daemon: the steps with their live checks, skip, finish and
// "Show setup guide", "Start at login" on by default, no horizontal scroll bar, accessible names, and the
// grace period before the interface starts the daemon (src/ui/model/setup_guide_model.hpp, daemon_autostart.hpp).
static bool setupGuideSelfTest(const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "setup guide self-test failed: %s\n", what);
        ok = ok && cond;
    };
    const QString dir = tmpPath + QStringLiteral("/guide");
    QDir().mkpath(dir + QStringLiteral("/run"));
    QDir().mkpath(dir + QStringLiteral("/run2"));
    QDir().mkpath(dir + QStringLiteral("/cfg"));

    // ---- the dialog on its own, with a fake status
    GuideRunner rec;
    spn::ui::StatusModel st;
    QtStrings strings;
    UiContext c;
    c.runner = &rec;
    c.status = &st;
    c.strings = &strings;
    c.runtimeDir = (dir + QStringLiteral("/run")).toStdString();
    auto reader = [&rec](std::string *) { return static_cast<spn::autostart::AutostartState>(rec.autostart.load()); };
    SetupGuideDialog d(c, nullptr, true, nullptr, reader);
    int skipped = 0, finished = 0;
    QObject::connect(&d, &SetupGuideDialog::skipRequested, [&] { ++skipped; });
    QObject::connect(&d, &SetupGuideDialog::finishRequested, [&] { ++finished; });
    d.show();
    d.waitIdle();
    settle();
    check(rec.count(spn::ui::RecordingActionRunner::Kind::SetAutostart) == 1 && rec.calls.front().enable,
          "the first run switches Start at login on through the runner");
    check(d.autostartBox() && d.autostartBox()->isChecked() && d.autostartBox()->isEnabled(), "the check box is on by default");
    const spn::ui::SetupStep *autoStep = d.step(QStringLiteral("autostart"));
    check(autoStep && autoStep->state == spn::ui::StepState::Done, "Start at login is done");
    const spn::ui::SetupStep *daemonStep = d.step(QStringLiteral("daemon"));
    check(daemonStep && daemonStep->state == spn::ui::StepState::Problem, "no heartbeat: the daemon step needs attention");
    check(d.actionButtonOf(QStringLiteral("daemon")) != nullptr, "...with a Start now button");
    d.actionButtonOf(QStringLiteral("daemon"))->click();
    check(rec.count(spn::ui::RecordingActionRunner::Kind::StartDaemon) == 1, "Start now asks the runner to start the daemon");
    check(d.step(QStringLiteral("tryit")) && d.step(QStringLiteral("tryit"))->state == spn::ui::StepState::Info, "Try it is information");
    // unticking switches it off at once
    d.autostartBox()->click();
    d.waitIdle();
    check(rec.calls.back().kind == spn::ui::RecordingActionRunner::Kind::SetAutostart && !rec.calls.back().enable &&
              !d.autostartBox()->isChecked() && d.step(QStringLiteral("autostart"))->state == spn::ui::StepState::Todo,
          "unticking Start at login switches it off");
    // a live heartbeat without a device: the daemon is done, the device is not (spacenavd off: the command)
    rec.spacenavd = spn::ui::ServiceState::Inactive;
    st.update(guideStatusText(QString()).toStdString(), true, std::time(nullptr));
    d.refresh();
    check(d.step(QStringLiteral("daemon"))->state == spn::ui::StepState::Done, "a heartbeat: dofscroll is running");
    check(d.step(QStringLiteral("device"))->state != spn::ui::StepState::Done, "no device yet");
    if (spn::ui::hostOs() == spn::ui::Os::Linux)
        check(d.commandOf(QStringLiteral("device")) == QStringLiteral("sudo systemctl enable --now spacenavd.service"),
              "spacenavd not active: the fix is shown as a command");
    st.update(guideStatusText(QStringLiteral("devices_seen=1\ndevice=SpaceMouse Pro\n")).toStdString(), true, std::time(nullptr));
    d.refresh();
    check(d.step(QStringLiteral("device"))->state == spn::ui::StepState::Done && d.commandOf(QStringLiteral("device")).isEmpty(),
          "the device is found: done, no command");
    check(d.step(QStringLiteral("device"))->text.find("SpaceMouse Pro") != std::string::npos, "the device name is shown");
    // every row says its state in words, every control has a name, nothing scrolls sideways
    for (const spn::ui::SetupStep &s : d.steps()) check(s.state == spn::ui::StepState::Info || !s.status.empty(), "every step has a state word");
    for (QPushButton *b : d.findChildren<QPushButton *>()) check(!b->isVisibleTo(&d) || !b->text().isEmpty() || !b->accessibleName().isEmpty(), "buttons have a name");
    for (QLineEdit *e : d.findChildren<QLineEdit *>()) check(!e->accessibleName().isEmpty(), "the command field has an accessible name");
    check(!d.autostartBox()->accessibleName().isEmpty(), "the check box has an accessible name");
    for (const QSize &size : {QSize(660, 700), QSize(520, 420), QSize(720, 480)}) {
        d.resize(size);
        settle();
        check(!d.hasHorizontalScrollBar(), "no horizontal scroll bar");
    }
    // Skip and Finish: once each, the dialog hides
    d.skipButton()->click();
    check(skipped == 1 && finished == 0 && !d.isVisible(), "Skip closes the guide");
    d.show();
    d.finishButton()->click();
    check(skipped == 1 && finished == 1 && !d.isVisible(), "Finish closes the guide");
    d.show();
    d.close();  // the close button (Escape does the same through reject)
    check(skipped == 2 && !d.isVisible(), "the close button counts as Skip, once");

    // ---- in the window: shown at the first start, remembered, shown again from the Overview
    {
        GuideRunner r2;
        UiContext ctx;
        ctx.runner = &r2;
        ctx.configPath = (dir + QStringLiteral("/cfg/dofscroll.conf")).toStdString();
        ctx.modesDir = (dir + QStringLiteral("/cfg/modes")).toStdString();
        ctx.buttonModesDir = (dir + QStringLiteral("/cfg/buttons")).toStdString();
        ctx.runtimeDir = (dir + QStringLiteral("/run2")).toStdString();
        const std::string progress = spn::ui::uiProgressPath(ctx.configPath);
        {
            MainWindow w(ctx);
            check(!w.setupGuideShown(), "no guide before the start call");
            check(w.startSetupGuide(true) && w.setupGuideShown(), "a new installation shows the guide with the window");
            check(w.setupGuide() && w.setupGuide()->isEnabled(), "the guide is usable");
            w.setupGuide()->waitIdle();
            w.setupGuide()->skipButton()->click();
            check(!w.setupGuideShown() && spn::ui::loadUiProgress(progress).setupGuideSeen, "Skip remembers it in ui-progress.conf");
            // ... and the Overview opens it again
            w.show();
            w.showPage(QStringLiteral("overview"));
            settle();
            QPushButton *again = w.findChild<QPushButton *>(QStringLiteral("show-setup-guide"));
            check(again != nullptr, "the Overview has Show setup guide");
            if (again) again->click();
            settle();
            check(w.setupGuideShown(), "Show setup guide opens the guide again");
            w.setupGuide()->waitIdle();
            w.setupGuide()->finishButton()->click();
            check(!w.setupGuideShown(), "Finish closes it");
        }
        {
            MainWindow w2(ctx);
            check(!w2.startSetupGuide(true) && !w2.setupGuideShown(), "a guide that was seen does not come back by itself");
        }
        {
            // an existing installation (not a fresh one) never gets it, and is marked as seen
            std::remove(progress.c_str());
            MainWindow w3(ctx);
            check(!w3.startSetupGuide(false) && !w3.setupGuideShown() && spn::ui::loadUiProgress(progress).setupGuideSeen,
                  "an existing installation does not get the guide");
        }
    }

    // ---- the daemon is started by the interface after the grace period, once
    {
        GuideRunner r3;
        const std::string run = (dir + QStringLiteral("/run")).toStdString();
        DaemonStarter s1(r3, run);
        check(s1.tick(0, 0) == spn::ui::DaemonAutoStart::Step::Wait && s1.waiting() && !s1.failed(), "inside the grace period: wait");
        check(s1.tick(3000, 0) == spn::ui::DaemonAutoStart::Step::Wait, "still waiting");
        check(r3.count(spn::ui::RecordingActionRunner::Kind::StartDaemon) == 0, "not started inside the grace period");
        check(s1.tick(spn::ui::DaemonAutoStart::kGraceMs, 0) == spn::ui::DaemonAutoStart::Step::Start &&
                  r3.count(spn::ui::RecordingActionRunner::Kind::StartDaemon) == 1 && s1.attempted() && s1.waiting(),
              "after the grace period: started once");
        s1.tick(9000, 0);
        check(r3.count(spn::ui::RecordingActionRunner::Kind::StartDaemon) == 1, "never started twice");
        check(!s1.failed() && s1.tick(20000, 0) == spn::ui::DaemonAutoStart::Step::Done && s1.failed(),
              "no heartbeat long after the start: failed");
        GuideRunner r4;
        DaemonStarter s2(r4, run);
        s2.tick(2000, 1);  // the heartbeat shows up (the login autostart started the daemon too)
        s2.tick(60000, 0);
        check(r4.count(spn::ui::RecordingActionRunner::Kind::StartDaemon) == 0 && !s2.failed(), "a daemon that runs is never started");
        // a heartbeat read from the status file
        QFile sf(dir + QStringLiteral("/run/dofscroll-status"));
        check(sf.open(QIODevice::WriteOnly) && sf.write(guideStatusText(QString()).toUtf8()) > 0, "write a status file");
        sf.close();
        GuideRunner r5;
        DaemonStarter s3(r5, run);
        s3.tick(0);
        s3.tick(spn::ui::DaemonAutoStart::kGraceMs + 1000);
        check(r5.count(spn::ui::RecordingActionRunner::Kind::StartDaemon) == 0 && s3.daemonSeen(), "the status file is the heartbeat");
        GuideRunner r6;
        r6.result = false;
        DaemonStarter s4(r6, (dir + QStringLiteral("/run2")).toStdString(), nullptr, 100);
        s4.tick(0);
        s4.tick(200);
        check(s4.startFailed() && s4.failed() && !s4.waiting(), "a failed start is reported at once");
    }
    std::printf("setup guide self-test: %s\n", ok ? "ok" : "failed");
    return ok;
}

// Index of the firefox program profile in the Overview list of the demo configuration used by --screenshots
// (global, two devices, then the programs).
static constexpr int kFirefoxProfile = 3;
static constexpr int kOkularProfile = 6;  // the program profile shown on the website

int main(int argc, char **argv) {
    bool selfTest = false;
    const char *screenshotDir = nullptr;
    bool restarted = false;  // started by "Restart": wait for the old instance's lock
    bool sessionRestore = false;  // started by the desktop's session restore (-session <id>), not by the user
    // Before any output and before QApplication: X11 thread support, and on Windows the parent
    // console for the command line flags (the executable has no console of its own).
    dofscroll_ui::earlyPlatformInit();
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h") ||
            !std::strcmp(argv[i], "--self-test") || !std::strcmp(argv[i], "--screenshots"))
            dofscroll_ui::attachParentConsole();
    // Handled before QApplication exists: no window, no lock, no display needed.
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version")) {
            // First line: the program and its version (scripts and docs parse it). Second line: the Qt
            // that is running (the Linux packages bundle their own Qt, so this tells which one is used).
            std::printf("dofscroll-ui %s\nQt %s (built with Qt %s)\n", DOFSCROLL_VERSION, qVersion(), QT_VERSION_STR);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            printUsage();
            return 0;
        }
        if (!std::strcmp(argv[i], "--self-test")) selfTest = true;
        if (!std::strcmp(argv[i], "--screenshots") && i + 1 < argc) screenshotDir = argv[i + 1];
        if (!std::strcmp(argv[i], spn::ui::restartFlag())) restarted = true;
        if (!std::strcmp(argv[i], "-session")) sessionRestore = true;
    }

#if defined(DOFSCROLL_BUNDLED_QT)
    // The bundled Qt (Linux packages) has no KDE or GNOME platform theme plugin: take the colour scheme
    // (light or dark) from the desktop portal and use Qt's own Fusion style. Set only while the
    // application object is created, so programs started from here do not inherit it.
    const bool setTheme = qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME");
    if (setTheme) qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif
    QApplication app(argc, argv);
#if defined(DOFSCROLL_BUNDLED_QT)
    if (setTheme) qunsetenv("QT_QPA_PLATFORMTHEME");
#endif
    QApplication::setApplicationName(QStringLiteral("dofscroll"));
    QApplication::setApplicationVersion(QStringLiteral(DOFSCROLL_VERSION));
    QApplication::setQuitOnLastWindowClosed(false);
    installLook();  // spacing scale, font, style sheet and the wheel guard, for every window and dialog

    // Catalog converted from po/de.po at build time (dofscroll_<lang>.qm next to the binary).
    // The language comes from the per-user UI settings (General page): "en" = no catalog.
    const std::string lang = spn::ui::languageLocaleName(spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).language);
    QTranslator translator;
    // The self-test checks English texts: it never loads the catalog of the user's language setting.
    if (lang != "en" && !selfTest &&
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
        // Demo data without any real names or paths (also used for the website screenshots).
        conf.write("[global]\n\n[device:SpaceMouse Pro]\nSCROLL_V_SENS = 9\n\n[device:SpaceMouse Compact]\nSCROLL_V_SENS = 7\n\n"
                   "[app:firefox]\nMODE = timeline\nBUTTON_1 = key:space\nBUTTON_2 = buttonmode:latch-nav\nBUTTON_3 = buttonmode:gone\n\n"
                   "[app:blender]\nENABLED = 0\n\n[app:gimp]\n\n[app:okular]\nMODE = slides\nSCROLL_V_SENS = 6\n");
        conf.close();
        // The daemon's status file with a connected device, as the running daemon writes it, so that
        // the Overview and the status bar show real-looking data. Rewritten before every grab because
        // the window counts a status older than a few seconds as "daemon not running".
        const std::string statusPath = spn::statusFilePathIn(ctx.runtimeDir);
        QDir().mkpath(QString::fromStdString(ctx.runtimeDir));
        MainWindow w(ctx);
        auto refreshDemoStatus = [&](const char *program) {
            QFile sf(QString::fromStdString(statusPath));
            if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                sf.write(QString::fromLatin1("profile=%1\ndevice=SpaceMouse Pro\napp=%1\nmode=normal\ntoggle=\nenabled=1\npaused=0\n"
                                             "devices_seen=1\nsession=active\nupdated=%2\n")
                             .arg(QString::fromLatin1(program), QString::number(static_cast<qlonglong>(std::time(nullptr))))
                             .toUtf8());
                sf.close();
            }
            w.refreshStatusNow();
        };
        w.show();
        // Light at three sizes, dark at the medium size (the layout does not depend on the colours).
        static const struct { const char *theme; const char *name; int w, h; } shots[] = {
            {"light", "small", 720, 480}, {"light", "medium", 960, 560}, {"light", "wide", 1500, 820}, {"dark", "medium", 960, 560}};
        int written = 0;
        for (const auto &sz : shots) {
            applyTheme(spn::ui::themeFromId(sz.theme));
            w.resize(sz.w, sz.h);
            for (const std::string &id : w.pageIds()) {
                w.showPage(QString::fromStdString(id));
                refreshDemoStatus("firefox");
                if (id == "profiles") w.openOverviewProfile(kFirefoxProfile);  // the firefox profile: mode and button modes
                settle();
                const QString file = QString::fromUtf8(screenshotDir) + QStringLiteral("/%1-%2-%3.png")
                                         .arg(QString::fromLatin1(sz.theme), QString::fromLatin1(sz.name),
                                              QString::fromStdString(id));
                if (!w.grab().save(file)) return 1;
                ++written;
            }
        }
        {   // the tabs of the profile editor, for Global and for a program profile
            applyTheme(spn::ui::themeFromId("light"));
            w.resize(1500, 820);
            w.showPage(QStringLiteral("profiles"));
            ProfilesPage *pp = w.profilesPage();
            if (pp)
                for (const char *who : {"global", "program"})
                    for (const QString &tab : pp->tabIds()) {
                        pp->selectSection(std::string(who) == "global" ? 0 : size_t(kFirefoxProfile));
                        pp->selectTab(tab);
                        settle();
                        const QString file = QString::fromUtf8(screenshotDir) +
                                             QStringLiteral("/light-wide-profile-%1-%2.png").arg(QString::fromLatin1(who), tab);
                        if (w.grab().save(file)) ++written;
                    }
            w.resize(960, 560);  // the same tabs in a medium window
            if (pp)
                for (const QString &tab : pp->tabIds()) {
                    pp->selectSection(size_t(kFirefoxProfile));
                    pp->selectTab(tab);
                    settle();
                    if (w.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/light-medium-profile-%1-program.png").arg(tab))) ++written;
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
        {   // the Editor page: a joystick mode of the user, a motion selected, a button selected, a built-in one
            for (const char *th : {"light", "dark"}) {
                applyTheme(spn::ui::themeFromId(th));
                EditorPage ep(ctx);
                ep.resize(1500, 860);
                ep.show();
                settle();
                if (ep.selectMode(QStringLiteral("slides")) && ep.editorWidget() && ep.editorWidget()->motionEditor()) {
                    MotionEditor *me = ep.editorWidget()->motionEditor();
                    me->select(QStringLiteral("m:v_push"));
                    settle();
                    if (ep.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/%1-wide-editor-motion.png").arg(QString::fromLatin1(th)))) ++written;
                    if (std::string(th) == "light") {
                        ep.editorWidget()->motionEditor()->select(QStringLiteral("b:1"));
                        settle();
                        if (ep.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/light-wide-editor-button.png"))) ++written;
                        ep.editorWidget()->motionEditor()->select(QStringLiteral("m:lift"));
                        settle();
                        if (ep.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/light-wide-editor-lift.png"))) ++written;
                    }
                }
                if (std::string(th) == "light" && ep.selectMode(QStringLiteral("timeline"))) {
                    settle();
                    if (ep.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/light-wide-editor-builtin.png"))) ++written;
                }
            }
            applyTheme(spn::ui::themeFromId("light"));
            EditorPage small(ctx);
            small.resize(900, 640);
            small.show();
            settle();
            small.selectMode(QStringLiteral("slides"));
            settle();
            if (small.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/light-medium-editor-motion.png"))) ++written;
        }
        for (const char *th : {"light", "dark"}) {  // the first-run guide (a recording runner: nothing is changed on this machine)
            applyTheme(spn::ui::themeFromId(th));
            spn::ui::RecordingActionRunner guideRunner;
            spn::ui::StatusModel guideStatus;
            QtStrings guideStrings;
            UiContext gctx = ctx;
            gctx.runner = &guideRunner;
            gctx.status = &guideStatus;
            gctx.strings = &guideStrings;
            guideStatus.update(guideStatusText(QStringLiteral("devices_seen=1\ndevice=SpaceMouse Pro\n")).toStdString(), true,
                               std::time(nullptr));
            SetupGuideDialog guide(gctx, nullptr, false, nullptr, [](std::string *) { return spn::autostart::AutostartState::Enabled; });
            guide.show();
            guide.waitIdle();
            settle();
            if (guide.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/%1-setup-guide.png").arg(QString::fromLatin1(th)))) ++written;
        }
        for (const char *th : {"light", "dark"}) {  // lists with a selected row, in both themes
            applyTheme(spn::ui::themeFromId(th));
            ModesPage page(ctx);
            page.resize(1000, 640);
            page.show();
            page.selectMode(QStringLiteral("slides"));
            settle();
            if (page.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/%1-wide-modes-selected.png").arg(QString::fromLatin1(th)))) ++written;
            ButtonModesPage bpage(ctx);
            bpage.resize(1000, 640);
            bpage.show();
            bpage.selectButtonMode(QStringLiteral("latch-nav"));
            settle();
            if (bpage.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/%1-wide-button-modes-selected.png").arg(QString::fromLatin1(th)))) ++written;
            // "Cycle through": the built-in cycle-favorites (read-only, with the note), then its editable copy
            bpage.resize(1000, 900);
            bpage.selectButtonMode(QStringLiteral("cycle-favorites"));
            settle();
            if (bpage.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/%1-wide-button-modes-cycle-builtin.png").arg(QString::fromLatin1(th)))) ++written;
            if (bpage.catalog().find("cycle-favorites") && bpage.catalog().find("cycle-favorites")->builtin) {
                bpage.createEditableCopy(QStringLiteral("cycle-favorites"));
                bpage.cycleEditor()->setChecked(QStringLiteral("video"), true);
                bpage.flush();
            }
            {
                settle();
                if (bpage.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/%1-wide-button-modes-cycle.png").arg(QString::fromLatin1(th)))) ++written;
            }
        }
        applyTheme(spn::ui::themeFromId("light"));
        {   // the dialogs (minimum-ish size and the default one)
            ProfilesEnv env;
            AddDeviceDialog device(env, {});
            device.show();
            settle();
            if (device.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/light-dialog-add-device.png"))) ++written;
            AddAppDialog appDialog(env, {});
            appDialog.show();
            settle();
            if (appDialog.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/light-dialog-add-app.png"))) ++written;
        }
        for (const char *th : {"light", "dark"}) {  // the website: the settings window and the Overview, with a running daemon
            applyTheme(spn::ui::themeFromId(th));
            w.resize(1040, 680);
            w.showPage(QStringLiteral("profiles"));
            refreshDemoStatus("okular");
            w.openOverviewProfile(kOkularProfile);
            if (ProfilesPage *pp = w.profilesPage()) pp->selectTab(QStringLiteral("general"));
            settle();
            if (w.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/site-profiles-%1.png").arg(QString::fromLatin1(th)))) ++written;
            w.resize(1040, 680);  // the Joystick modes page: six motions per mode at a glance
            w.showPage(QStringLiteral("modes"));
            if (auto *mp = qobject_cast<ModesPage *>(w.currentPage())) mp->selectMode(QStringLiteral("browse"));
            settle();
            if (w.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/site-modes-%1.png").arg(QString::fromLatin1(th)))) ++written;
            w.resize(720, 470);
            w.showPage(QStringLiteral("overview"));
            refreshDemoStatus("okular");
            settle();
            if (w.grab().save(QString::fromUtf8(screenshotDir) + QStringLiteral("/site-overview-%1.png").arg(QString::fromLatin1(th)))) ++written;
        }
        std::printf("screenshots: %d files in %s\n", written, screenshotDir);
        return written > 0 ? 0 : 1;
    }

    if (selfTest) {
        // Private temporary directories: the test never touches the user's configuration.
        QTemporaryDir tmp;
        if (!tmp.isValid()) return 1;
        // A runner that records instead of opening anything or signalling a daemon.
        spn::ui::RecordingActionRunner recorder;
        UiContext ctx;
        ctx.runner = &recorder;
        ctx.reload = spn::ui::reloadVia(recorder);
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
        if (!motionEditorSelfTest(ctx, tmp.path())) return 1;
        if (!editorPageSelfTest(ctx, tmp.path())) return 1;
        if (!modeFilesAutosaveSelfTest(ctx, tmp.path())) return 1;
        if (!examplesSelfTest(ctx, tmp.path())) return 1;
        if (!extensionsSelfTest(ctx, tmp.path())) return 1;
        if (!modeLibrarySelfTest(ctx, tmp.path())) return 1;
        if (!modeEditorTabsSelfTest(ctx)) return 1;
        if (!keyTreePickerSelfTest(&w)) return 1;
        ModesPage modes(ctx);  // standalone (own catalog): built-in normal, off, timeline + the file mode
        modes.show();
        Tray tray(&w, icon, ctx);
        // Must not crash; the result depends on the platform (offscreen: usually false).
        const bool avail = QSystemTrayIcon::isSystemTrayAvailable();
        std::printf("self-test: entries=%d profiles=%d modes=%d tray-modes=%d tray-entries=%d tray-available=%d\n",
                    w.pageCount(), w.profileCount(), modes.modeCount(), tray.modeMenuCount(),
                    tray.menuEntryCount(), avail ? 1 : 0);
#if defined(Q_OS_LINUX)
        // No platform theme (offscreen, or the bundled Qt without KDE and GNOME plugins) means Qt's own
        // Fusion style, the base of our style sheet; a style such as "windows" would look different.
        // (our SpacingStyle is a proxy: ask its base style)
        const QStyle *baseStyle = QApplication::style();
        if (const auto *proxy = qobject_cast<const QProxyStyle *>(baseStyle)) baseStyle = proxy->baseStyle();
        const QString styleName = baseStyle->name();
        std::printf("self-test: qt=%s style=%s\n", qVersion(), qPrintable(styleName));
        if (styleName.compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0) {
            std::fprintf(stderr, "self-test failed: the base style is %s, expected fusion\n", qPrintable(styleName));
            return 1;
        }
#endif
        // every built-in (however many the embedded library holds) plus the one mode file of this test
        const int expectedModes = static_cast<int>(spn::loadModes("", nullptr).modes.size()) + 1;
        bool tipsOk = static_cast<int>(tray.modeMenuToolTips().size()) == expectedModes;
        for (const QString &tip : tray.modeMenuToolTips()) tipsOk = tipsOk && !tip.isEmpty();
        if (!tipsOk) std::fprintf(stderr, "self-test failed: every entry of the tray's joystick mode menu carries the description as tooltip\n");
        const bool ok = tipsOk && w.pageCount() == 9 && w.categoryCount() == 5 && w.profileCount() >= 0 &&
                        modes.modeCount() == expectedModes && tray.modeMenuCount() == expectedModes && tray.menuEntryCount() >= 8;
        if (!ok) return 1;
        if (!overviewSelfTest(w)) return 1;
        if (!quickActionsSelfTest(ctx, tmp.path())) return 1;
        if (!accessibilitySelfTest(w)) return 1;
        if (!sidebarFocusSelfTest(w)) return 1;
        if (!targetSizeSelfTest(w)) return 1;
        if (!keyboardSelfTest(w)) return 1;
        if (!profileEditorSelfTest([](MainWindow &win) { return accessibilitySelfTest(win); })) return 1;
        if (!borrowBuildSelfTest()) return 1;
        if (!autosaveAuditSelfTest([](MainWindow &win) { return accessibilitySelfTest(win); })) return 1;
        if (!legacyButtonsSelfTest()) return 1;
        if (!profileFilesSelfTest()) return 1;
        if (!themeSelfTest(w)) return 1;
        if (!selectionContrastSelfTest(ctx)) return 1;
        if (!fontScaleSelfTest(w)) return 1;
        if (!wheelSelfTest()) return 1;
        if (!defaultSizeFitsSelfTest(w)) return 1;
        if (!windowStateSelfTest(ctx, tmp.path())) return 1;
        if (!setupGuideSelfTest(tmp.path())) return 1;
        if (!windowAutosaveSelfTest(ctx, tmp.path())) return 1;
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
    // after "Restart" the old instance is still shutting down, so wait longer.
    if (!lock.tryLock(spn::ui::lockWaitMs(restarted))) {
        if (spn::ui::classifyLockFailure(lock.error() == QLockFile::LockFailedError) ==
            spn::ui::LockOutcome::AlreadyRunning) {
            // A session restore at login races with the systemd unit: it must not pop the window up.
            if (!sessionRestore) {
                QFile m(raiseMarker);
                if (m.open(QIODevice::WriteOnly)) m.close();
            }
            dofscroll_ui::attachParentConsole();
            std::fprintf(stderr, "dofscroll-ui is already running\n");
            return spn::ui::kExitAlreadyRunning;
        }
        std::fprintf(stderr, "dofscroll-ui: cannot create the single-instance lock %s (%s)\n",
                     qPrintable(lock.fileName()),
                     lock.error() == QLockFile::PermissionError ? "permission denied" : "unknown error");
        return spn::ui::kExitLockFailure;
    }
    QFile::remove(raiseMarker);  // stale request of an earlier start

    // First start (src/ui/model/first_start.hpp). A new installation has no configuration yet and nothing
    // to migrate: it gets the setup guide (src/ui/model/setup_guide_model.hpp) with the window.
    const bool configExisted = QFileInfo::exists(QString::fromStdString(ctx.configPath));
    const spn::ui::FirstStart firstStart = spn::ui::runFirstStart();
    std::fputs(firstStart.text("dofscroll-ui").c_str(), stderr);
    const bool freshInstall = !configExisted && !firstStart.migrated;

    // The theme of the user (before any window exists, so the whole interface starts in it).
    applyTheme(spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).theme);

    // The interface is started by its systemd unit, never by the desktop's session restore (a second
    // start at login would show the window instead of staying in the tray).
    QObject::connect(&app, &QGuiApplication::saveStateRequest,
                     [](QSessionManager &mgr) { mgr.setRestartHint(QSessionManager::RestartNever); });
    QObject::connect(&app, &QGuiApplication::commitDataRequest,
                     [](QSessionManager &mgr) { mgr.setRestartHint(QSessionManager::RestartNever); });

    MainWindow window(ctx);
    window.setWindowIcon(QIcon(QStringLiteral(":/dofscroll-app.svg")));  // taskbar and window: the colourful app icon
    const bool hasTray = QSystemTrayIcon::isSystemTrayAvailable();
    window.setTrayAvailable(hasTray);  // without a tray, closing the window quits
    QObject::connect(&window, &MainWindow::quitRequested, &app, &QApplication::quit);
    window.enableStatePersistence(spn::ui::uiSettingsPath());  // size, position and page of the last run
    // The one informational line of the migration to one file per profile (also in the log above): once.
    if (firstStart.split.status == spn::MigrateStatus::Migrated) {
        QString line = T("Your profiles are now one file each. The old configuration file is kept as "
                         "backup/dofscroll.conf.pre-split.");
        for (const std::string &w : firstStart.split.warnings)  // e.g. profiles whose old values differ from an existing file
            line += QLatin1Char(' ') + qs8(spn::ui::displayText(w, 300));
        window.showStartupInfo(line);
    }
    Tray tray(&window, icon, ctx);
    tray.show();
    // The daemon is started from here when none runs (after the grace period of a login autostart,
    // src/ui/model/daemon_autostart.hpp): the first start after installing is "open dofscroll from the menu".
    DaemonStarter starter(runner, ctx.runtimeDir);
    window.setDaemonStarter(&starter);
    starter.start();
    // Starts minimized to the tray: no window is shown. Without a system tray the icon would be
    // unreachable, so the window is then shown minimized (the taskbar or the second start reach it).
    // The first start of a new installation shows the window with the setup guide instead.
    if (window.startSetupGuide(freshInstall)) window.bringToFront();
    else if (!hasTray) window.showMinimized();
    QTimer raiseTimer;
    QObject::connect(&raiseTimer, &QTimer::timeout, [&] {
        if (QFile::exists(raiseMarker) && QFile::remove(raiseMarker)) window.bringToFront();
    });
    raiseTimer.start(500);
    const int code = app.exec();
    window.saveWindowState();  // "Quit" from the tray with the window open
    return code;
}
