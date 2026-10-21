// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/device_page.hpp"

#include <QGroupBox>
#include <QLabel>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

#include "ui/model/design_tokens.hpp"
#include "ui/model/device_info_model.hpp"
#include "ui_i18n.hpp"

QWidget *createDevicePage(const UiContext &ctx) { return new DevicePage(ctx); }

DevicePage::DevicePage(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx) {
    namespace tk = spn::ui::tokens;
    const std::string dir = ctx_.runtimeDir;
    feedClient_ = std::make_unique<spn::LiveFeedClient>([dir](std::string *err) { return spn::connectFeed(dir, err); });
    clock_.start();

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("devicePageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    auto *content = new QWidget;
    content->setMaximumWidth(tk::kMaxContentWidth);
    auto *col = new QVBoxLayout(content);
    col->setContentsMargins(tk::kPageMargin, tk::kSpaceLg, tk::kPageMargin, tk::kPageMargin);
    col->setSpacing(tk::kSectionGap);

    auto *intro = new QLabel(T("The connected 3D mouse and how many buttons a profile offers for it. Live axes and the axis "
                               "assistant are on the Axes tab."));
    intro->setWordWrap(true);
    intro->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(intro);

    auto *box = new QGroupBox(T("Connected devices"));
    box->setAccessibleName(T("Connected devices"));
    auto *bl = new QVBoxLayout(box);
    bl->setContentsMargins(tk::kCardPadding, tk::kCardPadding, tk::kCardPadding, tk::kCardPadding);
    none_ = new QLabel(T("No device found. Start spacenavd and plug in the device."));
    none_->setWordWrap(true);
    bl->addWidget(none_);
    listHost_ = new QWidget;
    list_ = new QVBoxLayout(listHost_);
    list_->setContentsMargins(0, 0, 0, 0);
    list_->setSpacing(tk::kSpaceMd);
    bl->addWidget(listHost_);
    col->addWidget(box);

    state_ = new QLabel;
    state_->setWordWrap(true);
    state_->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(state_);
    col->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll);

    timer_ = new QTimer(this);
    timer_->setInterval(500);
    connect(timer_, &QTimer::timeout, this, [this] { refresh(); });
    refresh();
}

void DevicePage::refresh() {
    feedModel_.pump(*feedClient_, static_cast<int64_t>(clock_.nsecsElapsed()));
    if (ctx_.status) devices_.setStatusDevice(ctx_.status->status().device);
    devices_.setFeed(feedModel_.hasDevices(), feedModel_.devices());
    const spn::ui::IdentityStrings fallback;
    const spn::ui::StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    const std::vector<spn::ui::DeviceInfoLine> infos = spn::ui::buildDeviceInfo(devices_.devices());

    // Rebuild the rows only when their text changed (this runs twice a second).
    QStringList lines, notes;
    for (const spn::ui::DeviceInfoLine &l : infos) {
        lines << qs8(spn::ui::deviceInfoText(l, strings));
        notes << qs8(spn::ui::deviceButtonSource(l, strings));
    }
    const QString key = lines.join(QLatin1Char('\n')) + QLatin1Char('\x1f') + notes.join(QLatin1Char('\n'));
    if (key != key_) {
        key_ = key;
        lines_ = lines;
        while (QLayoutItem *it = list_->takeAt(0)) {
            if (QWidget *w = it->widget()) w->deleteLater();
            delete it;
        }
        for (int i = 0; i < lines.size(); ++i) {
            auto *name = new QLabel(lines[i]);
            name->setTextFormat(Qt::PlainText);  // built from the name a USB device reports
            name->setProperty("heading", QStringLiteral("section"));
            name->setWordWrap(true);
            name->setTextInteractionFlags(Qt::TextSelectableByMouse);
            auto *note = new QLabel(notes[i]);
            note->setTextFormat(Qt::PlainText);
            note->setWordWrap(true);
            note->setForegroundRole(QPalette::PlaceholderText);
            list_->addWidget(name);
            list_->addWidget(note);
        }
    }
    none_->setVisible(infos.empty());
    listHost_->setVisible(!infos.empty());
    QString state;
    if (ctx_.status) {
        state = qs8(ctx_.status->daemonRunning() ? strings.tr("Daemon: running") : strings.tr("Daemon: not running"));
        const std::string warn = ctx_.status->warningLine(strings);
        if (!warn.empty()) state += QLatin1Char('\n') + qs8(warn);
    }
    state_->setText(state);
}

void DevicePage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    refresh();
    timer_->start();
}

void DevicePage::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    timer_->stop();
}
