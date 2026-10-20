// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll-ui: Qt6 Widgets tray icon and settings window (see docs/ui-options.md).
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QLocale>
#include <QLockFile>
#include <QStandardPaths>
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
#include <QTreeWidget>
#include <QTimer>
#include <QTranslator>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <cstring>
#include <ctime>

#include "main_window.hpp"
#include "pages/add_app_dialog.hpp"
#include "pages/add_device_dialog.hpp"
#include "pages/axes_page.hpp"
#include "button_row.hpp"
#include "common.hpp"
#include "pages/profiles_env.hpp"
#include "pages/button_modes_page.hpp"
#include "pages/channels_page.hpp"
#include "pages/extensions_page.hpp"
#include "pages/log_dialog.hpp"
#include "pages/mode_editor_widget.hpp"
#include "pages/modes_page.hpp"
#include "platform_hooks.hpp"
#include "unsaved_prompt.hpp"
#include "pages/profiles_page.hpp"
#include "pages/overview_page.hpp"
#include "qt_action_runner.hpp"
#include "theme.hpp"
#include "tray.hpp"
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
    check(!page.stageEdit(QStringLiteral("x"), QString(), QStringLiteral("click")) && !page.dirty(),
          "a built-in button mode cannot be edited in place");
    check(page.selectButtonMode(QStringLiteral("latch")), "select the file");
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
    check(page.buttonModeCount() == kBuiltin + 2, "a new file shows up in the list");
    QFile::remove(tmpPath + QStringLiteral("/buttons/second.conf"));
    page.refreshIfChanged();
    check(page.buttonModeCount() == kBuiltin + 1, "a removed file disappears from the list");
    return ok;
}

// Unsaved edits are never dropped silently: selecting another entry, reloading the same one, creating or
// importing something and quitting all ask Save / Discard / Cancel first (the question is answered by a hook here).
static bool unsavedGuardSelfTest(const UiContext &ctx, const QString &tmpPath, MainWindow &window) {
    using spn::ui::UnsavedChoice;
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "unsaved-changes self-test failed: %s\n", what);
        ok = ok && cond;
    };
    int asked = 0;
    UnsavedChoice answer = UnsavedChoice::Cancel;
    unsaved_prompt::setUnsavedAnswer([&](const std::string &) {
        ++asked;
        return answer;
    });
    auto fileText = [&](const QString &rel) {
        QFile f(tmpPath + rel);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    };

    {   // Modes page, in-page editor
        ModesPage page(ctx);
        page.show();
        settle();
        auto edit = [&](const char *name) {
            page.selectMode(QStringLiteral("mine"));
            page.openEditor();
            page.editorWidget()->editor().form().name = name;
        };
        asked = 0;
        page.selectMode(QStringLiteral("mine"));
        page.selectMode(QStringLiteral("timeline"));
        check(asked == 0, "no question without edits");
        edit("Unsaved");
        check(page.hasUnsavedChanges(), "the edit is pending");
        answer = UnsavedChoice::Cancel;
        page.selectMode(QStringLiteral("timeline"));
        check(asked == 1 && page.currentId() == QStringLiteral("mine") && page.hasUnsavedChanges() &&
                  page.editorWidget()->editor().form().name == "Unsaved",
              "Cancel keeps the row and the edits");
        check(!page.openEditor() && page.hasUnsavedChanges(), "Cancel on Edit of the same mode keeps the edits");
        answer = UnsavedChoice::Discard;
        page.selectMode(QStringLiteral("timeline"));
        check(page.currentId() == QStringLiteral("timeline") && !page.hasUnsavedChanges() &&
                  !fileText(QStringLiteral("/modes/mine.conf")).contains("NAME = Unsaved"),
              "Discard moves on and writes nothing");
        edit("Saved by guard");
        answer = UnsavedChoice::Save;
        page.selectMode(QStringLiteral("timeline"));
        check(page.currentId() == QStringLiteral("timeline") && !page.hasUnsavedChanges() &&
                  fileText(QStringLiteral("/modes/mine.conf")).contains("NAME = Saved by guard"),
              "Save writes the file and moves on");
        edit("Again");
        answer = UnsavedChoice::Discard;
        check(page.openEditor() && !page.hasUnsavedChanges() && page.editorWidget()->editor().form().name == "Saved by guard",
              "Discard on Edit of the same mode reloads the file");
        // Save that fails (invalid value): stay where we are
        page.editorWidget()->editor().form().overrides.emplace_back("SCROLL_V_SENS", "abc");
        answer = UnsavedChoice::Save;
        page.selectMode(QStringLiteral("timeline"));
        check(page.currentId() == QStringLiteral("mine") && page.hasUnsavedChanges(), "a failed Save keeps the edits");
        // Quit: the window asks for every page with edits
        answer = UnsavedChoice::Discard;
        page.selectMode(QStringLiteral("timeline"));
    }
    {   // Quit through the window
        auto *modes = window.findChild<ModesPage *>();
        check(modes != nullptr, "the window has a Modes page");
        if (modes) {
            check(window.confirmQuit(), "quit without edits is not asked");
            modes->selectMode(QStringLiteral("mine"));
            modes->openEditor();
            modes->editorWidget()->editor().form().name = "Quit edit";
            asked = 0;
            answer = UnsavedChoice::Cancel;
            check(!window.confirmQuit() && asked == 1 && modes->hasUnsavedChanges(), "Cancel stops the quit");
            answer = UnsavedChoice::Discard;
            check(window.confirmQuit() && !modes->hasUnsavedChanges(), "Discard lets the quit go on");
        }
    }
    {   // Button modes page
        QFile second(tmpPath + QStringLiteral("/buttons/guard.conf"));
        check(second.open(QIODevice::WriteOnly) && second.write("NAME = Guard\nACTION = click\n") > 0, "write a second file");
        second.close();
        ButtonModesPage page(ctx);
        page.show();
        settle();
        check(page.selectButtonMode(QStringLiteral("latch")), "the first entry is listed");
        asked = 0;
        check(page.stageEdit(QStringLiteral("Pending"), QStringLiteral("d"), QStringLiteral("key:space")) && page.dirty(), "staged edit");
        answer = UnsavedChoice::Cancel;
        page.selectButtonMode(QStringLiteral("guard"));
        check(asked == 1 && page.currentId() == QStringLiteral("latch") && page.dirty(), "Cancel keeps the row and the edits");
        answer = UnsavedChoice::Discard;
        page.selectButtonMode(QStringLiteral("guard"));
        check(page.currentId() == QStringLiteral("guard") && !page.dirty() &&
                  !fileText(QStringLiteral("/buttons/latch.conf")).contains("Pending"),
              "Discard moves on and writes nothing");
        page.stageEdit(QStringLiteral("Kept"), QString(), QStringLiteral("click"));
        answer = UnsavedChoice::Save;
        page.selectButtonMode(QStringLiteral("latch"));
        check(page.currentId() == QStringLiteral("latch") && !page.dirty() &&
                  fileText(QStringLiteral("/buttons/guard.conf")).contains("NAME = Kept"),
              "Save writes the file and moves on");
        check(window.confirmQuit(), "quit with a clean button mode form is not asked");
        page.stageEdit(QStringLiteral("Late"), QString(), QStringLiteral("click"));
        asked = 0;
        answer = UnsavedChoice::Cancel;
        check(!page.confirmLeaveForm() && asked == 1, "the guard used before quit and New/Import answers Cancel");
        answer = UnsavedChoice::Discard;
        check(page.confirmLeaveForm(), "Discard lets it go on");
        QFile::remove(tmpPath + QStringLiteral("/buttons/guard.conf"));
    }
    unsaved_prompt::setUnsavedAnswer(nullptr);
    window.showPage(QStringLiteral("overview"));  // confirmQuit moved the window to the page with the edits
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

static bool underScrollArea(const QWidget *w) {
    for (const QWidget *p = w->parentWidget(); p; p = p->parentWidget())
        if (qobject_cast<const QAbstractScrollArea *>(p) && !qobject_cast<const QTreeWidget *>(p) &&
            p->objectName() != QLatin1String("pageScroll"))  // the scroll area around the whole page does not count
            return true;
    return false;
}

static bool profilesPageSelfTest(MainWindow &w, const QString &tmpPath, const std::function<bool()> &accessibility) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "profiles self-test failed: %s\n", what);
        ok = ok && cond;
    };
    w.show();
    w.showPage(QStringLiteral("profiles"));
    settle();
    auto *page = w.findChild<ProfilesPage *>();
    if (!page) return false;
    int asked = 0;
    UnsavedChoice answer = UnsavedChoice::Cancel;
    page->setUnsavedPrompt([&](const QString &) {
        ++asked;
        return answer;
    });
    auto tabs = [&] { return page->tabIds().join(QLatin1Char(',')); };

    // sub-tabs: the default mode, a device profile (default mode), a profile with an installed mode
    check(page->selectSection(0) && tabs() == QStringLiteral("general,buttons"), "default mode: General and Buttons");
    check(page->selectSection(1) && tabs() == QStringLiteral("general,channels,buttons,overrides"),
          "a profile has General, Channels, Buttons and Overrides");
    {   // a profile with an installed mode shows the mode's buttons read-only in General (own models: modes loaded)
        spn::ui::SettingsModel sm;
        std::vector<std::string> warnings;
        std::string err;
        sm.load((tmpPath + QStringLiteral("/dofscroll.conf")).toStdString(), &warnings, &err);
        sm.setModes(spn::loadModes((tmpPath + QStringLiteral("/modes")).toStdString(), nullptr),
                    (tmpPath + QStringLiteral("/modes")).toStdString());
        QtStrings qstr;
        UiContext c2;
        c2.settings = &sm;
        c2.strings = &qstr;
        c2.runner = nullptr;
        c2.modesDir = (tmpPath + QStringLiteral("/modes")).toStdString();
        c2.buttonModesDir = (tmpPath + QStringLiteral("/buttons")).toStdString();
        c2.runtimeDir = (tmpPath + QStringLiteral("/run2")).toStdString();
        ProfilesPage p2(c2);
        p2.show();
        settle();
        check(p2.selectSection(2) && p2.tabIds().join(QLatin1Char(',')) == QStringLiteral("general,channels,overrides"),
              "a profile with an installed mode has no Buttons tab");
        QComboBox *mode = nullptr;
        for (QComboBox *c : p2.findChildren<QComboBox *>())
            if (c->accessibleName() == QStringLiteral("Joystick mode")) mode = c;
        check(mode && mode->count() >= 3, "the installed modes are offered");
        if (mode) QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 0));  // Default mode
        settle();
        check(p2.tabIds().join(QLatin1Char(',')) == QStringLiteral("general,channels,buttons,overrides") && p2.selectedModified(),
              "choosing the default mode brings the Buttons tab back");
        check(p2.revertSelected() && p2.tabIds().join(QLatin1Char(',')) == QStringLiteral("general,channels,overrides"),
              "Revert restores the mode and hides the Buttons tab again");
    }
    // the last tab is remembered, also over a profile without it
    check(page->selectSection(1) && page->selectTab(QStringLiteral("overrides")), "select the Overrides tab");
    check(page->selectSection(3) && page->currentTabId() == QStringLiteral("overrides"), "the tab is kept for another profile");
    check(page->selectSection(0) && page->currentTabId() == QStringLiteral("general"), "a missing tab falls back to General");
    check(page->selectSection(1) && page->currentTabId() == QStringLiteral("overrides"), "the remembered tab comes back");

    // Save and Revert are not inside a scrolling area
    for (const QString &name : {QStringLiteral("Save"), QStringLiteral("Revert")}) {
        QPushButton *b = nullptr;
        for (QPushButton *c : page->findChildren<QPushButton *>())
            if (c->text() == name) b = c;
        check(b && !underScrollArea(b), "the footer button stays outside the scrolling area");
    }
    check(!page->revertEnabled() && !page->saveEnabled(), "nothing to save or revert at the start");

    // an edit makes the profile modified; Revert restores the saved value
    QLineEdit *edit = lineEditNamed(page, QStringLiteral("SCROLL_V_SENS"));
    check(edit && edit->text() == QStringLiteral("9"), "the override of the device profile is shown");
    auto editValue = [&](const QString &v) {
        QLineEdit *e = lineEditNamed(page, QStringLiteral("SCROLL_V_SENS"));
        if (!e) return false;
        e->setText(v);
        QMetaObject::invokeMethod(e, "editingFinished");
        return true;
    };
    check(editValue(QStringLiteral("5")) && page->selectedModified() && page->revertEnabled() && page->saveEnabled(),
          "an edit enables Save and Revert");
    check(page->revertSelected() && !page->selectedModified() && !page->revertEnabled() && !page->saveEnabled(), "Revert");
    edit = lineEditNamed(page, QStringLiteral("SCROLL_V_SENS"));
    check(edit && edit->text() == QStringLiteral("9") && page->currentTabId() == QStringLiteral("overrides"),
          "Revert reloads the saved value and keeps the tab");

    // guard: another profile
    check(editValue(QStringLiteral("5")), "edit again");
    asked = 0;
    answer = UnsavedChoice::Cancel;
    page->selectSection(3);
    check(!w.selectedProfileIs(3), "Cancel keeps the profile");
    check(asked == 1 && w.selectedProfileIs(1) && page->selectedModified(), "Cancel: asked once, edit still there");
    answer = UnsavedChoice::Discard;
    check(page->selectSection(3) && w.selectedProfileIs(3) && asked == 2, "Discard: the other profile opens");
    check(page->selectSection(1) && lineEditNamed(page, QStringLiteral("SCROLL_V_SENS")) &&
              lineEditNamed(page, QStringLiteral("SCROLL_V_SENS"))->text() == QStringLiteral("9") && !page->selectedModified(),
          "Discard dropped the edit");
    check(editValue(QStringLiteral("5")), "edit once more");
    answer = UnsavedChoice::Save;
    check(page->selectSection(3) && asked == 3 && !page->saveEnabled(), "Save writes and moves on");
    {
        QFile f(tmpPath + QStringLiteral("/dofscroll.conf"));
        check(f.open(QIODevice::ReadOnly) && f.readAll().contains("SCROLL_V_SENS = 5"), "the file holds the saved value");
    }

    // guard: another page; Cancel returns to the Profiles page
    check(page->selectSection(1) && editValue(QStringLiteral("6")), "edit before leaving the page");
    answer = UnsavedChoice::Cancel;
    asked = 0;
    w.showPage(QStringLiteral("overview"));
    settle();
    check(asked == 1 && w.currentPageId() == QStringLiteral("profiles") && page->selectedModified(), "Cancel stays on the Profiles page");
    answer = UnsavedChoice::Discard;
    w.showPage(QStringLiteral("overview"));
    settle();
    check(asked == 2 && w.currentPageId() == QStringLiteral("overview") && !page->selectedModified(), "Discard leaves the page");
    w.showPage(QStringLiteral("profiles"));
    settle();

    // guard: closing the window (the event is only sent, so the window stays open)
    check(page->selectSection(1) && editValue(QStringLiteral("7")), "edit before closing");
    answer = UnsavedChoice::Cancel;
    asked = 0;
    QCloseEvent ev;
    QApplication::sendEvent(&w, &ev);
    check(asked == 1 && !ev.isAccepted(), "Cancel stops the close");
    answer = UnsavedChoice::Discard;
    QCloseEvent ev2;
    QApplication::sendEvent(&w, &ev2);
    check(asked == 2 && ev2.isAccepted() && !page->saveEnabled(), "Discard lets the window close");
    page->setUnsavedPrompt(nullptr);

    // search: the profile list and the settings rows
    page->setProfileFilter(QStringLiteral("FIRE"));
    check(page->visibleProfileCount() == 1, "the profile search finds one program");
    page->setProfileFilter(QStringLiteral("pro"));
    check(page->visibleProfileCount() == 1, "the profile search finds the device");
    page->setProfileFilter(QStringLiteral("zzz"));
    check(page->visibleProfileCount() == 0, "the profile search can find nothing");
    page->setProfileFilter(QString());
    check(page->visibleProfileCount() == 3, "an empty search shows every profile");
    check(page->selectSection(1), "select the device profile");
    page->setSettingsFilter(QString());
    const int all = page->settingRowCount();
    page->setSettingsFilter(QStringLiteral("scroll_v_sens"));
    const int some = page->settingRowCount();
    check(some >= 1 && some < all, "the settings search keeps the matching rows");
    page->setSettingsFilter(QStringLiteral("no such setting"));
    check(page->settingRowCount() == 0, "the settings search can find nothing");
    page->setSettingsFilter(QString());
    check(page->settingRowCount() == all, "an empty search shows every row again");

    // accessibility and tab order of the new widgets, on every tab
    for (const QString &id : page->tabIds()) {
        page->selectTab(id);
        check(accessibility(), "every input of the tab has an accessible name");
    }
    w.showPage(QStringLiteral("profiles"));
    settle();
    const QStringList order = {QStringLiteral("Search profiles"), QStringLiteral("Profiles"), QStringLiteral("Search settings"),
                               QStringLiteral("Profile sections")};
    int pos = -1;
    QWidget *start = lineEditNamed(page, QStringLiteral("Search profiles"));
    QList<QWidget *> chain;
    QWidget *c = start;
    do {
        chain << c;
        c = c ? c->nextInFocusChain() : nullptr;
    } while (c && c != start && chain.size() < 4000);
    for (const QString &name : order) {
        int found = -1;
        for (int i = 0; i < chain.size(); ++i)
            if (chain[i]->accessibleName() == name) found = i;
        check(found > pos, "the search boxes, the list and the tabs follow in tab order");
        pos = std::max(pos, found);
    }
    return ok;
}

// Mode editor sub-tabs: Settings, Buttons, Overrides; Save and Revert stay outside the scrolling
// area, the last tab is kept over a rebuild (Revert).
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
    check(ed->tabIds() == QStringList({QStringLiteral("settings"), QStringLiteral("buttons"), QStringLiteral("overrides")}),
          "Settings, Buttons and Overrides");
    check(ed->currentTabId() == QStringLiteral("settings"), "the first tab is Settings");
    for (const QString &name : {QStringLiteral("Save"), QStringLiteral("Revert")}) {
        QPushButton *b = nullptr;
        for (QPushButton *c : ed->findChildren<QPushButton *>())
            if (c->text() == name) b = c;
        check(b && !underScrollArea(b), "the footer button stays outside the scrolling area");
    }
    check(!ed->revertEnabled() && !ed->saveEnabled(), "an unchanged mode has nothing to save");
    ed->editor().form().overrides.emplace_back("SCROLL_V_SENS", "abc");
    ed->save();  // refused: the invalid value; shows the problem list and enables Revert
    check(ed->revertEnabled() && !ed->saveEnabled(), "an invalid edit enables Revert only");
    check(ed->selectTab(QStringLiteral("buttons")), "select the Buttons tab");
    QPushButton *revert = nullptr;
    for (QPushButton *c : ed->findChildren<QPushButton *>())
        if (c->text() == QStringLiteral("Revert")) revert = c;
    if (revert) revert->click();
    settle();
    check(ed->currentTabId() == QStringLiteral("buttons") && !ed->revertEnabled(), "Revert keeps the tab and reloads the file");
    ed->selectTab(QStringLiteral("overrides"));
    QLineEdit *ov = lineEditNamed(ed, QStringLiteral("SCROLL_V_SENS"));
    check(ov && ov->text() == QStringLiteral("2"), "the Overrides tab shows the saved override");
    return ok;
}

// Extensions page: lists modes and button modes with their origin, adds a shipped example and removes it again.
static bool extensionsSelfTest(const UiContext &ctx, const QString &tmpPath) {
    bool ok = true;
    auto check = [&ok](bool cond, const char *what) {
        if (!cond) std::fprintf(stderr, "extensions self-test failed: %s\n", what);
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
    ExtensionsPage page(ctx);  // standalone: own catalogs
    page.show();
    settle();
    const int before = page.extensionCount();
    check(before >= 6, "built-in modes, the file modes and the examples are listed");
    check(page.selectExtension(QStringLiteral("mode:ex-one")) && page.installSelected(), "install a mode example");
    check(QFile::exists(tmpPath + QStringLiteral("/modes/ex-one.conf")), "the example file was copied");
    check(page.extensionCount() == before, "the entry stays in the list, now as your file");
    check(page.removeSelected() && !QFile::exists(tmpPath + QStringLiteral("/modes/ex-one.conf")), "remove it again");
    check(page.selectExtension(QStringLiteral("button:ex-btn")) && page.installSelected() &&
              QFile::exists(tmpPath + QStringLiteral("/buttons/ex-btn.conf")),
          "install a button mode example");
    check(page.removeSelected(), "remove the button mode");
    check(page.selectExtension(QStringLiteral("mode:normal")) && !page.removeSelected() && !page.installSelected(),
          "a built-in mode can neither be removed nor installed");
    qunsetenv("DOFSCROLL_EXAMPLES");
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
    check(opened == QStringList({QStringLiteral("channels"), QStringLiteral("axes"), QStringLiteral("modes")}),
          "the task buttons open Channels, Axes and Modes");
    page.findChild<QPushButton *>(QStringLiteral("new-program-profile"))->click();
    check(picker == 1, "New profile for the running program is requested");

    // mode switch: disabled without a daemon, then enabled and marked with the running mode
    page.refresh();
    check(page.modeCombo()->count() >= 3 && !page.modeCombo()->isEnabled(), "mode switch lists the modes but is disabled without a daemon");
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

    // Channels: the config key in tooltip and accessible description, the filter and the width limit
    {
        QWidget *channels = createChannelsPage(ctx);
        channels->resize(2400, 900);
        channels->show();
        settle();
        QAbstractSpinBox *sensV = nullptr, *sensH = nullptr;
        int described = 0, spins = 0;
        for (auto *s : channels->findChildren<QAbstractSpinBox *>()) {
            if (s->objectName() == QStringLiteral("qt_spinbox_lineedit")) continue;
            ++spins;
            described += s->accessibleDescription().contains(QStringLiteral("Config key: ")) &&
                         s->toolTip().contains(QStringLiteral("Config key: "));
            if (s->toolTip().contains(QStringLiteral("Config key: SCROLL_V_SENS"))) sensV = s;
            if (s->toolTip().contains(QStringLiteral("Config key: SCROLL_H_SENS"))) sensH = s;
        }
        check(spins > 0 && described == spins, "every spin box row names its config key");
        check(sensV && sensH, "the sensitivity rows exist");
        QLineEdit *filter = nullptr;
        for (auto *e : channels->findChildren<QLineEdit *>())
            if (e->accessibleName() == QStringLiteral("Filter settings")) filter = e;
        check(filter != nullptr, "the Channels page has a filter box");
        if (filter && sensV && sensH) {
            filter->setText(QStringLiteral("scroll_h_sens"));
            check(sensH->isVisibleTo(channels) && !sensV->isVisibleTo(channels), "the filter finds a row by its config key");
            filter->setText(QStringLiteral("Vertical scroll"));
            check(sensV->isVisibleTo(channels) && !sensH->isVisibleTo(channels), "the filter finds the rows of a group by its title");
            filter->setText(QStringLiteral("nothing like it"));
            check(!sensV->isVisibleTo(channels) && !sensH->isVisibleTo(channels), "a filter without a match hides every row");
            filter->setText(QString());
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
        conf.write("[global]\n\n[device:SpaceMouse Pro]\nSCROLL_V_SENS = 9\n\n[app:firefox]\nMODE = timeline\nBUTTON_1 = key:space\nBUTTON_2 = buttonmode:latch-nav\nBUTTON_3 = buttonmode:gone\n\n"
                   "[app:blender]\nENABLED = 0\n\n[app:gimp]\n");
        conf.close();
        MainWindow w(ctx);
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
        if (!unsavedGuardSelfTest(ctx, tmp.path(), w)) return 1;
        if (!extensionsSelfTest(ctx, tmp.path())) return 1;
        if (!modeLibrarySelfTest(ctx, tmp.path())) return 1;
        if (!modeEditorTabsSelfTest(ctx)) return 1;
        ModesPage modes(ctx);  // standalone (own catalog): built-in normal, off, timeline + the file mode
        modes.show();
        Tray tray(&w, icon, ctx);
        // Must not crash; the result depends on the platform (offscreen: usually false).
        const bool avail = QSystemTrayIcon::isSystemTrayAvailable();
        std::printf("self-test: entries=%d profiles=%d modes=%d tray-modes=%d tray-entries=%d tray-available=%d\n",
                    w.pageCount(), w.profileCount(), modes.modeCount(), tray.modeMenuCount(),
                    tray.menuEntryCount(), avail ? 1 : 0);
        // every built-in (however many the embedded library holds) plus the one mode file of this test
        const int expectedModes = static_cast<int>(spn::loadModes("", nullptr).modes.size()) + 1;
        const bool ok = w.pageCount() == 8 && w.categoryCount() == 5 && w.profileCount() >= 0 && !w.profilePageHasModeSelector() &&
                        modes.modeCount() == expectedModes && tray.modeMenuCount() == expectedModes && tray.menuEntryCount() >= 8;
        if (!ok) return 1;
        if (!overviewSelfTest(w)) return 1;
        if (!quickActionsSelfTest(ctx, tmp.path())) return 1;
        if (!accessibilitySelfTest(w)) return 1;
        if (!profilesPageSelfTest(w, tmp.path(), [&w] { return accessibilitySelfTest(w); })) return 1;
        if (!themeSelfTest(w)) return 1;
        if (!wheelSelfTest()) return 1;
        if (!defaultSizeFitsSelfTest(w)) return 1;
        if (!windowStateSelfTest(ctx, tmp.path())) return 1;
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
    // after "Restart interface" the old instance is still shutting down, so wait longer.
    if (!lock.tryLock(spn::ui::lockWaitMs(restarted))) {
        if (spn::ui::classifyLockFailure(lock.error() == QLockFile::LockFailedError) ==
            spn::ui::LockOutcome::AlreadyRunning) {
            QFile m(raiseMarker);
            if (m.open(QIODevice::WriteOnly)) m.close();
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

    // First start, as the GTK tray does (src/ui/model/first_start.hpp).
    std::fputs(spn::ui::runFirstStart().text("dofscroll-ui").c_str(), stderr);

    // The theme of the user (before any window exists, so the whole interface starts in it).
    applyTheme(spn::ui::loadUiSettings(spn::ui::uiSettingsPath()).theme);

    MainWindow window(ctx);
    window.setWindowIcon(icon);
    const bool hasTray = QSystemTrayIcon::isSystemTrayAvailable();
    window.setTrayAvailable(hasTray);  // without a tray, closing the window quits
    QObject::connect(&window, &MainWindow::quitRequested, &app, &QApplication::quit);
    window.enableStatePersistence(spn::ui::uiSettingsPath());  // size, position and page of the last run
    Tray tray(&window, icon, ctx);
    tray.show();
    // Starts minimized to the tray: no window is shown. Without a system tray the icon would be
    // unreachable, so the window is then shown minimized (the taskbar or the second start reach it).
    if (!hasTray) window.showMinimized();
    QTimer raiseTimer;
    QObject::connect(&raiseTimer, &QTimer::timeout, [&] {
        if (QFile::exists(raiseMarker) && QFile::remove(raiseMarker)) window.bringToFront();
    });
    raiseTimer.start(500);
    const int code = app.exec();
    window.saveWindowState();  // "Quit" from the tray with the window open
    return code;
}
