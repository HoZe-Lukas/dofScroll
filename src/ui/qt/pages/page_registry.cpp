// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "page_registry.hpp"

#include <QWidget>

#include "axes_page.hpp"
#include "ui_i18n.hpp"

namespace {
struct PageEntry {
    const char *title;  // English navigation title
    QWidget *(*create)();
};
const PageEntry kPages[] = {
    {"Axes / assistant", &createAxesPage},
};
}  // namespace

QWidget *createRegisteredPage(const QString &translatedTitle) {
    for (const PageEntry &e : kPages)
        if (T(e.title) == translatedTitle) return e.create();
    return nullptr;
}
