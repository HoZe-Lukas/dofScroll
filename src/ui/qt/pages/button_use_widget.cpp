// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/button_use_widget.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <set>

#include "pages/motion_editor.hpp"
#include "ui/model/binding_editor_model.hpp"
#include "ui/model/button_chip_model.hpp"
#include "ui/model/device_layout_model.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

ButtonUseWidget::ButtonUseWidget(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx) {
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    picture_ = new MotionPicture(this);
    picture_->setAccessibleName(T("Picture of the device buttons"));
    col->addWidget(picture_);
    selected_ = new QLabel(this);
    selected_->setWordWrap(true);
    selected_->setTextFormat(Qt::PlainText);
    col->addWidget(selected_);
    auto *row = new QHBoxLayout;
    use_ = new QPushButton(this);
    unbind_ = new QPushButton(T("Unbind"), this);
    unbind_->setToolTip(T("Remove the binding of this button in the Global profile"));
    row->addWidget(use_, 1);
    row->addWidget(unbind_);
    col->addLayout(row);
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    status_->setAccessibleName(T("Where this button mode is used"));
    col->addWidget(status_);
    connect(picture_, &MotionPicture::selected, this, [this] { updateButtons(); });
    connect(use_, &QPushButton::clicked, this, [this] {
        const QString sel = picture_->current();
        if (!ctx_.settings || id_.isEmpty() || !sel.startsWith(QLatin1String("b:"))) return;
        std::string err;
        if (!applyBinding(*ctx_.settings, 0, sel.mid(2).toInt(), Binding{BindingKind::ButtonMode, id_.toStdString()}, &err)) {
            status_->setText(mark(StatusKind::Error, qs8(err)));
            return;
        }
        ctx_.saveSoon();
        refresh();
    });
    connect(unbind_, &QPushButton::clicked, this, [this] {
        const QString sel = picture_->current();
        if (!ctx_.settings || !sel.startsWith(QLatin1String("b:"))) return;
        ctx_.settings->clearButton(0, sel.mid(2).toInt());
        ctx_.saveSoon();
        refresh();
    });
    refresh();
}

void ButtonUseWidget::setButtonMode(const QString &id, const QString &label) {
    id_ = id;
    label_ = label;
    refresh();
}

QString ButtonUseWidget::status() const { return status_->text(); }
QString ButtonUseWidget::selectedId() const { return picture_->current(); }

bool ButtonUseWidget::select(const QString &id) {
    for (const MotionPicture::Item &it : picture_->items())
        if (it.id == id) {
            picture_->setCurrent(id);
            return true;
        }
    return false;
}

void ButtonUseWidget::refresh() {
    if (!ctx_.settings) return;
    const IdentityStrings fallback;
    const StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    const std::vector<BindingRow> rows = bindingRows(*ctx_.settings, 0);
    const std::string device = ctx_.status ? ctx_.status->status().device : std::string();
    const int limit = reportedOrDefaultButtons(ctx_.settings->buttonLimit());
    const DeviceLayout layout = deviceLayout(device, limit);
    QList<MotionPicture::Item> items;
    std::set<int> have;
    QStringList uses;
    auto addButton = [&](int n, double ux, double uy) {
        have.insert(n);
        const BindingRow *row = nullptr;
        for (const BindingRow &r : rows)
            if (r.number == n) row = &r;
        const bool bound = row && row->text != "none" && !row->text.empty();
        const bool mine = bound && row->binding.kind == BindingKind::ButtonMode && !id_.isEmpty() && row->binding.arg == id_.toStdString();
        if (mine) uses << QString::number(n);
        const ButtonChip chip = buttonChip(n, bound ? row->text : std::string(), std::string(), strings);
        MotionPicture::Item it;
        it.id = QStringLiteral("b:") + QString::number(n);
        it.title = QString::number(n);
        it.text = mine ? T("This button mode") : qs8(chip.action);
        it.muted = !bound;
        it.ux = ux;
        it.uy = uy;
        it.tooltip = qs8(chip.tooltip) + QStringLiteral(", ") + T("Global profile");
        items << it;
    };
    for (const ButtonSlot &s : layout.buttons) addButton(s.number, s.x, s.y);
    for (const BindingRow &r : rows)
        if (!have.count(r.number) && !r.text.empty() && r.text != "none") addButton(r.number, 0.5, 1.0);
    picture_->setItems(items, layout.kind);
    if (id_.isEmpty()) status_->setText(T("Choose a button mode to see where it is used."));
    else if (uses.isEmpty()) status_->setText(T("Not used on a button of the Global profile."));
    else status_->setText(T("Used on buttons: %1 (Global profile)").arg(uses.join(QStringLiteral(", "))));
    updateButtons();
}

void ButtonUseWidget::updateButtons() {
    const QString sel = picture_->current();
    const bool isButton = sel.startsWith(QLatin1String("b:"));
    const QString n = isButton ? sel.mid(2) : QString();
    selected_->setText(isButton ? T("Button %1 in the Global profile: %2").arg(n).arg(picture_->items().isEmpty() ? QString() : [&] {
        for (const MotionPicture::Item &it : picture_->items())
            if (it.id == sel) return it.text;
        return QString();
    }()) : T("Choose a button in the picture."));
    use_->setText(id_.isEmpty() ? T("Use on this button") : T("Use \"%1\" on button %2").arg(label_, isButton ? n : QStringLiteral("...")));
    use_->setEnabled(isButton && !id_.isEmpty());
    unbind_->setEnabled(isButton);
}
