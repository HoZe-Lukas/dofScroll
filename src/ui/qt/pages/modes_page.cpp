// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "pages/modes_page.hpp"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QFrame>
#include <QGridLayout>
#include <QPainter>
#include <QScrollArea>
#include <QStyledItemDelegate>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

#include <QStackedWidget>

#include "described_item_delegate.hpp"
#include "pages/mode_editor_widget.hpp"
#include "ui/model/mode_info_model.hpp"
#include "motion_view.hpp"
#include "ui/model/motion_summary_model.hpp"
#include "theme.hpp"
#include "ui/model/design_tokens.hpp"
#include "platform/fs.hpp"
#include "qt_action_runner.hpp"
#include "ui_i18n.hpp"
#include "seed.hpp"
#include "ui/model/examples_model.hpp"
#include "ui/model/ui_terms.hpp"

using spn::ui::ModeCatalog;
using Origin = ModeCatalog::Origin;

namespace {
enum { kRoleId = Qt::UserRole };

QString originText(Origin o) {
    QtStrings strings;
    return qs8(spn::ui::modeOriginText(o, strings));
}

// Item roles of a row of the list (kRoleDescription and kRoleOrigin come from described_item_delegate.hpp).
enum { kRoleChips = Qt::UserRole + 3, kRoleMuted = Qt::UserRole + 4 };

// One row of the list: the name (bold) with the origin badge at the right end, one short description line (muted,
// cut to the width) and the six motion chips below, wrapped to the width of the list. All lengths follow the font
// (metrics of the item's font), so a larger font gives larger chips and rows, never a horizontal scroll bar.
// A muted chip (motion off or not used) has a dashed outline and keeps its word ("Off", "Unused"): never colour alone.
class ModeRowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QFontMetrics fm(opt.font);
        const int pad = spn::ui::tokens::kSpaceSm;
        const motionview::ChipLayout chips = motionview::layoutChips(fm, chipsOf(index), textWidth(opt));
        int h = fm.height() + 2 * pad;
        if (!index.data(kRoleDescription).toString().isEmpty()) h += fm.height();
        h += chips.height + (chips.height ? motionview::chipGap(fm) : 0);
        return QSize(opt.rect.width(), std::max(spn::ui::tokens::kRowHeight, h));
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QString title = opt.text;
        opt.text.clear();
        const QWidget *w = opt.widget;
        QStyle *style = w ? w->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, w);  // background, selection, icon
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, w);
        const int pad = spn::ui::tokens::kSpaceSm;
        const int width = std::min(textRect.width(), textWidth(opt));
        const QFontMetrics fm(opt.font);
        const bool selected = opt.state & QStyle::State_Selected;
        const QColor nameColor = selected ? selectedTextColor(false) : opt.palette.color(QPalette::Text);
        const QColor mutedColor = selected ? selectedTextColor(true) : opt.palette.color(QPalette::PlaceholderText);
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setFont(opt.font);
        int y = textRect.top() + pad;
        // origin badge at the right end of the name line (cut to a third of the row)
        const QString origin = fm.elidedText(index.data(kRoleOrigin).toString(), Qt::ElideRight, width / 3);
        int badgeWidth = 0;
        if (!origin.isEmpty()) {
            badgeWidth = fm.horizontalAdvance(origin) + fm.height();
            const QRectF badge(textRect.left() + width - badgeWidth, y, badgeWidth, fm.height());
            painter->setPen(QPen(mutedColor, 1));
            painter->drawRoundedRect(badge.adjusted(0.5, 0.5, -0.5, -0.5), fm.height() / 3.0, fm.height() / 3.0);
            painter->drawText(badge, Qt::AlignCenter, origin);
        }
        QFont bold = opt.font;
        bold.setBold(true);
        painter->setFont(bold);
        painter->setPen(nameColor);
        const int nameWidth = width - (badgeWidth ? badgeWidth + motionview::chipGap(fm) : 0);
        painter->drawText(QRect(textRect.left(), y, nameWidth, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(bold).elidedText(title, Qt::ElideRight, nameWidth));
        painter->setFont(opt.font);
        y += fm.height();
        const QString desc = index.data(kRoleDescription).toString();
        if (!desc.isEmpty()) {
            painter->setPen(mutedColor);
            painter->drawText(QRect(textRect.left(), y, width, fm.height()), Qt::AlignLeft | Qt::AlignVCenter,
                              fm.elidedText(desc, Qt::ElideRight, width));
            y += fm.height();
        }
        y += motionview::chipGap(fm);
        const QList<motionview::Chip> chips = chipsOf(index);
        motionview::paintChips(painter, fm, chips, motionview::layoutChips(fm, chips, width), QPoint(textRect.left(), y), nameColor, mutedColor);
        painter->restore();
    }

private:
    // the six motion chips of the row (text in kRoleChips, '1' in kRoleMuted for a muted one)
    static QList<motionview::Chip> chipsOf(const QModelIndex &index) {
        QList<motionview::Chip> out;
        const QStringList texts = index.data(kRoleChips).toStringList();
        const QString mask = index.data(kRoleMuted).toString();
        for (int i = 0; i < texts.size(); ++i) {
            motionview::Chip c;
            // the list shows the symbol only (the words are in the detail view and the tooltip, the position
            // says which motion it is); a word would be cut off in a narrow row
            const int space = texts[i].indexOf(QLatin1Char(' '));
            c.text = space > 0 ? texts[i].left(space) : texts[i];
            c.muted = i < mask.size() && mask[i] == QLatin1Char('1');
            out.append(c);
        }
        return out;
    }
    static int textWidth(const QStyleOptionViewItem &opt) {
        const QWidget *w = opt.widget;
        int width = w ? w->width() : opt.rect.width();
        if (const auto *view = qobject_cast<const QAbstractItemView *>(w)) width = view->viewport()->width();
        return std::max(120, width - 2 * spn::ui::tokens::kSpaceMd - (opt.icon.isNull() ? 0 : 28));
    }
};
}  // namespace

QWidget *makeModesPage(const UiContext &ctx) { return new ModesPage(ctx); }

ModesPage::ModesPage(const UiContext &ctx, QWidget *parent)
    : QWidget(parent),
      ctx_(ctx),
      ownCatalog_(ctx.modes ? nullptr : new spn::ui::ModeCatalog(ctx.modesDir)),
      catalog_(ctx.modes ? *ctx.modes : *ownCatalog_) {
    auto *root = new QVBoxLayout(this);
    auto *title = new QLabel(QStringLiteral("<b>") + T(spn::ui::terms::kJoystickModes) + QStringLiteral("</b>"), this);
    root->addWidget(title);
    auto *intro = new QLabel(T("Each joystick mode says what the six motions of the cap and the buttons do. Pick one for a "
                               "profile on the Profiles page."), this);
    intro->setWordWrap(true);
    intro->setForegroundRole(QPalette::PlaceholderText);
    root->addWidget(intro);

    // Permanent hint: a mode file can press any key (also shortcut chords).
    auto *hint = new QLabel(T("Joystick mode files can press any key. Only use joystick mode files from trusted sources."), this);
    hint->setWordWrap(true);
    hint->setTextFormat(Qt::PlainText);
    root->addWidget(hint);

    auto *row = new QHBoxLayout;
    auto *split = new QSplitter(Qt::Horizontal, this);
    auto *left = new QWidget(split);
    auto *leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    list_ = new QListWidget(left);
    list_->setAccessibleName(T(spn::ui::terms::kJoystickModes));
    list_->setToolTip(T("All joystick modes: the built-in ones and the files in the joystick modes folder"));
    list_->setItemDelegate(new ModeRowDelegate(list_));  // name, origin badge, short description, six motion chips
    list_->setWordWrap(true);
    list_->setResizeMode(QListView::Adjust);
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // wide enough for the name, the badge and the six symbol chips in one row (the width follows the font size)
    list_->setMinimumWidth(list_->fontMetrics().horizontalAdvance(QLatin1Char('x')) * 41);
    leftLayout->addWidget(list_, 1);
    auto *legend = new QLabel(T("Symbols in this order: vertical push, vertical tilt, horizontal push, horizontal tilt, twist, "
                                "lift / press. \u2195 scroll, \u2194 scroll sideways, \u2295 zoom, \u2328 keys, \u21E5 one tap per "
                                "push, \u2298 off, \u2013 not used (dashed)."), left);
    legend->setWordWrap(true);
    legend->setTextFormat(Qt::PlainText);
    legend->setForegroundRole(QPalette::PlaceholderText);
    leftLayout->addWidget(legend);
    right_ = new QStackedWidget(split);
    buildDetail();
    right_->addWidget(detail_);
    editor_ = new ModeEditorWidget(ctx_, right_);
    right_->addWidget(editor_);
    connect(editor_, &ModeEditorWidget::closeRequested, this, [this] { right_->setCurrentWidget(detail_); });
    connect(editor_, &ModeEditorWidget::saved, this, [this](const QString &id) {
        catalog_.refresh();
        populate(id);
        right_->setCurrentWidget(editor_);  // stay in the editor after saving
    });
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);
    split->setChildrenCollapsible(false);
    connect(split, &QSplitter::splitterMoved, this, [this] { list_->doItemsLayout(); });
    row->addWidget(split, 1);

    auto *buttons = new QVBoxLayout;
    auto mk = [&](const QString &text, const QString &tip) {
        auto *b = new QPushButton(text, this);
        b->setToolTip(tip);
        buttons->addWidget(b);
        return b;
    };
    newBtn_ = mk(T("New joystick mode"), T("Create a joystick mode file from a commented template and open it in your editor"));
    exampleBtn_ = mk(T("New from example..."), T("Copy one of the examples that ship with dofscroll into your folder"));
    dupBtn_ = mk(T("Duplicate"), T("Copy the selected joystick mode under a new name"));
    importBtn_ = mk(T("Import file..."), T("Add a joystick mode file from somewhere else (you see its warnings first)"));
    editBtn_ = mk(T("Edit"), T("Edit the joystick mode here: name, channels, buttons and overrides"));
    delBtn_ = mk(T("Delete"), T("Delete the joystick mode file (your own files only)"));
    renameBtn_ = mk(T("Rename..."), T("Give the joystick mode file a new name and id (profiles that use it are listed first)"));
    exportBtn_ = mk(T("Export..."), T("Save a copy of the joystick mode file somewhere else"));
    folderBtn_ = mk(T("Open joystick modes folder"), T("Show the folder with the joystick mode files"));
    buttons->addStretch(1);
    row->addLayout(buttons);
    root->addLayout(row, 1);

    // Tab order follows the creation order: list, details, buttons.
    setTabOrder(list_, detail_);
    setTabOrder(detail_, newBtn_);
    setTabOrder(newBtn_, exampleBtn_);
    setTabOrder(exampleBtn_, dupBtn_);
    setTabOrder(dupBtn_, importBtn_);
    setTabOrder(importBtn_, editBtn_);
    setTabOrder(editBtn_, delBtn_);
    setTabOrder(delBtn_, renameBtn_);
    setTabOrder(renameBtn_, exportBtn_);
    setTabOrder(exportBtn_, folderBtn_);
    setAcceptDrops(true);  // drop .conf files to import them

    connect(list_, &QListWidget::currentItemChanged, this, [this] { onCurrentChanged(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this] { onEdit(); });
    connect(newBtn_, &QPushButton::clicked, this, [this] { onNew(); });
    connect(exampleBtn_, &QPushButton::clicked, this, [this] { onNewFromExample(); });
    connect(dupBtn_, &QPushButton::clicked, this, [this] { onDuplicate(); });
    connect(importBtn_, &QPushButton::clicked, this, [this] { onImport(); });
    connect(editBtn_, &QPushButton::clicked, this, [this] { onEdit(); });
    connect(delBtn_, &QPushButton::clicked, this, [this] { onDelete(); });
    connect(renameBtn_, &QPushButton::clicked, this, [this] { onRename(); });
    connect(exportBtn_, &QPushButton::clicked, this, [this] { onExport(); });
    connect(folderBtn_, &QPushButton::clicked, this, [this] { onOpenFolder(); });

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &ModesPage::refreshIfChanged);

    populate(QString());
}

int ModesPage::modeCount() const { return list_->count(); }

void ModesPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    refreshIfChanged();  // catch up with changes made while hidden
    timer_->start();
}

void ModesPage::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    timer_->stop();
}

void ModesPage::refreshIfChanged() {
    const ModeCatalog::ModeEntry *cur = current();
    const QString keep = cur ? QString::fromStdString(cur->id) : QString();
    if (catalog_.refreshIfChanged()) populate(keep);
}

QString ModesPage::currentId() const {
    const QListWidgetItem *it = list_->currentItem();
    return it ? it->data(kRoleId).toString() : QString();
}

void ModesPage::selectRow(const QString &id) {
    const QSignalBlocker block(list_);
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kRoleId).toString() == id) {
            list_->setCurrentRow(i);
            return;
        }
}

bool ModesPage::pendingEdits() const { return editor_->pending(); }

// The editor writes by itself; before it is replaced or closed the last change is written now.
void ModesPage::flushEditor() { editor_->flush(); }

void ModesPage::onCurrentChanged() {
    const QString target = currentId();
    if (right_->currentWidget() == editor_ && target != shownId_) flushEditor();
    shownId_ = target;
    right_->setCurrentWidget(detail_);  // the editor belongs to the mode it was opened for
    updateDetail();
}

const ModeCatalog::ModeEntry *ModesPage::current() const {
    const QListWidgetItem *it = list_->currentItem();
    return it ? catalog_.find(it->data(kRoleId).toString().toStdString()) : nullptr;
}

void ModesPage::populate(const QString &selectId) {
    const QSignalBlocker block(list_);
    list_->clear();
    int select = 0, n = 0;
    for (const ModeCatalog::ModeEntry &m : catalog_.modes()) {
        const QString id = QString::fromStdString(m.id), label = QString::fromStdString(m.label);
        auto *it = new QListWidgetItem(label == id ? id : label + QStringLiteral(" (") + id + QStringLiteral(")"));
        const QString desc = qs8(m.description.empty() ? spn::ui::noDescriptionText(QtStrings()) : m.description);
        QStringList chips;
        QString mask, motionText;
        if (const spn::ModeDef *def = catalog_.modeSet().find(m.id)) {
            const std::vector<spn::ui::MotionRow> rows = spn::ui::motionSummary(*def, QtStrings());
            for (const std::string &c : spn::ui::motionChipTexts(rows)) chips << qs8(c);
            for (const spn::ui::MotionRow &r : rows) mask += r.muted() ? QLatin1Char('1') : QLatin1Char('0');
            motionText = qs8(spn::ui::motionSummaryText(rows, "; "));
        }
        it->setData(kRoleId, id);
        it->setData(kRoleOrigin, originText(m.origin));
        it->setData(kRoleDescription, desc);
        it->setData(kRoleChips, chips);
        it->setData(kRoleMuted, mask);
        it->setData(Qt::AccessibleTextRole, it->text() + QStringLiteral(", ") + originText(m.origin));
        it->setData(Qt::AccessibleDescriptionRole, desc + QStringLiteral(". ") + motionText);
        if (!m.warnings.empty()) it->setIcon(style()->standardIcon(QStyle::SP_MessageBoxWarning));
        QString motionLines = motionText;
        motionLines.replace(QStringLiteral("; "), QStringLiteral("\n"));
        it->setToolTip(desc + QLatin1Char('\n') + originText(m.origin) + QStringLiteral("\n\n") + motionLines);
        list_->addItem(it);
        if (id == selectId) select = n;
        ++n;
    }
    if (n) list_->setCurrentRow(select);
    // The editor belongs to one mode: a clean one for another mode (the list moved on) makes room for the details.
    if (right_->currentWidget() == editor_ && qs8(editor_->editor().form().id) != currentId()) right_->setCurrentWidget(detail_);
    if (right_->currentWidget() != editor_) shownId_ = currentId();
    updateDetail();
}

// The detail panel is built once (buildDetail) and filled by updateDetail for the selected joystick mode. Mode
// files are foreign content: every label shows plain text. The six motions and the buttons use the shared
// components of motion_view.hpp, the same look as the profile page and the Overview.
void ModesPage::buildDetail() {
    detail_ = new QScrollArea(right_);
    detail_->setWidgetResizable(true);
    detail_->setFrameShape(QFrame::NoFrame);
    detail_->setFocusPolicy(Qt::StrongFocus);  // scrolls with the keyboard
    detail_->setAccessibleName(T("Details of the selected joystick mode"));
    auto *content = new QWidget;
    auto *v = new QVBoxLayout(content);
    auto label = [content](bool muted) {
        auto *l = new QLabel(content);
        l->setWordWrap(true);
        l->setTextFormat(Qt::PlainText);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        if (muted) l->setForegroundRole(QPalette::PlaceholderText);
        return l;
    };
    auto heading = [&label](const QString &text) {
        QLabel *l = label(false);
        QFont f = l->font();
        f.setBold(true);
        l->setFont(f);
        l->setText(text);
        return l;
    };
    // name and origin badge
    auto *head = new QHBoxLayout;
    detailTitle_ = label(false);
    QFont tf = detailTitle_->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() * 1.25);
    detailTitle_->setFont(tf);
    head->addWidget(detailTitle_, 1);
    detailBadge_ = label(true);
    detailBadge_->setFrameShape(QFrame::StyledPanel);
    detailBadge_->setMargin(2);
    v->addLayout(head);
    detailBadge_->setFrameShape(QFrame::NoFrame);  // a line of text under the title, not a boxed block at the side
    v->addWidget(detailBadge_);
    detailDesc_ = label(false);
    v->addWidget(detailDesc_);
    detailMeta_ = label(true);
    v->addWidget(detailMeta_);

    v->addWidget(heading(T("What each motion does")));
    grid_ = new motionview::MotionGrid(content);
    v->addWidget(grid_);

    v->addWidget(heading(T("Buttons")));
    buttonChips_ = new motionview::ButtonChips(content);
    v->addWidget(buttonChips_);
    usedByLabel_ = label(false);
    v->addWidget(usedByLabel_);
    warningsLabel_ = label(false);
    v->addWidget(warningsLabel_);
    v->addStretch(1);
    detail_->setWidget(content);
}

QStringList ModesPage::rowChips(const QString &id) const {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kRoleId).toString() == id) return list_->item(i)->data(kRoleChips).toStringList();
    return QStringList();
}

QStringList ModesPage::motionCardTexts() const { return grid_->cardTexts(); }

void ModesPage::updateDetail() {
    const ModeCatalog::ModeEntry *m = current();
    const spn::ModeDef *def = m ? catalog_.modeSet().find(m->id) : nullptr;
    QtStrings strings;
    detailTitle_->setVisible(m != nullptr);
    detailBadge_->setVisible(m != nullptr);
    detailDesc_->setVisible(m != nullptr);
    detailMeta_->setVisible(m != nullptr);
    grid_->setVisible(def != nullptr);
    buttonChips_->setVisible(def != nullptr);
    usedByLabel_->setVisible(m != nullptr);
    if (m) {
        detailTitle_->setText(QString::fromStdString(m->label));
        detailBadge_->setText(originText(m->origin));
        detailDesc_->setText(qs8(m->description.empty() ? spn::ui::noDescriptionText(strings) : m->description));
        QString meta = T("Id: %1").arg(QString::fromStdString(m->id));
        if (!m->path.empty()) meta += QStringLiteral("    ") + T("File: %1").arg(QString::fromStdString(m->path));
        detailMeta_->setText(meta);
    }
    if (def) {
        grid_->setMotions(spn::ui::motionSummary(*def, strings));
        buttonChips_->setButtons(spn::ui::modeButtonChips(*def, strings), T("No buttons bound"));
    }
    if (m) {
        const QStringList uses = usedByLines(QString::fromStdString(m->id));
        usedByLabel_->setText(uses.isEmpty() ? T("Used by: no profile") : T("Used by:") + QLatin1Char('\n') + uses.join(QLatin1Char('\n')));
    }
    QString warn;
    if (m && !m->warnings.empty()) {
        warn += T("Warnings:") + QLatin1Char('\n');
        for (const std::string &w : m->warnings) warn += QStringLiteral(" - ") + QString::fromStdString(w) + QLatin1Char('\n');
    }
    if (!catalog_.generalWarnings().empty()) {
        if (!warn.isEmpty()) warn += QLatin1Char('\n');
        warn += T("Warnings for the joystick modes folder:") + QLatin1Char('\n');
        for (const std::string &w : catalog_.generalWarnings()) warn += QStringLiteral(" - ") + QString::fromStdString(w) + QLatin1Char('\n');
    }
    warningsLabel_->setText(warn.trimmed());
    warningsLabel_->setVisible(!warn.isEmpty());
    dupBtn_->setEnabled(m != nullptr);
    editBtn_->setEnabled(m != nullptr);
    delBtn_->setEnabled(m && m->origin != Origin::BuiltIn);
    renameBtn_->setEnabled(m && m->origin == Origin::File);
    exportBtn_->setEnabled(m != nullptr);
}

QStringList ModesPage::usedByLines(const QString &id) const {
    QStringList out;
    if (!ctx_.settings) return out;
    for (const ModeCatalog::ModeUse &u : ModeCatalog::usedBy(ctx_.settings->profiles(), id.toStdString())) {
        QString how;
        if (u.selects) how = T("selected as the joystick mode");
        if (!u.buttons.empty()) {
            QStringList nums;
            for (int b : u.buttons) nums << QString::number(b);
            if (!how.isEmpty()) how += QStringLiteral(", ");
            how += T("switched to by button %1").arg(nums.join(QStringLiteral(", ")));
        }
        out << QString::fromStdString(ModeCatalog::useTitle(u)) + QStringLiteral(": ") + how;
    }
    return out;
}

void ModesPage::showError(const QString &what, const std::string &detail) {
    QMessageBox::warning(this, T(spn::ui::terms::kJoystickModes), what + QStringLiteral("\n\n") + QString::fromStdString(detail));
}

bool ModesPage::openInEditor(const std::string &path) {
    std::string err;
    if (ctx_.runner && ctx_.runner->openPath(path, &err)) return true;
    showError(T("Could not open the file."), err);
    return false;
}

QString ModesPage::askName(const QString &title, const QString &label, const QString &initial, bool *ok) {
    return QInputDialog::getText(this, title, label, QLineEdit::Normal, initial, ok).trimmed();
}

void ModesPage::onNew() {
    bool ok = false;
    const QString name = askName(T("New joystick mode"), T("Name of the new joystick mode:"), QString(), &ok);
    if (!ok) return;
    flushEditor();
    const ModeCatalog::Result r = catalog_.createNew(name.toStdString());
    if (!r.ok) return showError(T("Could not create the joystick mode."), r.error);
    populate(QString::fromStdString(r.id));
    openInEditor(r.path);
}

void ModesPage::onDuplicate() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m) return;
    // Copies: the timer may rebuild the catalog while the dialog runs, which invalidates "m".
    const std::string id = m->id;
    const QString label = QString::fromStdString(m->label);
    bool ok = false;
    const QString name = askName(T("Duplicate"), T("Name of the copy:"), T("%1 (copy)").arg(label), &ok);
    if (!ok) return;
    flushEditor();
    const ModeCatalog::Result r = catalog_.duplicate(id, name.toStdString());
    if (!r.ok) return showError(T("Could not duplicate the joystick mode."), r.error);
    populate(QString::fromStdString(r.id));
}

void ModesPage::onImport() {
    const QString file = QFileDialog::getOpenFileName(this, T("Import joystick mode file"), QDir::homePath(),
                                                      T("Joystick mode files (*.conf);;All files (*)"));
    if (file.isEmpty()) return;
    importPath(file);
}

bool ModesPage::importPath(const QString &file, bool interactive) {
    lastError_.clear();
    const std::string path = file.toStdString();
    std::string name;  // empty: the id comes from the file name
    ModeCatalog::Result chk = catalog_.inspectImport(path, name);
    if (!chk.ok && interactive && chk.error.find("already exists") != std::string::npos) {
        // The id is taken: let the user pick another name.
        bool ok = false;
        const QString n = askName(T("Import joystick mode file"),
                                  T("A joystick mode with this id already exists. Name for the imported joystick mode:"),
                                  QFileInfo(file).completeBaseName(), &ok);
        if (!ok) return false;
        name = n.toStdString();
        chk = catalog_.inspectImport(path, name);
    }
    if (!chk.ok) {
        lastError_ = QString::fromStdString(chk.error);
        if (interactive) showError(T("This file cannot be imported."), chk.error);
        return false;
    }

    // Show the warnings and the permanent safety note before anything is kept.
    if (interactive) {
        QString text = T("Import the joystick mode \"%1\"?").arg(QString::fromStdString(chk.id));
        if (!chk.warnings.empty()) {
            text += QStringLiteral("\n\n") + T("Warnings:");
            for (const std::string &w : chk.warnings) text += QStringLiteral("\n - ") + QString::fromStdString(w);
        } else {
            text += QStringLiteral("\n\n") + T("The file has no loader warnings.");
        }
        QMessageBox box(QMessageBox::Warning, T("Import joystick mode file"), text, QMessageBox::NoButton, this);
        box.setInformativeText(QString::fromStdString(ModeCatalog::importSafetyNote(QtStrings())));
        QPushButton *yes = box.addButton(T("Import"), QMessageBox::AcceptRole);
        QPushButton *no = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        box.exec();
        if (box.clickedButton() != yes) return false;
    }

    flushEditor();
    const ModeCatalog::Result r = catalog_.importInspected(chk);  // what the user saw, not a second read
    if (!r.ok) {
        lastError_ = QString::fromStdString(r.error);
        if (interactive) showError(T("Could not import the joystick mode."), r.error);
        return false;
    }
    populate(QString::fromStdString(r.id));
    return true;
}

void ModesPage::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

// Dropped files are only read as mode files (same checks and confirmation as Import); nothing is run.
void ModesPage::dropEvent(QDropEvent *e) {
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile() && u.toLocalFile().endsWith(QStringLiteral(".conf"), Qt::CaseInsensitive))
            importPath(u.toLocalFile());
    e->acceptProposedAction();
}

bool ModesPage::selectMode(const QString &id) {
    for (int i = 0; i < list_->count(); ++i)
        if (list_->item(i)->data(kRoleId).toString() == id) {
            list_->setCurrentRow(i);
            return true;
        }
    return false;
}

bool ModesPage::openEditor() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m || m->path.empty()) return false;
    const std::string id = m->id;
    // Opening reloads the file: the edits of the editor are written first. A write refreshes the
    // catalog, so the entry and the definition are looked up again afterwards and copied before the
    // editor (which may show a message box) runs.
    flushEditor();
    const ModeCatalog::ModeEntry *e = catalog_.find(id);
    const spn::ModeDef *def = catalog_.modeSet().find(id);
    if (!e || e->path.empty() || !def) return false;
    const std::string path = e->path;
    const spn::ModeDef defCopy = *def;
    if (!editor_->open(path, defCopy)) return false;
    shownId_ = QString::fromStdString(id);
    right_->setCurrentWidget(editor_);
    return true;
}

void ModesPage::onEdit() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m) return;
    const std::string sid = m->id;  // copies: "m" does not survive a catalog refresh during a dialog
    if (!m->path.empty()) {
        openEditor();
        return;
    }
    // A built-in mode has no file: offer an editable copy with the same id that replaces it.
    const QString id = QString::fromStdString(sid);
    const QMessageBox::StandardButton a = QMessageBox::question(
        this, T("Edit"),
        T("The built-in joystick mode \"%1\" is not a file. Create an editable file with the same id? It replaces the "
          "built-in joystick mode until you delete the file. To keep the original, use Duplicate instead.")
            .arg(id),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (a != QMessageBox::Yes) return;
    const ModeCatalog::Result r = catalog_.makeEditableCopy(sid);
    if (!r.ok) return showError(T("Could not create the file."), r.error);
    populate(id);
    openEditor();
}

void ModesPage::onDelete() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m || m->origin == Origin::BuiltIn) return;
    // Copies: the timer may rebuild the catalog while the dialog runs, which invalidates "m".
    const std::string sid = m->id;
    const QString id = QString::fromStdString(sid);
    const bool overridden = m->origin == Origin::BuiltInOverridden;
    QString text = T("Delete the joystick mode file \"%1\"?").arg(QString::fromStdString(m->path));
    if (overridden)
        text += QStringLiteral("\n\n") + T("This file replaces the built-in joystick mode \"%1\"; the built-in one comes back.").arg(id);
    text += QStringLiteral("\n\n") + T("This cannot be undone.");
    QMessageBox box(QMessageBox::Question, T("Delete"), text, QMessageBox::NoButton, this);
    QPushButton *yes = box.addButton(T("Delete"), QMessageBox::DestructiveRole);
    QPushButton *no = box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(no);
    box.setEscapeButton(no);
    box.exec();
    if (box.clickedButton() != yes) return;
    // Edits of the joystick mode being deleted are moot (the user confirmed); edits of another one are written.
    if (qs8(editor_->editor().form().id) != id) flushEditor();
    else right_->setCurrentWidget(detail_);
    const ModeCatalog::Result r = catalog_.remove(sid);
    if (!r.ok) return showError(T("Could not delete the joystick mode."), r.error);
    right_->setCurrentWidget(detail_);
    populate(id);  // an overridden built-in is selected again, else the first entry
}

bool ModesPage::exportModeTo(const QString &id, const QString &dest) {
    const ModeCatalog::Result r = catalog_.exportTo(id.toStdString(), dest.toStdString());
    lastError_ = QString::fromStdString(r.error);
    return r.ok;
}

void ModesPage::onExport() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m) return;
    const QString id = QString::fromStdString(m->id);
    // The dialog asks before replacing; the model never overwrites, so an existing file is removed first.
    const QString file = QFileDialog::getSaveFileName(this, T("Export joystick mode file"),
                                                      QDir::homePath() + QLatin1Char('/') + id + QStringLiteral(".conf"),
                                                      T("Joystick mode files (*.conf);;All files (*)"));
    if (file.isEmpty()) return;
    if (QFileInfo::exists(file) && !QFile::remove(file)) return showError(T("Could not export the joystick mode."), file.toStdString());
    if (!exportModeTo(id, file)) showError(T("Could not export the joystick mode."), lastError_.toStdString());
}

bool ModesPage::renameMode(const QString &id, const QString &newName) {
    const ModeCatalog::Result r = catalog_.rename(id.toStdString(), newName.toStdString());
    lastError_ = QString::fromStdString(r.error);
    if (!r.ok) return false;
    populate(QString::fromStdString(r.id));
    return true;
}

void ModesPage::onRename() {
    const ModeCatalog::ModeEntry *m = current();
    if (!m || m->origin != Origin::File) return;
    const QString id = QString::fromStdString(m->id);
    bool ok = false;
    const QString name = askName(T("Rename"), T("New name of the joystick mode:"), QString::fromStdString(m->label), &ok);
    if (!ok) return;
    const std::string nid = ModeCatalog::deriveId(ModeCatalog::cleanLabel(name.toStdString()));
    const QStringList uses = nid == m->id ? QStringList() : usedByLines(id);
    if (!uses.isEmpty()) {
        const QString text = T("The id of the joystick mode changes from \"%1\" to \"%2\". These profiles still refer to the "
                               "old id and will fall back to the Global joystick mode until you change them:")
                                 .arg(id, QString::fromStdString(nid)) +
                             QStringLiteral("\n\n - ") + uses.join(QStringLiteral("\n - "));
        QMessageBox box(QMessageBox::Warning, T("Rename"), text, QMessageBox::NoButton, this);
        QPushButton *yes = box.addButton(T("Rename"), QMessageBox::AcceptRole);
        QPushButton *no = box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(no);
        box.setEscapeButton(no);
        box.exec();
        if (box.clickedButton() != yes) return;
    }
    if (!renameMode(id, name)) showError(T("Could not rename the joystick mode."), lastError_.toStdString());
}

void ModesPage::onOpenFolder() {
    std::string err;
    if (!spn::fs::mkdirsPrivate(ctx_.modesDir, &err)) return showError(T("Could not create the joystick modes folder."), err);
    openInEditor(ctx_.modesDir);
}

// "New from example...": the examples that ship with dofscroll and are not in the folder yet; the chosen
// one is copied (checked like an import) and selected.
void ModesPage::onNewFromExample() {
    const std::vector<spn::ui::Example> ex = spn::ui::availableModeExamples(catalog_, spn::examplesDir());
    if (ex.empty()) {
        QMessageBox::information(this, T("New from example"),
                                 T("All examples are already in your joystick modes folder, or none ships with this installation."));
        return;
    }
    QtStrings strings;
    QStringList items;
    for (const spn::ui::Example &e : ex)
        items << qs8(e.label) + QStringLiteral("  -  ") + qs8(spn::ui::exampleOriginText(strings)) +
                     (e.description.empty() ? QString() : QStringLiteral("  -  ") + qs8(e.description));
    bool ok = false;
    const QString pick = QInputDialog::getItem(this, T("New from example"), T("Copy this example into your joystick modes folder:"),
                                               items, 0, false, &ok);
    if (!ok) return;
    const int idx = items.indexOf(pick);
    if (idx < 0) return;
    if (!installExample(QString::fromStdString(ex[size_t(idx)].id))) showError(T("Could not add the example."), lastError_.toStdString());
}

// Copies the example with this id (no dialog); false + lastError() if it is not offered or cannot be copied.
bool ModesPage::installExample(const QString &id) {
    lastError_.clear();
    for (const spn::ui::Example &e : spn::ui::availableModeExamples(catalog_, spn::examplesDir())) {
        if (qs8(e.id) != id) continue;
        flushEditor();
        const spn::ui::ExampleResult r = spn::ui::installModeExample(catalog_, e);
        if (!r.ok) {
            lastError_ = qs8(r.error);
            return false;
        }
        populate(qs8(r.id));
        return true;
    }
    lastError_ = T("This example is not offered.");
    return false;
}

QStringList ModesPage::exampleIds() const {
    QStringList out;
    for (const spn::ui::Example &e : spn::ui::availableModeExamples(catalog_, spn::examplesDir())) out << qs8(e.id);
    return out;
}

QString ModesPage::originOf(const QString &id) const {
    const ModeCatalog::ModeEntry *e = catalog_.find(id.toStdString());
    return e ? originText(e->origin) : QString();
}
