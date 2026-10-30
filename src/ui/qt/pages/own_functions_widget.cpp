// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/own_functions_widget.hpp"

#include <QGridLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "pages/new_function_dialog.hpp"
#include "plain_text.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
QPushButton *ownButton(const QString &text, const char *objectName, const QString &description, QWidget *parent) {
    auto *b = new QPushButton(text, parent);
    b->setObjectName(QString::fromLatin1(objectName));
    b->setAccessibleName(text);
    b->setAccessibleDescription(description);
    b->setMinimumHeight(24);
    b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);  // a long text must not widen the page
    b->setMinimumWidth(100);
    return b;
}
QLabel *ownLabel(const QString &text, QWidget *parent, const char *objectName = nullptr) {
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextFormat(Qt::PlainText);
    if (objectName) l->setObjectName(QString::fromLatin1(objectName));
    return l;
}
}  // namespace

const StringProvider &OwnFunctionsWidget::strings() const {
    static const IdentityStrings fallback;
    return ctx_.strings ? *ctx_.strings : fallback;
}

OwnFunctionsWidget::OwnFunctionsWidget(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx) {
    setObjectName(QStringLiteral("ownFunctions"));
    auto *v = new QVBoxLayout(this);
    auto *head = ownLabel(QStringLiteral("<b>") + T("Own functions of this profile").toHtmlEscaped() + QStringLiteral("</b>"), this);
    head->setTextFormat(Qt::RichText);
    v->addWidget(head);
    auto *intro = ownLabel(T("The functions you made with New function... in this profile. A function that several motions or buttons use is listed once."), this);
    v->addWidget(intro);
    empty_ = ownLabel(T("This profile has no functions of its own yet. Choose New function... in the list of a motion or a button."), this, "ownFunctionsEmpty");
    v->addWidget(empty_);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("ownFunctionsList"));
    list_->setAccessibleName(T("Own functions of this profile"));
    list_->setAccessibleDescription(T("The list of functions; each says what it does and which motions and buttons use it"));
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setWordWrap(true);
    list_->setTextElideMode(Qt::ElideNone);
    list_->setMinimumHeight(80);
    intro->setBuddy(list_);  // the visible label of the list
    v->addWidget(list_);
    auto *grid = new QGridLayout;
    edit_ = ownButton(T("Edit"), "ownFunctionEdit", T("Open the chosen function in the New function window"), this);
    delete_ = ownButton(T("Delete"), "ownFunctionDelete", T("Remove the chosen function from every motion and button that uses it; they inherit again"), this);
    asMode_ = ownButton(T("Save as joystick mode..."), "ownFunctionAsMode", T("Write a copy of the chosen function as a new joystick mode; this profile keeps its own function"), this);
    asButton_ = ownButton(T("Save as button mode..."), "ownFunctionAsButton", T("Write a copy of the chosen button function as a new button mode; this profile keeps its own function"), this);
    grid->addWidget(edit_, 0, 0);
    grid->addWidget(delete_, 0, 1);
    grid->addWidget(asMode_, 1, 0);
    grid->addWidget(asButton_, 1, 1);
    v->addLayout(grid);
    result_ = ownLabel(QString(), this, "ownFunctionsResult");
    result_->setAccessibleName(T("Result of the last change to the own functions"));
    result_->hide();
    v->addWidget(result_);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { refreshButtons(); });
    connect(list_, &QListWidget::itemActivated, this, [this](QListWidgetItem *) { editSelected(); });
    connect(edit_, &QPushButton::clicked, this, [this] { editSelected(); });
    connect(delete_, &QPushButton::clicked, this, [this] { deleteSelected(false); });
    connect(asMode_, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, T("Save as joystick mode"), T("Name of the new joystick mode:"), QLineEdit::Normal, QString(), &ok);
        if (ok) saveSelectedAsJoystickMode(name);
    });
    connect(asButton_, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, T("Save as button mode"), T("Name of the new button mode:"), QLineEdit::Normal, QString(), &ok);
        if (ok) saveSelectedAsButtonMode(name);
    });
    refreshButtons();
}

void OwnFunctionsWidget::refresh() { rebuild(); }

int OwnFunctionsWidget::count() const { return int(functions_.size()); }

void OwnFunctionsWidget::setSection(size_t section) {
    if (section != section_) result_->hide();  // the words about the last change stay while the page redraws
    section_ = section;
    rebuild();
}

void OwnFunctionsWidget::rebuild() {
    const int keep = list_->currentRow();
    functions_.clear();
    if (ctx_.settings) functions_ = ownFunctions(*ctx_.settings, section_, strings());
    list_->clear();
    for (const OwnFunction &f : functions_) {
        auto *item = new QListWidgetItem(qs8(f.words) + QStringLiteral(" - ") + T("used by %1").arg(qs8(usesWords(f.uses, strings()))));
        spn::ui::qt::setPlainToolTip(item, T("Stored as:") + QLatin1Char(' ') + qs8(f.text));
        list_->addItem(item);
    }
    if (!functions_.empty()) list_->setCurrentRow(std::min(std::max(keep, 0), int(functions_.size()) - 1));
    empty_->setVisible(functions_.empty());
    list_->setVisible(!functions_.empty());
    refreshButtons();
}

void OwnFunctionsWidget::refreshButtons() {
    const int row = list_->currentRow();
    const bool have = row >= 0 && size_t(row) < functions_.size();
    edit_->setEnabled(have);
    delete_->setEnabled(have);
    asMode_->setEnabled(have && ctx_.modes);
    asButton_->setEnabled(have && ctx_.buttonModes && functions_[size_t(row)].onButton());
    asButton_->setToolTip(have && !functions_[size_t(row)].onButton() ? T("Only a function of a button can be a button mode") : QString());
    for (QPushButton *b : {edit_, delete_, asMode_, asButton_}) b->setVisible(!functions_.empty());
}

void OwnFunctionsWidget::say(const QString &text, bool error) {
    result_->setText(mark(error ? StatusKind::Error : StatusKind::Success, text));
    result_->show();
}

void OwnFunctionsWidget::selectRow(int row) { list_->setCurrentRow(row); }

void OwnFunctionsWidget::editSelected() {
    const int row = list_->currentRow();
    if (row < 0 || size_t(row) >= functions_.size()) return;
    const FunctionTarget t = functions_[size_t(row)].uses.front().target();
    if (editRequested) editRequested(t);
}

void OwnFunctionsWidget::deleteSelected(bool confirmed) {
    const int row = list_->currentRow();
    if (row < 0 || size_t(row) >= functions_.size() || !ctx_.settings) return;
    const OwnFunction fn = functions_[size_t(row)];
    if (!confirmed) {
        const QString q = T("Delete this function? %1 will inherit again.").arg(qs8(usesWords(fn.uses, strings())));
        if (spn::ui::qt::plainBox(QMessageBox::Question, this, T("Delete function"), q) != QMessageBox::Yes) return;
    }
    const OwnDeleteResult r = deleteOwnFunction(ctx_.settings, section_, fn, strings());
    if (!r.ok) return;
    ctx_.saveSoon();
    rebuild();
    say(qs8(r.words), false);
    if (changed) changed();
}

void OwnFunctionsWidget::saveSelectedAsJoystickMode(const QString &name) {
    const int row = list_->currentRow();
    if (row < 0 || size_t(row) >= functions_.size() || !ctx_.settings || !ctx_.modes) return;
    SettingsModel *m = ctx_.settings;
    const size_t section = section_;
    const std::string lines = joystickModeLines(functions_[size_t(row)], [m, section](const std::string &key) {
        std::string held;
        motion_edit_detail::ownText(*m, section, key, &held);
        return held;
    }, *m, section);
    const SaveAsResult r = saveAsJoystickMode(ctx_.modes, name.toUtf8().toStdString(), lines);
    if (!r.ok) {
        say(T("Not saved: ") + qs8(r.error), true);
        return;
    }
    QString words = T("Saved as the joystick mode %1. This profile keeps its own function.").arg(qs8(r.id));
    if (!r.warnings.empty()) words += QLatin1Char(' ') + qs8(r.warnings.front());
    say(words, false);
}

void OwnFunctionsWidget::saveSelectedAsButtonMode(const QString &name) {
    const int row = list_->currentRow();
    if (row < 0 || size_t(row) >= functions_.size() || !ctx_.buttonModes) return;
    const OwnFunction &fn = functions_[size_t(row)];
    if (!fn.onButton()) {
        say(T("Only a function of a button can be a button mode."), true);
        return;
    }
    const SaveAsResult r = saveAsButtonMode(ctx_.buttonModes, name.toUtf8().toStdString(), fn.text);
    if (!r.ok) {
        say(T("Not saved: ") + qs8(r.error), true);
        return;
    }
    say(T("Saved as the button mode %1. This profile keeps its own function.").arg(qs8(r.id)), false);
}
