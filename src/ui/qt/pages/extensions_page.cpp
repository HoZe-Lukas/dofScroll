// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/extensions_page.hpp"

#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "ui/model/design_tokens.hpp"
#include "ui/model/mode_catalog.hpp"
#include "ui/model/ui_terms.hpp"
#include "ui_i18n.hpp"

using namespace spn::ui;

QWidget *createExtensionsPage(const UiContext &ctx) { return new ExtensionsPage(ctx); }

QString ExtensionsPage::kindName(ExtensionKind k) { return k == ExtensionKind::JoystickMode ? QStringLiteral("joystick") : QStringLiteral("button"); }

ExtensionsPage::ExtensionsPage(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx) {
    namespace tk = spn::ui::tokens;
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("extensionsPageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    outer->addWidget(scroll);
    auto *content = new QWidget;
    content->setMaximumWidth(tk::kMaxContentWidth);
    auto *col = new QVBoxLayout(content);
    col->setContentsMargins(tk::kPageMargin, tk::kSpaceLg, tk::kPageMargin, tk::kPageMargin);
    col->setSpacing(tk::kSectionGap);
    scroll->setWidget(content);

    auto *intro = new QLabel(T("User modes are plain files in your configuration folders. dofscroll reads them with its own "
                               "checks and never runs them. Switch a file off to keep it without using it; a switched-off file "
                               "with the id of a built-in joystick mode or button mode lets the built-in one apply again."));
    intro->setWordWrap(true);
    intro->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(intro);
    auto *safety = new QLabel(T("Joystick mode files can press any key. Only use joystick mode files from trusted sources."));
    safety->setWordWrap(true);
    safety->setForegroundRole(QPalette::PlaceholderText);
    col->addWidget(safety);

    message_ = new QLabel;
    message_->setWordWrap(true);
    message_->setAccessibleName(T("Problem"));
    message_->setVisible(false);
    col->addWidget(message_);

    auto addSection = [&](const QString &title, QVBoxLayout **list, QLabel **none, QWidget **openButton, const std::string &dir) {
        auto *box = new QGroupBox(title);
        box->setAccessibleName(title);
        auto *bl = new QVBoxLayout(box);
        bl->setContentsMargins(tk::kCardPadding, tk::kCardPadding, tk::kCardPadding, tk::kCardPadding);
        bl->setSpacing(tk::kSpaceMd);
        *none = new QLabel(T("No files of yours yet. Create or import one on the Joystick modes and Button modes pages."));
        (*none)->setWordWrap(true);
        bl->addWidget(*none);
        auto *host = new QWidget;
        *list = new QVBoxLayout(host);
        (*list)->setContentsMargins(0, 0, 0, 0);
        (*list)->setSpacing(tk::kSpaceMd);
        bl->addWidget(host);
        auto *open = new QPushButton(T("Open folder"));
        open->setAccessibleName(title + QStringLiteral(": ") + T("Open folder"));
        open->setToolTip(QString::fromStdString(dir));
        connect(open, &QPushButton::clicked, this, [this, dir] {
            std::string err;
            if (ctx_.runner && ctx_.runner->openPath(dir, &err)) return;
            showMessage(T("Could not open the folder.") + (err.empty() ? QString() : QStringLiteral(" ") + qs8(err)), true);
        });
        bl->addWidget(open, 0, Qt::AlignLeft);
        *openButton = open;
        col->addWidget(box);
    };
    addSection(T(terms::kJoystickModes) + QStringLiteral(" - ") + T("files"), &joystickList_, &joystickNone_, &openJoystick_, ctx_.modesDir);
    addSection(T(terms::kButtonModes) + QStringLiteral(" - ") + T("files"), &buttonList_, &buttonNone_, &openButton_, ctx_.buttonModesDir);
    col->addStretch(1);
    refresh();
}

void ExtensionsPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    refresh();
}

std::vector<QString> ExtensionsPage::usedBy(ExtensionKind kind, const std::string &id) const {
    std::vector<QString> out;
    if (!ctx_.settings) return out;
    if (kind == ExtensionKind::JoystickMode) {
        for (const ModeCatalog::ModeUse &u : ModeCatalog::usedBy(ctx_.settings->profiles(), id)) out.push_back(qs8(ModeCatalog::useTitle(u)));
    } else if (ctx_.buttonModes) {
        const spn::ModeSet modes = spn::loadModes(ctx_.modesDir, nullptr);
        for (const std::string &s : ctx_.buttonModes->referencedBy(id, *ctx_.settings, modes)) out.push_back(qs8(s));
    }
    return out;
}

void ExtensionsPage::refresh() {
    entries_ = listExtensions(ctx_.modesDir, ctx_.buttonModesDir, [this](ExtensionKind k, const std::string &id) {
        std::vector<std::string> out;
        for (const QString &q : usedBy(k, id)) out.push_back(q.toUtf8().toStdString());
        return out;
    });
    rebuild();
}

void ExtensionsPage::rebuild() {
    switches_.clear();
    for (QVBoxLayout *list : {joystickList_, buttonList_})
        while (QLayoutItem *it = list->takeAt(0)) {
            if (QWidget *w = it->widget()) w->deleteLater();
            delete it;
        }
    int joystick = 0, button = 0;
    std::vector<QWidget *> joystickOrder, buttonOrder;  // the controls of the rows, in reading order
    for (const ExtensionEntry &e : entries_) {
        const bool isJoystick = e.kind == ExtensionKind::JoystickMode;
        (isJoystick ? joystick : button)++;
        const QString label = qs8(e.label), id = qs8(e.id);
        auto *row = new QWidget;
        auto *g = new QGridLayout(row);
        g->setContentsMargins(0, 0, 0, 0);
        g->setColumnStretch(1, 1);
        auto *sw = new QCheckBox;
        sw->setChecked(e.enabled);
        sw->setAccessibleName(label + QStringLiteral(": ") + T("Switched on"));
        sw->setToolTip(e.enabled ? T("Switch this file off (it is kept, but not used)") : T("Switch this file on"));
        const QString kind = kindName(e.kind);
        connect(sw, &QCheckBox::clicked, this, [this, kind, id](bool on) { setEnabled(kind, id, on); });
        switches_[kind + QLatin1Char(':') + id] = sw;
        g->addWidget(sw, 0, 0, 2, 1, Qt::AlignTop);

        QString text = QStringLiteral("<b>") + label.toHtmlEscaped() + QStringLiteral("</b>");
        if (label != id) text += QStringLiteral(" (") + id.toHtmlEscaped() + QLatin1Char(')');
        auto *name = new QLabel(text);
        name->setTextFormat(Qt::RichText);
        name->setWordWrap(true);
        g->addWidget(name, 0, 1);
        QStringList lines;
        if (!e.description.empty()) lines << qs8(e.description);
        lines << (e.enabled ? T("Switched on") : T("Switched off: the file is kept, but not used"));
        if (!e.usedBy.empty()) {
            QStringList uses;
            for (const std::string &u : e.usedBy) uses << qs8(u);
            lines << T("Used by: %1").arg(uses.join(QStringLiteral(", ")));
        }
        auto *detail = new QLabel(lines.join(QLatin1Char('\n')));
        detail->setWordWrap(true);
        detail->setTextFormat(Qt::PlainText);
        detail->setForegroundRole(QPalette::PlaceholderText);
        detail->setAccessibleName(label + QStringLiteral(": ") + T("Details"));
        g->addWidget(detail, 1, 1);
        auto *rm = new QToolButton;
        rm->setText(T("Remove"));
        rm->setToolTip(T("Delete this file"));
        rm->setAccessibleName(T("Remove") + QLatin1Char(' ') + label);
        connect(rm, &QToolButton::clicked, this, [this, kind, id] { removeEntry(kind, id, true); });
        g->addWidget(rm, 0, 2, 2, 1, Qt::AlignTop);
        (isJoystick ? joystickList_ : buttonList_)->addWidget(row);
        (isJoystick ? joystickOrder : buttonOrder).push_back(sw);
        (isJoystick ? joystickOrder : buttonOrder).push_back(rm);
    }
    // New widgets would be last in the Tab chain, after the "Open folder" buttons: set the order again
    std::vector<QWidget *> order = joystickOrder;
    order.push_back(openJoystick_);
    order.insert(order.end(), buttonOrder.begin(), buttonOrder.end());
    order.push_back(openButton_);
    for (size_t i = 1; i < order.size(); ++i) QWidget::setTabOrder(order[i - 1], order[i]);
    joystickNone_->setVisible(joystick == 0);
    buttonNone_->setVisible(button == 0);
}

const ExtensionEntry *ExtensionsPage::find(const QString &kind, const QString &id) const {
    for (const ExtensionEntry &e : entries_)
        if (kindName(e.kind) == kind && qs8(e.id) == id) return &e;
    return nullptr;
}

bool ExtensionsPage::exists(const QString &kind, const QString &id) const { return find(kind, id) != nullptr; }

bool ExtensionsPage::isEnabled(const QString &kind, const QString &id) const {
    const ExtensionEntry *e = find(kind, id);
    return e && e->enabled;
}

QCheckBox *ExtensionsPage::switchOf(const QString &kind, const QString &id) const {
    const auto it = switches_.find(kind + QLatin1Char(':') + id);
    return it == switches_.end() ? nullptr : it->second;
}

QString ExtensionsPage::message() const { return problem_; }

void ExtensionsPage::showMessage(const QString &text, bool error) {
    problem_ = error ? text : QString();
    message_->setText(error ? mark(StatusKind::Error, text) : text);
    message_->setProperty("error", error);
    message_->style()->unpolish(message_);
    message_->style()->polish(message_);
    message_->setVisible(!text.isEmpty());
}

bool ExtensionsPage::setEnabled(const QString &kind, const QString &id, bool on) {
    const ExtensionEntry *e = find(kind, id);
    if (!e) return false;
    const ExtensionEntry entry = *e;
    const ExtensionResult r = setExtensionEnabled(entry, on);
    if (!r.ok) {
        showMessage(T("Could not switch the file: %1").arg(qs8(r.error)), true);
        refresh();  // the switch goes back to what the folder says
        return false;
    }
    showMessage(QString(), false);
    // the other pages read the folders again (and the daemon notices the change itself)
    if (ctx_.modes) ctx_.modes->refreshIfChanged();
    if (ctx_.buttonModes) ctx_.buttonModes->refreshIfChanged();
    ctx_.report(on ? T("Switched on: %1").arg(qs8(entry.label)) : T("Switched off: %1").arg(qs8(entry.label)), false);
    refresh();
    return true;
}

bool ExtensionsPage::removeEntry(const QString &kind, const QString &id, bool ask) {
    const ExtensionEntry *e = find(kind, id);
    if (!e) return false;
    const ExtensionEntry entry = *e;
    if (ask) {
        QString text = T("Delete the file \"%1\"? This cannot be undone.").arg(qs8(entry.label));
        if (!entry.usedBy.empty()) {
            QStringList uses;
            for (const std::string &u : entry.usedBy) uses << qs8(u);
            text += QStringLiteral("\n\n") + T("It is still used by: %1").arg(uses.join(QStringLiteral(", ")));
        }
        QMessageBox box(QMessageBox::Warning, T("Remove"), text, QMessageBox::NoButton, this);
        QPushButton *yes = box.addButton(T("Remove"), QMessageBox::AcceptRole);
        QPushButton *no = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        box.exec();
        if (box.clickedButton() != yes) return false;
    }
    const ExtensionResult r = removeExtension(entry);
    if (!r.ok) {
        showMessage(T("Could not remove the file: %1").arg(qs8(r.error)), true);
        refresh();
        return false;
    }
    showMessage(QString(), false);
    if (ctx_.modes) ctx_.modes->refreshIfChanged();
    if (ctx_.buttonModes) ctx_.buttonModes->refreshIfChanged();
    ctx_.report(T("Removed: %1").arg(qs8(entry.label)), false);
    refresh();
    return true;
}
