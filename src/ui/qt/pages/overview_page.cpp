// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "overview_page.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <algorithm>

#include "ui/model/design_tokens.hpp"
#include "log_dialog.hpp"
#include "ui/model/mode_info_model.hpp"
#include "ui/model/overview_model.hpp"
#include "ui/model/ui_links.hpp"
#include "ui/model/ui_progress.hpp"
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
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);  // a very wide window: the content stays centered
    outer->addWidget(scroll);
    auto *content = new QWidget;
    content->setMaximumWidth(kMaxContentWidth);
    content_ = content;
    auto *col = new QVBoxLayout(content);
    col->setContentsMargins(kPageMargin, kSpaceLg, kPageMargin, kPageMargin);
    col->setSpacing(kSectionGap);
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

    // ---- tasks: what the user usually wants, each opens the page that does it ----
    auto *taskBox = new QGroupBox(T("What do you want to do?"));
    auto *tl = new QHBoxLayout(taskBox);
    tl->setSpacing(kSpaceSm);
    for (const spn::ui::OverviewTask &task : spn::ui::overviewTasks()) {
        auto *b = new QPushButton(T(task.label.c_str()));
        b->setToolTip(T(task.tip.c_str()));
        b->setObjectName(qs8("task-" + task.id));
        const QString page = qs8(task.page);
        connect(b, &QPushButton::clicked, this, [this, page] { emit openPage(page); });
        tl->addWidget(b);
    }
    tl->addStretch(1);
    col->addWidget(taskBox);

    // ---- quick actions ----
    auto *actionBox = new QGroupBox(T("Quick actions"));
    auto *al = new QVBoxLayout(actionBox);
    quickGrid_ = new QGridLayout;  // the buttons wrap to more rows in a narrow window (layoutQuickActions)
    quickGrid_->setContentsMargins(0, 0, 0, 0);
    quickGrid_->setHorizontalSpacing(kRowGap);
    quickGrid_->setVerticalSpacing(kRowGap);
    auto *addProfile = new QPushButton(T("Add a profile..."));
    addProfile->setToolTip(T("Set up a program or a device on the Profiles page"));
    connect(addProfile, &QPushButton::clicked, this, [this] { emit openPage(QStringLiteral("profiles")); });
    quickButtons_.push_back(addProfile);
    auto *newForRunning = new QPushButton(T("New profile for the running program..."));
    newForRunning->setObjectName(QStringLiteral("new-program-profile"));
    newForRunning->setToolTip(T("Pick one of the running programs and give it its own profile"));
    connect(newForRunning, &QPushButton::clicked, this, [this] { emit newProgramProfile(); });
    quickButtons_.push_back(newForRunning);
    const std::vector<spn::ui::OverviewAction> actions = spn::ui::overviewActions();
    for (size_t i = 0; i < actions.size(); ++i) {
        auto *b = new QPushButton(T(actions[i].label.c_str()));
        b->setObjectName(qs8("action-" + actions[i].id));
        if (actions[i].id == "open_log") b->setToolTip(T("Show the end of the daemon log"));
        connect(b, &QPushButton::clicked, this, [this, i] { runAction(static_cast<int>(i)); });
        quickButtons_.push_back(b);
    }
    auto *docs = new QPushButton(T("Documentation"));
    docs->setObjectName(QStringLiteral("documentation"));
    docs->setToolTip(T("Open the documentation of the interface in your browser"));
    connect(docs, &QPushButton::clicked, this, [this] { openDocumentation(); });
    quickButtons_.push_back(docs);
    al->addLayout(quickGrid_);
    layoutQuickActions();
    auto *modeRow = new QHBoxLayout;
    auto *modeLabel = new QLabel(T("Active joystick mode:"));
    mode_ = new QComboBox;
    mode_->setAccessibleName(T("Active joystick mode"));
    mode_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    modeLabel->setBuddy(mode_);
    connect(mode_, QOverload<int>::of(&QComboBox::activated), this, [this](int i) { switchMode(mode_->itemData(i).toString()); });
    modeRow->addWidget(modeLabel);
    modeRow->addWidget(mode_);
    modeRow->addStretch(1);
    al->addLayout(modeRow);
    actionMsg_ = note();
    actionMsg_->setVisible(false);  // only when an action failed
    al->addWidget(actionMsg_);
    col->addWidget(actionBox);

    // ---- first-run guidance ----
    auto *gettingBox = new QGroupBox(T("Getting started"));
    new QVBoxLayout(gettingBox);
    gettingBox_ = gettingBox;
    gettingBox->setVisible(false);
    col->addWidget(gettingBox);

    // ---- profiles ----
    auto *profilesBox = new QGroupBox(T("Profiles"));
    auto *pl = new QVBoxLayout(profilesBox);
    profilesCount_ = note();
    pl->addWidget(profilesCount_);
    filter_ = new QLineEdit;
    filter_->setClearButtonEnabled(true);
    filter_->setPlaceholderText(T("Filter profiles by name, joystick mode or setting"));
    filter_->setAccessibleName(T("Filter profiles"));
    connect(filter_, &QLineEdit::textChanged, this, [this] { refresh(); });
    pl->addWidget(filter_);
    noMatch_ = note(T("No profile matches the filter."));
    pl->addWidget(noMatch_);
    emptyState_ = new QFrame;
    auto *el = new QVBoxLayout(emptyState_);
    auto *emptyText = new QLabel(T("No program or device has its own profile yet. A profile changes the joystick mode or single "
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

void OverviewPage::setProfileFilter(const QString &text) { filter_->setText(text); }

int OverviewPage::contentWidth() const { return content_->width(); }
QStringList OverviewPage::gettingStartedTexts() const { return gettingTexts_; }
QString OverviewPage::actionMessage() const { return actionMsg_->isVisible() ? actionMsg_->text() : QString(); }

void OverviewPage::showMessage(const std::string &err) {
    actionMsg_->setText(qs8(err));
    actionMsg_->setVisible(!err.empty());
}

void OverviewPage::refresh() {
    if (!ctx_.settings || !ctx_.status || !ctx_.strings) return;
    if (ctx_.modes) ctx_.modes->refreshIfChanged();
    const spn::ui::OverviewData d =
        spn::ui::buildOverview(*ctx_.settings, *ctx_.status, *ctx_.strings, ctx_.modes ? &ctx_.modes->modeSet() : nullptr);
    statusLine_->setText(qs8(d.statusLine));
    infoLine_->setText(qs8(d.infoLine));
    infoLine_->setVisible(!d.infoLine.empty());
    warningLine_->setText(qs8(d.warningLine));
    warningLine_->setVisible(!d.warningLine.empty());
    const std::vector<size_t> shown = spn::ui::filterOverviewProfiles(d, filter_->text().toUtf8().toStdString());
    profilesCount_->setText(d.profiles.empty()
                                ? QString()
                                : T("%1 device profile(s), %2 program profile(s). The Global joystick mode is %3.")
                                      .arg(d.deviceProfiles).arg(d.appProfiles).arg(qs8(d.defaultMode)));
    profilesCount_->setVisible(!d.profiles.empty());
    filter_->setVisible(!d.profiles.empty());
    noMatch_->setVisible(!d.profiles.empty() && shown.empty());
    emptyState_->setVisible(d.profiles.empty());
    rebuildModes(d);
    {
        const bool axesDone = spn::ui::loadUiProgress(spn::ui::uiProgressPath(ctx_.configPath)).axisAssistantApplied;
        const std::vector<spn::ui::GettingStartedStep> steps =
            spn::ui::gettingStartedSteps(d, *ctx_.status, *ctx_.strings, axesDone);
        gettingBox_->setVisible(spn::ui::showGettingStarted(steps));
        if (gettingBox_->isVisible()) rebuildGettingStarted(steps);
    }

    // Rebuild the cards only when what they show changed (this runs every second).
    QString key;
    for (size_t i : shown) {
        const spn::ui::OverviewProfile &p = d.profiles[i];
        key += QString::number(p.section) + QLatin1Char('|') + qs8(p.name) + QLatin1Char('|') + qs8(p.summary) +
               QLatin1Char('|') + qs8(p.details) + QLatin1Char('\n');
    }
    if (key != cardsKey_) {
        cardsKey_ = key;
        rebuildCards(d, shown);
    }
}

// The mode switch lists the loaded modes and marks the one the daemon runs. Without a running daemon
// there is nobody to ask, so it is disabled. Not rebuilt while its list is open.
void OverviewPage::rebuildModes(const spn::ui::OverviewData &d) {
    QString key = QLatin1String(d.daemonRunning ? "1" : "0") + qs8(d.activeMode);
    for (const spn::modelist::MenuEntry &e : d.modeChoices) key += QLatin1Char('|') + qs8(e.id) + QLatin1Char('=') + qs8(e.text);
    if (key == modeKey_ || mode_->view()->isVisible()) return;
    modeKey_ = key;
    mode_->clear();
    int current = -1;
    for (const spn::modelist::MenuEntry &e : d.modeChoices) {
        mode_->addItem(qs8(e.text), qs8(e.id));
        if (ctx_.modes && ctx_.strings) {  // the description of the joystick mode is the tooltip of its entry
            const spn::ModeDef *def = ctx_.modes->modeSet().find(e.id);
            mode_->setItemData(mode_->count() - 1,
                               def ? qs8(spn::ui::joystickModeTooltip(*def, *ctx_.strings)) : T("This joystick mode is not installed"),
                               Qt::ToolTipRole);
        }
        if (e.active) current = mode_->count() - 1;
    }
    mode_->setCurrentIndex(current);
    mode_->setEnabled(d.daemonRunning && mode_->count() > 0);
    mode_->setToolTip(d.daemonRunning
                          ? T("Switches the joystick mode of the running daemon until the next reload or restart. The Global "
                              "joystick mode in the configuration stays as it is.")
                          : T("The daemon is not running, so there is no joystick mode to switch."));
}

void OverviewPage::switchMode(const QString &id) {
    if (!ctx_.runner || id.isEmpty()) return;
    std::string err;
    spn::ui::requestActiveMode(ctx_.runtimeDir, *ctx_.runner, id.toUtf8().toStdString(), &err);
    showMessage(err);
}

void OverviewPage::openDocumentation() {
    if (!ctx_.runner) return;
    std::string err;
    spn::ui::openDocs(*ctx_.runner, "ui.md", &err);
    showMessage(err);
}

void OverviewPage::showLog() {
    if (ctx_.strings) LogDialog::showFor(window(), ctx_.runtimeDir, *ctx_.strings);
}

void OverviewPage::rebuildCards(const spn::ui::OverviewData &d, const std::vector<size_t> &shown) {
    using namespace spn::ui::tokens;
    qDeleteAll(cards_);
    cards_.clear();
    for (size_t index : shown) {
        const spn::ui::OverviewProfile &p = d.profiles[index];
        auto *card = new QFrame(cardHost_);
        card->setFrameShape(QFrame::StyledPanel);
        card->setProperty("card", true);  // rounded, tinted inset card (theme.cpp)
        card->setToolTip(qs8(p.details));  // every setting of the profile, the summary shows the first ones
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(kCardPadding, kCardPadding, kCardPadding, kCardPadding);
        cl->setSpacing(kSpaceSm);
        auto *kind = note(p.kind == spn::Section::DEVICE ? T("Device") : T("Program"));
        auto *name = new QLabel(qs8(p.name));
        QFont f = name->font();
        f.setBold(true);
        name->setFont(f);
        name->setWordWrap(true);
        auto *summary = new QLabel(qs8(p.summary));
        summary->setWordWrap(true);
        auto *overrides = note(qs8(p.overrideText));  // how much of the profile is its own, the rest is inherited
        overrides->setObjectName(QStringLiteral("overrideCount"));
        auto *open = new QPushButton(T("Open"));
        open->setAccessibleName(T("Open profile") + QStringLiteral(" ") + qs8(p.name));
        const int section = static_cast<int>(p.section);
        connect(open, &QPushButton::clicked, this, [this, section] { emit openProfile(section); });
        cl->addWidget(kind);
        cl->addWidget(name);
        cl->addWidget(summary);
        cl->addWidget(overrides);
        cl->addStretch(1);
        cl->addWidget(open, 0, Qt::AlignLeft);
        cards_.push_back(card);
    }
    columns_ = 0;  // force the layout
    layoutCards();
}

// Cards wrap: as many columns as fit with kCardMinWidth each, at least one.
void OverviewPage::rebuildGettingStarted(const std::vector<spn::ui::GettingStartedStep> &steps) {
    QString key;
    for (const auto &st : steps) key += qs8(st.id) + (st.done ? QLatin1Char('1') : QLatin1Char('0')) + qs8(st.hint);
    if (key == gettingKey_) return;
    gettingKey_ = key;
    gettingTexts_.clear();
    auto *lay = qobject_cast<QVBoxLayout *>(gettingBox_->layout());
    while (QLayoutItem *it = lay->takeAt(0)) {
        if (QWidget *w = it->widget()) w->deleteLater();
        if (QLayout *l = it->layout()) {
            while (QLayoutItem *x = l->takeAt(0)) {
                if (QWidget *w = x->widget()) w->deleteLater();
                delete x;
            }
        }
        delete it;
    }
    for (const spn::ui::GettingStartedStep &st : steps) {
        auto *row = new QHBoxLayout;
        // The state is written out: "Done:" / "To do:" (never only a colour or a symbol).
        QString text = (st.done ? T("Done:") : T("To do:")) + QLatin1Char(' ') + qs8(st.text);
        if (!st.done && !st.hint.empty()) text += QLatin1Char(' ') + qs8(st.hint);
        gettingTexts_ << text;
        auto *label = new QLabel(text, gettingBox_);
        label->setWordWrap(true);
        row->addWidget(label, 1);
        if (!st.page.empty() && !st.done) {
            auto *open = new QPushButton(T("Open"), gettingBox_);
            open->setAccessibleName(T("Open") + QLatin1Char(' ') + qs8(st.text));
            const QString page = qs8(st.page);
            connect(open, &QPushButton::clicked, this, [this, page] { emit openPage(page); });
            row->addWidget(open);
        }
        lay->addLayout(row);
    }
}

// The quick action buttons in as many columns as fit (at least one), so the row never forces the
// page wider than the window.
void OverviewPage::layoutQuickActions() {
    int widest = 0;
    for (const QPushButton *b : quickButtons_) widest = std::max(widest, b->sizeHint().width());
    // the content is not wider than kMaxContentWidth, however wide the window is (as for the cards)
    const int available = std::min(width(), spn::ui::tokens::kMaxContentWidth) - 2 * spn::ui::tokens::kPageMargin -
                          2 * spn::ui::tokens::kCardPadding;
    const int cell = widest + spn::ui::tokens::kRowGap;
    int columns = width() > 0 ? std::max(1, (available + spn::ui::tokens::kRowGap) / cell) : 1;
    columns = std::min(columns, static_cast<int>(quickButtons_.size()));
    if (columns == quickColumns_) return;
    quickColumns_ = columns;
    while (quickGrid_->count() > 0) delete quickGrid_->takeAt(0);
    for (size_t i = 0; i < quickButtons_.size(); ++i)
        quickGrid_->addWidget(quickButtons_[i], static_cast<int>(i) / columns, static_cast<int>(i) % columns);
    for (int c = 0; c <= columns; ++c) quickGrid_->setColumnStretch(c, c == columns ? 1 : 0);
    quickGrid_->invalidate();
}

void OverviewPage::layoutCards() {
    // From the width of the page, not of the card host: the host can be held wide by the minimum width
    // of the cards of the current columns (a larger font), which would then never let it wrap.
    const int available = std::min(this->width(), spn::ui::tokens::kMaxContentWidth) - 2 * spn::ui::tokens::kPageMargin - 2 * spn::ui::tokens::kCardPadding;
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
    hooks.showLog = [this] { showLog(); };
    std::string err;
    const bool ok = spn::ui::performTrayAction(entry, *ctx_.runner, hooks, &err);
    showMessage(ok ? std::string() : err);
}

bool OverviewPage::eventFilter(QObject *watched, QEvent *e) {
    if (watched == cardHost_ && e->type() == QEvent::Resize) layoutCards();
    return QWidget::eventFilter(watched, e);
}

void OverviewPage::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    layoutQuickActions();
    layoutCards();
}

// A larger font or another style makes buttons and cards wider without resizing the page, so the
// columns are planned again (otherwise a stale plan keeps the page wider than the window). The page gets
// the change before its children do: the plan is made when the event loop is idle again.
void OverviewPage::changeEvent(QEvent *e) {
    QWidget::changeEvent(e);
    if ((e->type() == QEvent::FontChange || e->type() == QEvent::StyleChange) && !replanQueued_) {
        replanQueued_ = true;
        QMetaObject::invokeMethod(
            this,
            [this] {
                replanQueued_ = false;
                quickColumns_ = 0;
                columns_ = 0;
                layoutQuickActions();
                layoutCards();
            },
            Qt::QueuedConnection);
    }
}

// The size hints of the buttons are only final once the style and the font are applied (the first plan is
// made before that on some platforms): a layout request means a hint changed, so the columns are checked
// again. The check returns at once when the number of columns stays the same.
bool OverviewPage::event(QEvent *e) {
    const bool handled = QWidget::event(e);
    if (e->type() == QEvent::LayoutRequest) {
        layoutQuickActions();
        layoutCards();
    }
    return handled;
}

void OverviewPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    refresh();
    layoutQuickActions();
    layoutCards();
}

QWidget *createOverviewPage(const UiContext &ctx) { return new OverviewPage(ctx); }
