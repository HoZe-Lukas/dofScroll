// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "page_registry.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include "channels_page.hpp"
#include "general_page.hpp"

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

QWidget *axesPlaceholder(PageContext &) { return placeholderPage("Axes"); }

// Thin placeholder until the modes page lands: the list and actions will be driven by ModeCatalog.
QWidget *modesPlaceholder(PageContext &) {
    auto *w = new QWidget;
    auto *l = new QVBoxLayout(w);
    l->addWidget(new QLabel(QStringLiteral("<b>") + T("Modes") + QStringLiteral("</b>")));
    auto *hint = new QLabel(T("Mode files can press any key. Only use mode files from trusted "
                              "sources."));
    hint->setWordWrap(true);
    l->addWidget(hint);
    auto *row = new QHBoxLayout;
    auto *list = new QListWidget(w);
    list->addItem(T("not implemented yet"));
    row->addWidget(list, 1);
    auto *buttons = new QVBoxLayout;
    for (const QString &b : {T("New mode"), T("Duplicate"), T("Import file..."), T("Edit"),
                             T("Delete"), T("Open modes folder")})
        buttons->addWidget(new QPushButton(b, w));
    buttons->addStretch(1);
    row->addLayout(buttons);
    l->addLayout(row, 1);
    return w;
}

}  // namespace

const PageInfo *pageRegistry(int *count) {
    // One page per line: add yours here (navigation order).
    static const PageInfo pages[] = {
        {"channels", "Channels", &createChannelsPage},
        {"general", "General", &createGeneralPage},
        {"axes", "Axes", &axesPlaceholder},
        {"modes", "Modes", &modesPlaceholder},
    };
    if (count) *count = int(sizeof(pages) / sizeof(pages[0]));
    return pages;
}
