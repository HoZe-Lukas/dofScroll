// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/page_registry.hpp"

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

#include "pages/channels_page.hpp"
#include "pages/general_page.hpp"
#include "pages/modes_page.hpp"
#include "ui_i18n.hpp"

namespace {

QWidget *placeholderPage(const char *title) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->addWidget(new QLabel(QStringLiteral("<b>") + T(title) + QStringLiteral("</b>")));
    auto *todo = new QLabel(T("not implemented yet"));
    todo->setAlignment(Qt::AlignCenter);
    l->addWidget(todo, 1);
    return w;
}

// Replaced by the real page of its branch: point the registry line below to it.
QWidget *axesPlaceholder(const UiContext &) { return placeholderPage("Axes"); }

}  // namespace

// One line per page, in navigation order; each page declares its factory in its own header.
const std::vector<PageFactory> &pageFactories() {
    static const std::vector<PageFactory> factories = {
        {"channels", "Channels", createChannelsPage},
        {"general", "General", createGeneralPage},
        {"axes", "Axes", axesPlaceholder},
        {"modes", "Modes", makeModesPage},
    };
    return factories;
}
