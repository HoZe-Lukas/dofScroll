// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "tray.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QPainter>
#include <QPixmap>
#include <QTimer>

#include "log_dialog.hpp"
#include "main_window.hpp"
#include "ui_i18n.hpp"
#include "plain_text.hpp"

namespace {
// The base icon drawn with reduced opacity: no hard-coded colors, so it follows the icon's own
// (theme friendly) colors. Without an SVG plugin the pixmaps are empty and the base icon is used.
QIcon withOpacity(const QIcon &base, qreal opacity) {
    QIcon out;
    for (int size : {16, 22, 24, 32, 48, 64}) {
        const QPixmap src = base.pixmap(size, size);
        if (src.isNull()) return base;
        QPixmap dst(src.size());
        dst.setDevicePixelRatio(src.devicePixelRatio());
        dst.fill(Qt::transparent);
        QPainter p(&dst);
        p.setOpacity(opacity);
        p.drawPixmap(0, 0, src);
        p.end();
        out.addPixmap(dst);
    }
    out.setIsMask(base.isMask());
    return out;
}

// macOS menu bar: a monochrome template icon that the system colors for light and dark menu bars.
// Elsewhere the colored icon is used as given.
QIcon trayBaseIcon(const QIcon &colored) {
#ifdef Q_OS_MACOS
    (void)colored;
    QIcon t(QStringLiteral(":/dofscroll-tray-template.svg"));
    t.setIsMask(true);
    return t;
#else
    return colored;
#endif
}
}  // namespace

Tray::Tray(MainWindow *window, const QIcon &baseIcon, const UiContext &ctx, QObject *parent)
    : QObject(parent),
      controller_(spn::ui::TrayController::Paths{ctx.configPath, ctx.modesDir, ctx.runtimeDir}, *ctx.runner, strings_),
      window_(window),
      enabledIcon_(trayBaseIcon(baseIcon)),
      disabledIcon_(withOpacity(enabledIcon_, 0.55)),
      notRunningIcon_(withOpacity(enabledIcon_, 0.3)),
      timer_(new QTimer(this)) {
    controller_.hooks().openSettings = [this] { window_->bringToFront(); };
    controller_.hooks().flush = [this] { window_->flushPending(); };
    controller_.hooks().quit = [this] {
        window_->flushPending();  // the last edit is on disk before the process ends
        QApplication::quit();
    };
    controller_.hooks().showLog = [this, dir = ctx.runtimeDir] { LogDialog::showFor(window_, dir, strings_); };
    icon_.setContextMenu(&menu_);
    // Closing the window does not quit: say so once per run.
    connect(window_, &MainWindow::hiddenToTray, this, [this] {
        icon_.showMessage(QStringLiteral("dofscroll"), T("dofscroll is still running in the tray"), QSystemTrayIcon::Information,
                          4000);
    });
    connect(&icon_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) window_->bringToFront();
    });
    connect(timer_, &QTimer::timeout, this, &Tray::refresh);
    timer_->start(1000);
    controller_.refresh(QDateTime::currentSecsSinceEpoch());
    rebuild();
    applyState();
}

void Tray::show() { icon_.show(); }

int Tray::modeMenuCount() const {
    const auto &entries = controller_.menu().entries;
    for (const auto &e : entries)
        if (e.kind == spn::ui::TrayEntry::Kind::Submenu) return int(e.children.size());
    return 0;
}

QStringList Tray::modeMenuToolTips() const {
    QStringList out;
    for (QMenu *sub : menu_.findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly))
        for (QAction *a : sub->actions()) out << a->toolTip();
    return out;
}

int Tray::menuEntryCount() const { return int(menu_.actions().size()); }

void Tray::refresh() {
    if (controller_.refresh(QDateTime::currentSecsSinceEpoch())) {
        rebuild();
        applyState();
    }
}

void Tray::applyState() {
    using Icon = spn::ui::TrayController::Icon;
    switch (controller_.icon()) {
        case Icon::Enabled: icon_.setIcon(enabledIcon_); break;
        case Icon::Disabled: icon_.setIcon(disabledIcon_); break;
        case Icon::NotRunning: icon_.setIcon(notRunningIcon_); break;
    }
    icon_.setToolTip(QString::fromStdString(controller_.tooltip()));
}

void Tray::rebuild() {
    // Submenus are children of the menu; clear() alone would leave them until the menu dies.
    for (QMenu *m : menu_.findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly)) delete m;
    menu_.clear();
    for (const spn::ui::TrayEntry &e : controller_.menu().entries) addEntry(&menu_, e);
}

void Tray::addEntry(QMenu *into, const spn::ui::TrayEntry &e) {
    using Kind = spn::ui::TrayEntry::Kind;
    if (!e.visible) return;
    if (e.kind == Kind::Separator) {
        into->addSeparator();
        return;
    }
    if (e.kind == Kind::Submenu) {
        QMenu *sub = into->addMenu(QString::fromStdString(e.text));
        sub->setToolTipsVisible(true);
        auto *group = new QActionGroup(sub);
        group->setExclusive(true);
        for (const spn::ui::TrayEntry &c : e.children) {
            QAction *a = sub->addAction(QString::fromStdString(c.text));
            spn::ui::qt::setPlainToolTip(a, QString::fromStdString(c.tooltip));  // the description of the joystick mode
            a->setStatusTip(QString::fromStdString(c.tooltip));
            a->setCheckable(true);
            a->setChecked(c.checked);
            group->addAction(a);
            connect(a, &QAction::triggered, this, [this, c] { run(c); });
        }
        return;
    }
    QAction *a = into->addAction(QString::fromStdString(e.text));
    if (e.kind == Kind::Info) {
        a->setEnabled(false);
        return;
    }
    if (e.kind == Kind::Check || e.kind == Kind::Radio) {
        a->setCheckable(true);
        a->setChecked(e.checked);
    }
    a->setEnabled(e.enabled);
    connect(a, &QAction::triggered, this, [this, e] { run(e); });
}

void Tray::run(const spn::ui::TrayEntry &e) {
    if (!controller_.perform(e) && !controller_.lastError().empty())
        icon_.showMessage(QStringLiteral("dofscroll"), QString::fromStdString(controller_.lastError()),
                          QSystemTrayIcon::Warning, 5000);
    // Rebuild after the menu finished handling this click (also resets a toggle that failed).
    QTimer::singleShot(0, this, [this] {
        controller_.refresh(QDateTime::currentSecsSinceEpoch());
        rebuild();
        applyState();
    });
}
