// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "overview_page.hpp"

#include <QApplication>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

#include "ui/model/design_tokens.hpp"
#include "ui/model/overview_model.hpp"
#include "ui_i18n.hpp"

namespace {

constexpr int kCardMinWidth = 260;  // cards wrap to the next row when a column would get narrower

QLabel *note(const QString &text = QString()) {
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    l->setForegroundRole(QPalette::PlaceholderText);
    return l;
}

}  // namespace

OverviewPage::OverviewPage(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx) {
    using namespace spn::ui::tokens;
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget;
    auto *col = new QVBoxLayout(content);
    col->setContentsMargins(kSpaceLg, kSpaceLg, kSpaceLg, kSpaceLg);
    col->setSpacing(kSpaceLg);
    scroll->setWidget(content);

    // ---- status ----
    auto *statusBox = new QGroupBox(T("Status"));
    auto *sl = new QVBoxLayout(statusBox);
    statusLine_ = new QLabel;
    statusLine_->setWordWrap(true);
    QFont bold = statusLine_->font();
    bold.setBold(true);
    statusLine_->setFont(bold);
    statusLine_->setAccessibleName(T("Daemon status"));
    infoLine_ = note();
    warningLine_ = new QLabel;
    warningLine_->setWordWrap(true);
    sl->addWidget(statusLine_);
    sl->addWidget(infoLine_);
    sl->addWidget(warningLine_);
    col->addWidget(statusBox);

    // ---- quick actions ----
    auto *actionBox = new QGroupBox(T("Quick actions"));
    auto *al = new QVBoxLayout(actionBox);
    auto *row = new QHBoxLayout;
    row->setSpacing(kSpaceSm);
    auto *addProfile = new QPushButton(T("Add a profile..."));
    addProfile->setToolTip(T("Set up a program or a device on the Profiles page"));
    connect(addProfile, &QPushButton::clicked, this, [this] { emit openPage(QStringLiteral("profiles")); });
    row->addWidget(addProfile);
    const std::vector<spn::ui::OverviewAction> actions = spn::ui::overviewActions();
    for (size_t i = 0; i < actions.size(); ++i) {
        auto *b = new QPushButton(T(actions[i].label.c_str()));
        connect(b, &QPushButton::clicked, this, [this, i] { runAction(static_cast<int>(i)); });
        row->addWidget(b);
    }
    row->addStretch(1);
    al->addLayout(row);
    actionMsg_ = note();
    actionMsg_->setVisible(false);  // only when an action failed
    al->addWidget(actionMsg_);
    col->addWidget(actionBox);

    // ---- profiles ----
    auto *profilesBox = new QGroupBox(T("Profiles"));
    auto *pl = new QVBoxLayout(profilesBox);
    profilesCount_ = note();
    pl->addWidget(profilesCount_);
    emptyState_ = new QFrame;
    auto *el = new QVBoxLayout(emptyState_);
    auto *emptyText = new QLabel(T("No program or device has its own profile yet. A profile changes the mode or single "
                                   "settings for one program or one device."));
    emptyText->setWordWrap(true);
    auto *emptyButton = new QPushButton(T("Add a profile..."));
    connect(emptyButton, &QPushButton::clicked, this, [this] { emit openPage(QStringLiteral("profiles")); });
    el->addWidget(emptyText);
    el->addWidget(emptyButton, 0, Qt::AlignLeft);
    pl->addWidget(emptyState_);
    cardHost_ = new QWidget;
    cardHost_->installEventFilter(this);
    cardGrid_ = new QGridLayout(cardHost_);
    cardGrid_->setContentsMargins(0, 0, 0, 0);
    cardGrid_->setSpacing(kSpaceMd);
    pl->addWidget(cardHost_);
    col->addWidget(profilesBox);
    col->addStretch(1);

    refresh();
}

void OverviewPage::refresh() {
    if (!ctx_.settings || !ctx_.status || !ctx_.strings) return;
    const spn::ui::OverviewData d = spn::ui::buildOverview(*ctx_.settings, *ctx_.status, *ctx_.strings);
    statusLine_->setText(qs8(d.statusLine));
    infoLine_->setText(qs8(d.infoLine));
    infoLine_->setVisible(!d.infoLine.empty());
    warningLine_->setText(qs8(d.warningLine));
    warningLine_->setVisible(!d.warningLine.empty());
    profilesCount_->setText(d.profiles.empty()
                                ? QString()
                                : T("%1 device profile(s), %2 program profile(s). The default mode is %3.")
                                      .arg(d.deviceProfiles).arg(d.appProfiles).arg(qs8(d.defaultMode)));
    profilesCount_->setVisible(!d.profiles.empty());
    emptyState_->setVisible(d.profiles.empty());

    // Rebuild the cards only when what they show changed (this runs every second).
    QString key;
    for (const spn::ui::OverviewProfile &p : d.profiles)
        key += QString::number(p.section) + QLatin1Char('|') + qs8(p.name) + QLatin1Char('|') + qs8(p.summary) + QLatin1Char('\n');
    if (key != cardsKey_) {
        cardsKey_ = key;
        rebuildCards();
    }
}

void OverviewPage::rebuildCards() {
    using namespace spn::ui::tokens;
    qDeleteAll(cards_);
    cards_.clear();
    const spn::ui::OverviewData d = spn::ui::buildOverview(*ctx_.settings, *ctx_.status, *ctx_.strings);
    for (const spn::ui::OverviewProfile &p : d.profiles) {
        auto *card = new QFrame(cardHost_);
        card->setFrameShape(QFrame::StyledPanel);
        card->setAutoFillBackground(true);
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(kSpaceMd, kSpaceMd, kSpaceMd, kSpaceMd);
        cl->setSpacing(kSpaceXs);
        auto *kind = note(p.kind == spn::Section::DEVICE ? T("Device") : T("Program"));
        auto *name = new QLabel(qs8(p.name));
        QFont f = name->font();
        f.setBold(true);
        name->setFont(f);
        name->setWordWrap(true);
        auto *summary = new QLabel(qs8(p.summary));
        summary->setWordWrap(true);
        auto *open = new QPushButton(T("Open"));
        open->setAccessibleName(T("Open profile") + QStringLiteral(" ") + qs8(p.name));
        const int section = static_cast<int>(p.section);
        connect(open, &QPushButton::clicked, this, [this, section] { emit openProfile(section); });
        cl->addWidget(kind);
        cl->addWidget(name);
        cl->addWidget(summary);
        cl->addStretch(1);
        cl->addWidget(open, 0, Qt::AlignLeft);
        cards_.push_back(card);
    }
    columns_ = 0;  // force the layout
    layoutCards();
}

// Cards wrap: as many columns as fit with kCardMinWidth each, at least one.
void OverviewPage::layoutCards() {
    // From the width of the page, not of the card host: the host can be held wide by the minimum width
    // of the cards of the current columns (a larger font), which would then never let it wrap.
    const int available = this->width() - 4 * spn::ui::tokens::kSpaceLg;
    // As many columns as fit, but not more than there are cards: a row of few cards fills the width.
    const int columns = std::max(1, std::min(available / kCardMinWidth, static_cast<int>(cards_.size())));
    if (columns == columns_ && cardGrid_->count() == static_cast<int>(cards_.size())) return;
    const int previousColumns = columns_;
    columns_ = columns;
    while (cardGrid_->count() > 0) delete cardGrid_->takeAt(0);
    for (size_t i = 0; i < cards_.size(); ++i)
        cardGrid_->addWidget(cards_[i], static_cast<int>(i) / columns, static_cast<int>(i) % columns);
    // Columns that no longer exist must not keep their stretch, or the cards would not fill the width.
    for (int c = 0; c < std::max(columns, previousColumns); ++c) cardGrid_->setColumnStretch(c, c < columns ? 1 : 0);
    // The number of rows changed: the height that the host needs is new, so tell the layouts above.
    cardGrid_->invalidate();
    cardHost_->updateGeometry();
}

bool OverviewPage::cardsFit() const {
    for (const QFrame *card : cards_)
        if (card->height() < card->minimumSizeHint().height()) return false;
    return true;
}

bool OverviewPage::cardsFillWidth() const {
    if (cards_.empty()) return true;
    // the cards of the first row together reach the right edge of the card area (within the gaps)
    int right = 0;
    for (const QFrame *card : cards_)
        if (card->y() == cards_.front()->y()) right = std::max(right, card->x() + card->width());
    return right >= cardHost_->width() - 2;
}

void OverviewPage::runAction(int index) {
    const std::vector<spn::ui::OverviewAction> actions = spn::ui::overviewActions();
    if (!ctx_.runner || index < 0 || index >= static_cast<int>(actions.size())) return;
    spn::ui::TrayEntry entry;
    entry.action = actions[static_cast<size_t>(index)].action;
    spn::ui::TrayHooks hooks;
    hooks.quit = [] { QApplication::quit(); };  // "Restart interface" starts a new one, then this one ends
    std::string err;
    const bool ok = spn::ui::performTrayAction(entry, *ctx_.runner, hooks, &err);
    actionMsg_->setText(ok ? QString() : qs8(err));
    actionMsg_->setVisible(!ok);
}

bool OverviewPage::eventFilter(QObject *watched, QEvent *e) {
    if (watched == cardHost_ && e->type() == QEvent::Resize) layoutCards();
    return QWidget::eventFilter(watched, e);
}

void OverviewPage::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    layoutCards();
}

void OverviewPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    refresh();
    layoutCards();
}

QWidget *createOverviewPage(const UiContext &ctx) { return new OverviewPage(ctx); }
