// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Channels" page: vertical scroll, horizontal scroll, zoom and daemon settings of [global], with
// a live curve preview per channel. Thin view over SettingsModel and channels_page_model.hpp.
#pragma once
#include <QWidget>

#include "ui_context.hpp"

QWidget *createChannelsPage(const UiContext &ctx);
