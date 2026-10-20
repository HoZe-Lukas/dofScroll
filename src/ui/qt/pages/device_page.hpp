// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Device" tab of the Device category: the connected devices with their button count and the
// state of the daemon. Read-only; the axes view and the axis assistant are the "Axes" tab next to
// it. Data: the live feed of the daemon (device list), the status file as fallback.
#pragma once
#include <QWidget>

#include <memory>
#include <string>

#include <QElapsedTimer>
#include <QStringList>

#include "live_feed/live_feed.hpp"
#include "ui/model/device_source_model.hpp"
#include "ui/model/live_feed_model.hpp"
#include "ui_context.hpp"

class QLabel;
class QTimer;
class QVBoxLayout;

class DevicePage : public QWidget {
    Q_OBJECT
public:
    explicit DevicePage(const UiContext &ctx, QWidget *parent = nullptr);
    QStringList deviceLines() const { return lines_; }  // the text of the device rows (self-test)
    void refresh();

protected:
    void showEvent(QShowEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    UiContext ctx_;
    std::unique_ptr<spn::LiveFeedClient> feedClient_;
    spn::LiveFeedModel feedModel_;
    spn::ui::DeviceSourceModel devices_;
    QElapsedTimer clock_;
    QTimer *timer_ = nullptr;
    QWidget *listHost_ = nullptr;
    QVBoxLayout *list_ = nullptr;
    QLabel *state_ = nullptr, *none_ = nullptr;
    QStringList lines_;
    QString key_;
};

QWidget *createDevicePage(const UiContext &ctx);
