// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "profiles_page.hpp"

#include <algorithm>
#include <cstdlib>

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QHideEvent>
#include <QShowEvent>

#include "add_app_dialog.hpp"
#include "add_device_dialog.hpp"
#include "button_row.hpp"
#include "channels_editor.hpp"
#include "info_popup.hpp"
#include "key_combo.hpp"
#include "platform/fs.hpp"
#include "ui/model/binding_editor_model.hpp"
#include "ui/model/button_choice_model.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/effective_model.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/examples_model.hpp"
#include "ui/model/mode_info_model.hpp"
#include "ui/model/key_list.hpp"
#include "ui/model/profile_export.hpp"
#include "ui/model/profile_tree_model.hpp"
#include "platform_hooks.hpp"
#include "theme.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
enum { kRoleSection = Qt::UserRole };  // section index, -1 for group headers

namespace tk = spn::ui::tokens;

QLabel *wrapLabel(const QString &text, QWidget *parent = nullptr) {
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QLabel *noteLabel(const QString &text, QWidget *parent = nullptr) {
    QLabel *l = wrapLabel(text, parent);
    l->setForegroundRole(QPalette::PlaceholderText);
    return l;
}

QString iconResource(Section::Kind kind) {
    return kind == Section::GLOBAL   ? QStringLiteral(":/icons/global.svg")
           : kind == Section::DEVICE ? QStringLiteral(":/icons/device.svg")
                                     : QStringLiteral(":/icons/app.svg");
}

}  // namespace

QWidget *createProfilesPage(const UiContext &ctx) { return new ProfilesPage(ctx); }

QStringList ProfilesPage::profileListLabels() const {
    QStringList out;
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        const QTreeWidgetItem *top = tree_->topLevelItem(g);
        out << top->text(0);
        for (int c = 0; c < top->childCount(); ++c) out << top->child(c)->text(0);
    }
    return out;
}

QString ProfilesPage::treeLabel(size_t section) const {
    const SettingsModel::SectionInfo info = ctx_.settings->sections()[section];
    return section == 0 ? T("Global") : qs8(spn::ui::profileListLabel(info.pattern, ctx_.settings->overrideCount(section)));
}

ProfilesPage::ProfilesPage(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx) {
    env_.runningApps = [] { return spn::makeRunningAppsProvider(); };
    env_.activeWindow = dofscroll_ui::makeActiveWindowPoll();
    {
        const std::string dir = ctx_.runtimeDir;  // empty on Windows (named pipe)
        feedClient_ = std::make_unique<spn::LiveFeedClient>(
            [dir](std::string *err) { return spn::connectFeed(dir, err); });
        feedClock_.start();
        env_.devices = [this] {
            pollDeviceFeed();
            return devices_.devices();
        };
        auto *t = new QTimer(this);  // the device list changes rarely: 5 Hz is plenty
        t->setInterval(200);
        connect(t, &QTimer::timeout, this, [this] { pollDeviceFeed(); });
        t->start();
    }
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(tk::kPageMargin, tk::kSpaceSm, tk::kPageMargin, tk::kSpaceMd);
    auto *split = new QSplitter(Qt::Horizontal, this);

    // Left: the list of the profiles, always visible next to the tabs of the selected one.
    auto *left = new QWidget(split);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, tk::kSpaceMd, 0);
    ll->setSpacing(tk::kSpaceSm);
    profileFilter_ = new QLineEdit(left);
    profileFilter_->setClearButtonEnabled(true);
    profileFilter_->setPlaceholderText(T("Search profiles..."));
    profileFilter_->setAccessibleName(T("Search profiles"));
    profileFilter_->setToolTip(T("Show only the programs and devices whose name contains this text"));
    ll->addWidget(profileFilter_);
    // No explicit minimum width on the pane: an explicit minimum replaces the layout's own minimum, and the
    // buttons below ("Add application...") would be squeezed at a larger font. The list sets the floor.
    tree_ = new QTreeWidget(left);
    tree_->setMinimumWidth(170);
    tree_->setMinimumHeight(160);
    tree_->setHeaderHidden(true);
    tree_->setAccessibleName(T("Profiles"));
    ll->addWidget(tree_, 1);
    add_app_ = new QPushButton(T("Add application..."), left);
    add_app_->setToolTip(T("Create a profile for a program, matched by its window class or name"));
    add_dev_ = new QPushButton(T("Add device..."), left);
    add_dev_->setToolTip(T("Create a profile for a connected device"));
    for (QPushButton *b : {add_app_, add_dev_}) ll->addWidget(b);

    // Right: who is edited, the search box of the settings and the tabs.
    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(tk::kSpaceMd, 0, 0, 0);
    rl->setSpacing(tk::kSpaceSm);
    auto *header = new QWidget(right);
    header->setObjectName(QStringLiteral("profileHeader"));
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(tk::kSpaceMd);
    headerIcon_ = new QLabel(header);
    headerIcon_->setFixedSize(tk::kIconSize * 3 / 2, tk::kIconSize * 3 / 2);
    headerIcon_->setScaledContents(true);
    headerIcon_->setAccessibleName(T("Icon of the edited profile"));
    headerName_ = new QLabel(header);
    headerName_->setProperty("heading", QStringLiteral("section"));
    headerName_->setAccessibleName(T("Edited profile"));
    headerName_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    headerKind_ = new QLabel(header);
    headerKind_->setForegroundRole(QPalette::PlaceholderText);
    headerKind_->setAccessibleName(T("Kind of the edited profile"));
    hl->addWidget(headerIcon_);
    hl->addWidget(headerName_);
    hl->addWidget(headerKind_);
    hl->addStretch(1);
    rl->addWidget(header);
    headerNote_ = new QLabel(right);
    headerNote_->setWordWrap(true);
    headerNote_->setForegroundRole(QPalette::PlaceholderText);
    headerNote_->setAccessibleName(T("About inheritance"));
    rl->addWidget(headerNote_);
    settingsFilter_ = new QLineEdit(right);
    settingsFilter_->setClearButtonEnabled(true);
    settingsFilter_->setPlaceholderText(T("Search settings of this profile..."));
    settingsFilter_->setAccessibleName(T("Search settings"));
    settingsFilter_->setToolTip(T("Show only the settings whose name or key contains this text"));
    rl->addWidget(settingsFilter_);
    tabs_ = new QTabWidget(right);
    tabs_->setAccessibleName(T("Profile sections"));
    tabs_->setDocumentMode(true);
    rl->addWidget(tabs_, 1);
    tabs_->setMinimumWidth(260);  // the editor never gets squashed, and the page fits the smallest window
    split->setChildrenCollapsible(false);

    // The tabs exist once; they stay when another profile is selected (only their content changes).
    auto makeArea = [](const char *id) {
        auto *area = new QScrollArea;
        area->setWidgetResizable(true);
        area->setFrameShape(QFrame::NoFrame);
        area->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        area->setProperty("tabId", QString::fromLatin1(id));
        return area;
    };
    general_ = makeArea("general");
    buttons_ = makeArea("buttons");
    overrides_ = makeArea("overrides");
    channels_ = new ChannelsEditor(ctx_, tabs_);
    channels_->setProperty("tabId", QStringLiteral("channels"));
    tabs_->addTab(general_, T("General"));
    tabs_->addTab(channels_, T("Channels"));
    tabs_->addTab(buttons_, T("Buttons"));
    tabs_->addTab(overrides_, T("Overrides"));
    tabs_->setTabToolTip(3, T("Advanced: every value this profile sets itself"));

    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({260, 760});
    outer->addWidget(split, 1);

    setTabOrder(profileFilter_, tree_);
    setTabOrder(tree_, add_app_);
    setTabOrder(add_app_, add_dev_);
    setTabOrder(add_dev_, settingsFilter_);
    setTabOrder(settingsFilter_, tabs_);

    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *cur, QTreeWidgetItem *prev) { onSelectionChanged(cur, prev); });
    connect(profileFilter_, &QLineEdit::textChanged, this, [this] { applyProfileFilter(); });
    connect(settingsFilter_, &QLineEdit::textChanged, this, [this] {
        channels_->setFilter(settingsFilter_->text());
        queueRebuild();
    });
    connect(tabs_, &QTabWidget::currentChanged, this, [this] { ctx_.saveNow(); });
    connect(add_app_, &QPushButton::clicked, this, &ProfilesPage::addApplication);
    connect(add_dev_, &QPushButton::clicked, this, &ProfilesPage::addDevice);

    // the numbers of overrides in the list follow the edits
    if (ctx_.autosave) connect(ctx_.autosave, &Autosaver::written, this, [this] { refreshTreeLabels(); });

    rebuildTree(0);
}

void ProfilesPage::refreshTreeLabels() {
    if (!ctx_.settings) return;
    const spn::ui::IdentityStrings fallback;
    const spn::ui::StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        const int s = (*it)->data(0, kRoleSection).toInt();
        if (s <= 0 || size_t(s) >= ctx_.settings->sectionCount()) continue;
        (*it)->setText(0, treeLabel(size_t(s)));
        (*it)->setToolTip(0, qs8(spn::ui::profileOverrideCountText(ctx_.settings->overrideCount(size_t(s)), strings)));
    }
}

int ProfilesPage::profileCount() const { return int(ProfileTreeModel::entries(*ctx_.settings).size()) - 1; }

size_t ProfilesPage::currentSection() const {
    const QTreeWidgetItem *it = tree_->currentItem();
    const int s = it ? it->data(0, kRoleSection).toInt() : 0;
    return s < 0 ? 0 : size_t(s);
}

void ProfilesPage::problem(const QString &text) { ctx_.report(text, true); }

void ProfilesPage::rebuildTree(size_t selectSection) {
    QSignalBlocker block(tree_);
    tree_->clear();
    const spn::ui::IdentityStrings fallbackStrings;
    const spn::ui::StringProvider *sp = ctx_.strings ? ctx_.strings : &fallbackStrings;
    QTreeWidgetItem *dev = nullptr, *app = nullptr, *select = nullptr;
    const QColor iconColor = palette().color(QPalette::WindowText);
    for (const ProfileEntry &e : ProfileTreeModel::entries(*ctx_.settings)) {
        QTreeWidgetItem *it;
        if (e.isDefault) {
            it = new QTreeWidgetItem(tree_, QStringList(T("Global")));
            it->setToolTip(0, qs8(headerExplanation(0, Section::GLOBAL, *sp)));
        } else {
            QTreeWidgetItem *&group = e.kind == Section::DEVICE ? dev : app;
            if (!group) {
                group = new QTreeWidgetItem(tree_, QStringList(T(ProfileTreeModel::groupTitle(e.kind))));
                group->setData(0, kRoleSection, -1);
                group->setFlags(Qt::ItemIsEnabled);
                group->setExpanded(true);
            }
            const size_t n = ctx_.settings->overrideCount(e.section);
            it = new QTreeWidgetItem(group, QStringList(qs8(profileListLabel(e.pattern, n))));
            it->setToolTip(0, qs8(profileOverrideCountText(n, *sp)));
        }
        it->setIcon(0, themedIcon(iconResource(e.kind), iconColor, tk::kIconSize));
        it->setData(0, kRoleSection, int(e.section));
        if (e.section == selectSection) select = it;
    }
    if (!select) select = tree_->topLevelItem(0);
    tree_->setCurrentItem(select);
    block.unblock();
    applyProfileFilter();
    rebuildContent();
}

bool ProfilesPage::selectSection(size_t section) {
    for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
        if ((*it)->data(0, kRoleSection).toInt() == int(section) && (*it)->flags() & Qt::ItemIsSelectable) {
            if (tree_->currentItem() == *it) {
                rebuildContent();  // the model may have changed since
                return true;
            }
            tree_->setCurrentItem(*it);  // onSelectionChanged rebuilds the content
            return true;
        }
    }
    return false;
}

// Search box of the list: hides the profiles whose name does not contain the text, and a group
// header without a visible profile. Global is always shown.
void ProfilesPage::applyProfileFilter() {
    const std::string text = profileFilter_->text().toUtf8().toStdString();
    std::vector<size_t> keep;
    for (const ProfileEntry &e : ProfileTreeModel::filter(ProfileTreeModel::entries(*ctx_.settings), text))
        keep.push_back(e.section);
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        QTreeWidgetItem *top = tree_->topLevelItem(g);
        if (top->data(0, kRoleSection).toInt() >= 0) continue;  // Global
        int visible = 0;
        for (int c = 0; c < top->childCount(); ++c) {
            QTreeWidgetItem *it = top->child(c);
            const bool show = std::find(keep.begin(), keep.end(), size_t(it->data(0, kRoleSection).toInt())) != keep.end();
            it->setHidden(!show);
            visible += show ? 1 : 0;
        }
        top->setHidden(visible == 0);
    }
}

int ProfilesPage::visibleProfileCount() const {
    int n = 0;
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        const QTreeWidgetItem *top = tree_->topLevelItem(g);
        if (top->data(0, kRoleSection).toInt() >= 0) continue;
        for (int c = 0; c < top->childCount(); ++c) n += top->child(c)->isHidden() ? 0 : 1;
    }
    return n;
}

void ProfilesPage::setProfileFilter(const QString &text) { profileFilter_->setText(text); }

void ProfilesPage::setSettingsFilter(const QString &text) {
    settingsFilter_->setText(text);
    channels_->setFilter(text);
    rebuildContent();  // at once, not queued: callers read the rows
}

int ProfilesPage::settingRowCount() const { return settingRows_ + channels_->visibleRowCount(); }

// Another profile was picked: what was typed so far is written first, then the tabs show the new
// profile (the tab stays).
void ProfilesPage::onSelectionChanged(QTreeWidgetItem *, QTreeWidgetItem *) {
    ctx_.saveNow();
    rebuildContent();
}

QStringList ProfilesPage::tabIds() const {
    QStringList out;
    for (int i = 0; i < tabs_->count(); ++i) out << tabs_->widget(i)->property("tabId").toString();
    return out;
}

QString ProfilesPage::currentTabId() const {
    return tabs_->currentWidget() ? tabs_->currentWidget()->property("tabId").toString() : QString();
}

bool ProfilesPage::selectTab(const QString &id) {
    for (int i = 0; i < tabs_->count(); ++i)
        if (tabs_->widget(i)->property("tabId").toString() == id) {
            tabs_->setCurrentIndex(i);
            return true;
        }
    return false;
}

QString ProfilesPage::headerName() const { return headerName_->text(); }
QString ProfilesPage::headerKind() const { return headerKind_->text(); }
QString ProfilesPage::headerNote() const { return headerNote_->text(); }
bool ProfilesPage::headerHasIcon() const { return !headerIcon_->pixmap(Qt::ReturnByValue).isNull(); }

// A palette change (theme) repaints the icons in the colour of the new palette.
void ProfilesPage::changeEvent(QEvent *e) {
    QWidget::changeEvent(e);
    if (e->type() == QEvent::PaletteChange && tree_ && ctx_.settings) {
        const QColor c = palette().color(QPalette::WindowText);
        for (QTreeWidgetItemIterator it(tree_); *it; ++it) {
            const int s = (*it)->data(0, kRoleSection).toInt();
            if (s < 0 || size_t(s) >= ctx_.settings->sectionCount()) continue;
            const Section::Kind kind = s == 0 ? Section::GLOBAL : ctx_.settings->sections()[size_t(s)].kind;
            (*it)->setIcon(0, themedIcon(iconResource(kind), c, tk::kIconSize));
        }
        rebuildHeader();
    }
}

void ProfilesPage::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    ctx_.saveNow();  // leaving the page: the last edit is on disk
}

// The page comes back: the installed joystick modes or the device may have changed meanwhile.
void ProfilesPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    rebuildContent();
}

void ProfilesPage::queueRebuild() {
    if (rebuildQueued_) return;
    rebuildQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        rebuildQueued_ = false;
        rebuildContent();
    });
}

// Reads the feed (bounded work per call), merges the status file fallback and queues a rebuild
// of the page when the device list changed (the BUTTON_<n> limit may differ).
void ProfilesPage::pollDeviceFeed() {
    if (!feedClient_) return;
    const uint32_t before = feedModel_.infoRevision();
    const bool hadFeed = feedModel_.hasDevices();
    feedModel_.pump(*feedClient_, static_cast<int64_t>(feedClock_.nsecsElapsed()));
    if (ctx_.status) devices_.setStatusDevice(ctx_.status->status().device);
    devices_.setFeed(feedModel_.hasDevices(), feedModel_.devices());
    if (feedModel_.infoRevision() != before || feedModel_.hasDevices() != hadFeed) queueRebuild();
}

// Button count of the connected device (or of the device profile's device).
void ProfilesPage::refreshDeviceLimit(size_t section) {
    const SettingsModel::SectionInfo info = ctx_.settings->sections()[section];
    const spn::ui::DeviceSourceModel::Limit l = devices_.buttonLimitDevice(info.kind == Section::DEVICE, info.pattern);
    ctx_.settings->setDeviceInfo(l.name, l.reported);
}

// Icon, name and kind of the edited profile: the editor never looks like a global page.
void ProfilesPage::rebuildHeader() {
    const size_t section = shown_ < ctx_.settings->sectionCount() ? shown_ : 0;
    const SettingsModel::SectionInfo info = ctx_.settings->sections()[section];
    const int px = tk::kIconSize * 3 / 2;
    const QIcon ic = themedIcon(iconResource(info.kind), palette().color(QPalette::WindowText), px);
    headerIcon_->setPixmap(ic.pixmap(px, px));
    headerName_->setText(section == 0 ? T("Global") : qs8(info.pattern));
    const spn::ui::IdentityStrings fallback;
    headerNote_->setText(qs8(spn::ui::headerExplanation(section, info.kind, ctx_.strings ? *ctx_.strings : fallback)));
    headerKind_->setText(section == 0                   ? T("Global profile")
                         : info.kind == Section::DEVICE ? T("Device profile")
                                                        : T("Program profile"));
}

// Fills the tab area with the parts (the old content is replaced).
void ProfilesPage::setTabContent(QScrollArea *area, const QList<QWidget *> &parts) {
    const int scrollPos = area->verticalScrollBar()->value();
    auto *content = new QWidget;
    content->setMaximumWidth(tk::kMaxContentWidth);
    auto *l = new QVBoxLayout(content);
    l->setContentsMargins(tk::kSpaceMd, tk::kSpaceLg, tk::kSpaceMd, tk::kPageMargin);
    l->setSpacing(tk::kSectionGap);
    for (QWidget *w : parts)
        if (w) l->addWidget(w);
    l->addStretch(1);
    area->setWidget(content);  // deletes the previous content
    if (scrollPos > 0) QTimer::singleShot(0, area, [area, scrollPos] { area->verticalScrollBar()->setValue(scrollPos); });
}

void ProfilesPage::rebuildContent() {
    const size_t section = currentSection();
    shown_ = section;
    if (ctx_.modes) ctx_.settings->setModes(ctx_.modes->modeSet(), ctx_.modesDir);
    refreshDeviceLimit(section);
    rebuildHeader();
    settingRows_ = 0;
    const bool filtering = !settingsFilter_->text().isEmpty();
    const ProfilePageLayout layout = profilePageLayout(*ctx_.settings, section);

    // Channels: the persistent editor shows this profile.
    channels_->setSection(section);
    channels_->setFilter(settingsFilter_->text());

    auto noMatch = [filtering](const QList<QWidget *> &parts) {
        QList<QWidget *> out = parts;
        bool any = false;
        for (QWidget *w : parts) any = any || w;
        if (!any && filtering) out << noteLabel(T("No setting of this tab matches the search."));
        return out;
    };

    // General: the joystick mode, whether the profile sends anything, what the mode is.
    QList<QWidget *> general;
    if (section == 0)
        general << noteLabel(T("The Global profile is the base of every other profile. A device or program profile "
                               "inherits its settings unless it chooses another joystick mode or overrides a value."));
    general << buildModeSelector(section);
    if (layout.unknownMode)
        general << noteLabel(T("This profile names a joystick mode that is not installed; it behaves like the "
                               "Global joystick mode until you choose another one."));
    general << buildEnabled(section);
    if (layout.modeInfo) general << buildModeInfo(section);
    if (!filtering) general << buildProfileActions(section);  // not a setting: hidden while searching
    setTabContent(general_, noMatch(general));

    // Buttons: the bindings of the profile, or a hint when the joystick mode brings its own.
    QList<QWidget *> buttons;
    if (ctx_.notes && !ctx_.notes->empty()) {
        QString text;
        for (const std::string &n : *ctx_.notes) text += (text.isEmpty() ? QString() : QStringLiteral("\n")) + qs8(n);
        auto *note = wrapLabel(text);
        note->setObjectName(QStringLiteral("legacyButtonNotes"));
        note->setAccessibleName(T("Notes about the old button settings"));
        buttons << note;
    }
    if (layout.bindingEditor) {
        buttons << buildBindings(section);
    } else {
        buttons << noteLabel(T("The joystick mode of this profile has its own button bindings; they are listed on the "
                               "General tab. Choose \"Same as Global\" or the normal joystick mode to assign buttons "
                               "here."));
    }
    setTabContent(buttons_, noMatch(buttons));

    // Overrides (advanced): everything the profile sets itself.
    QList<QWidget *> over;
    if (section == 0)
        over << noteLabel(T("The Global profile holds the base values, so it has nothing to override. Change them on the "
                            "Channels and Buttons tabs."));
    else
        over << buildOverrides(section);
    setTabContent(overrides_, noMatch(over));
}

// Search box of the settings: does a row (label and key) match the text?
bool ProfilesPage::settingMatches(const QString &label, const std::string &key) const {
    return ProfileTreeModel::filterMatches(settingsFilter_->text().toUtf8().toStdString(),
                                           label.toUtf8().toStdString(), key);
}

QWidget *ProfilesPage::buildModeSelector(size_t section) {
    if (!settingMatches(T("Joystick mode"), "MODE")) return nullptr;
    ++settingRows_;
    auto *box = new QGroupBox(T("Joystick mode"));
    auto *l = new QVBoxLayout(box);
    l->setContentsMargins(tk::kCardPadding, tk::kCardPadding, tk::kCardPadding, tk::kCardPadding);
    auto *combo = new QComboBox(box);
    combo->setAccessibleName(T("Joystick mode"));
    combo->setToolTip(T("What the axes do while this profile is active") + QStringLiteral("\n") +
                      T("Config key: %1").arg(QStringLiteral("MODE")));
    const spn::ui::IdentityStrings fallbackStrings;
    const spn::ui::StringProvider &strs = ctx_.strings ? *ctx_.strings : fallbackStrings;
    const std::vector<SettingsModel::ModeOption> opts = ctx_.settings->modeOptions(section);
    const std::string globalMode = ctx_.settings->effectiveMode(0);
    for (size_t i = 0; i < opts.size(); ++i) {
        const SettingsModel::ModeOption &o = opts[i];
        QString text = o.id.empty() ? T("Same as Global") : qs8(o.label);
        if (!o.id.empty() && o.label != o.id) text += QStringLiteral(" (") + qs8(o.id) + QLatin1Char(')');
        if (!o.known) text += QStringLiteral(" - ") + T("not installed");
        combo->addItem(text);
        // every entry carries the description of its joystick mode as tooltip
        QString tip;
        if (o.id.empty()) {
            const ModeDef *g = ctx_.settings->modeDef(globalMode);
            tip = T("Uses the joystick mode of Global: %1").arg(g && !g->label.empty() ? qs8(g->label) : qs8(globalMode)) +
                  QStringLiteral("\n") + (g ? qs8(spn::ui::joystickModeTooltip(*g, strs)) : QString());
        } else if (!o.known) {
            tip = T("This joystick mode is not installed");
        } else {
            tip = o.description.empty() ? qs8(spn::ui::noDescriptionText(strs)) : qs8(o.description);
        }
        combo->setItemData(combo->count() - 1, tip, Qt::ToolTipRole);
    }
    combo->setCurrentIndex(std::max(0, ctx_.settings->selectedModeOption(section)));
    // do not ask for the width of the longest entry: with large fonts the page must not need a horizontal scroll bar
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(6);
    // the "i" button: name, origin, description and what the joystick mode does, for the chosen entry
    auto infoFor = [this, combo, opts, globalMode, &strs]() {
        const int idx = std::max(0, combo->currentIndex());
        const std::string id = size_t(idx) < opts.size() && !opts[size_t(idx)].id.empty() ? opts[size_t(idx)].id : globalMode;
        const ModeDef *def = ctx_.settings->modeDef(id);
        if (!def) return T("This joystick mode is not installed");
        std::string origin;
        if (ctx_.modes)
            if (const spn::ui::ModeCatalog::ModeEntry *e = ctx_.modes->find(id)) origin = spn::ui::modeOriginText(e->origin, strs);
        return qs8(spn::ui::joystickModeInfo(*def, origin, strs));
    };
    auto *infoButton = makeInfoButton(box, T("About this joystick mode"), T("About this joystick mode: its description and what it does"));
    connect(infoButton, &QToolButton::clicked, this, [infoButton, infoFor] { InfoPopup::open(infoButton, infoFor()); });
    connect(combo, &QComboBox::currentIndexChanged, this, [combo, infoButton, infoFor](int) { combo->setToolTip(infoFor()); });
    combo->setToolTip(infoFor());
    auto *comboRow = new QHBoxLayout;
    comboRow->addWidget(combo, 1);
    comboRow->addWidget(infoButton);
    l->addLayout(comboRow);
    l->addWidget(noteLabel(section == 0 ? T("The joystick mode that applies unless a device or program profile chooses "
                                            "another one.")
                                        : T("Choose another joystick mode for this profile, or keep \"Same as Global\".")));
    {   // where the joystick mode comes from; in Global the profiles that choose another one
        const spn::ui::IdentityStrings fallback;
        const spn::ui::StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
        std::string text;
        QString tip;
        if (section == 0) {
            std::vector<std::string> names;
            text = spn::ui::overrideHint(*ctx_.settings, "MODE", strings, &names);
            if (!text.empty()) tip = qs8(spn::ui::overrideHintTooltip(names, strings));
        } else {
            spn::ui::KeyOrigin o;
            o.kind = ctx_.settings->isOverridden(section, "MODE") ? spn::ui::KeyOrigin::Kind::SetHere
                     : ctx_.settings->isOverridden(0, "MODE")      ? spn::ui::KeyOrigin::Kind::FromGlobal
                                                                   : spn::ui::KeyOrigin::Kind::FromGlobalDefault;
            o.value = ctx_.settings->effectiveMode(section);
            o.inherited = ctx_.settings->effectiveMode(0);
            text = spn::ui::originBadgeText(o, strings);
        }
        if (!text.empty()) {
            auto *badge = noteLabel(qs8(text));
            badge->setObjectName(QStringLiteral("modeOrigin"));
            badge->setToolTip(tip);
            l->addWidget(badge);
        }
    }
    connect(combo, &QComboBox::activated, this, [this, section](int idx) {
        std::string err;
        bool changed = false;
        if (!ctx_.settings->setMode(section, idx, &err, &changed)) problem(qs8(err));
        else ctx_.saveSoon();
        queueRebuild();  // the layout follows the effective mode
    });
    return box;
}

// Device and program profiles: whether the profile sends anything. [global]: the master switch is
// on the Settings page, not here (no setting appears in two places).
// What belongs to the selected profile as a whole: another program or device for it, export, removal.
// Global has no program or device and cannot be removed; it can be exported.
QWidget *ProfilesPage::buildProfileActions(size_t section) {
    auto *box = new QGroupBox(T("This profile"));
    // One column: three buttons in a row do not fit the narrow tab at the default size (font scale self-test).
    auto *l = new QVBoxLayout(box);
    l->setContentsMargins(tk::kCardPadding, tk::kCardPadding, tk::kCardPadding, tk::kCardPadding);
    if (section != 0) {
        const bool app = ctx_.settings->sections()[section].kind == Section::APP;
        auto *change = new QPushButton(app ? T("Change application...") : T("Change device..."));
        change->setToolTip(app ? T("Choose another program for this profile, for example when its name changed; the "
                                   "settings stay")
                               : T("Choose another device for this profile; the settings stay"));
        connect(change, &QPushButton::clicked, this, &ProfilesPage::renameProfile);
        l->addWidget(change, 0, Qt::AlignLeft);
    }
    auto *exp = new QPushButton(T("Export..."));
    exp->setToolTip(T("Save this profile (its section) as a .conf file"));
    connect(exp, &QPushButton::clicked, this, &ProfilesPage::exportProfile);
    l->addWidget(exp, 0, Qt::AlignLeft);
    if (section != 0) {
        auto *remove = new QPushButton(T("Remove"));
        remove->setToolTip(T("Delete this profile and its settings"));
        connect(remove, &QPushButton::clicked, this, &ProfilesPage::removeProfile);
        l->addWidget(remove, 0, Qt::AlignLeft);
    }
    return box;
}

QWidget *ProfilesPage::buildEnabled(size_t section) {
    if (section == 0) return nullptr;
    if (!settingMatches(T("Send events"), "ENABLED")) return nullptr;
    SettingsModel::Value v;
    if (!ctx_.settings->value(section, "ENABLED", &v)) return nullptr;
    ++settingRows_;
    auto *box = new QGroupBox(T("Output"));
    auto *l = new QGridLayout(box);
    l->setContentsMargins(tk::kCardPadding, tk::kCardPadding, tk::kCardPadding, tk::kCardPadding);
    l->setColumnStretch(0, 1);
    // The switch keeps a short text (a check box cannot wrap); the sentence is a wrapped label under it.
    auto *check = new QCheckBox(T("Send events"));
    check->setChecked(v.text == "1");
    check->setAccessibleName(T("Send events"));
    check->setToolTip(T("Switch this off for a program that reads the SpaceMouse itself") + QStringLiteral("\n") +
                      T("Config key: %1").arg(QStringLiteral("ENABLED")));
    auto *origin = new QLabel;
    origin->setForegroundRole(QPalette::PlaceholderText);
    auto *reset = new QToolButton;
    reset->setText(T("Reset"));
    auto refresh = [this, section, origin, reset] {
        SettingsModel::Value cur;
        SettingsModel::Source src;
        if (!ctx_.settings->value(section, "ENABLED", &cur)) return;
        (void)src;
        const spn::ui::IdentityStrings fallback;
        const spn::ui::StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
        const spn::ui::KeyOrigin o = spn::ui::keyOrigin(*ctx_.settings, section, "ENABLED");
        origin->setText(qs8(spn::ui::originBadgeText(o, strings)));
        reset->setEnabled(o.canReset);
        reset->setToolTip(qs8(spn::ui::resetTooltip(section, o.canReset, strings)));
        reset->setAccessibleName(T("Reset to inherited") + QStringLiteral(" ENABLED"));
    };
    refresh();
    l->addWidget(check, 0, 0, 1, 2);
    auto *what = new QLabel(T("Send scroll, zoom and key events while this profile is active"));
    what->setWordWrap(true);
    what->setForegroundRole(QPalette::PlaceholderText);
    l->addWidget(what, 1, 0, 1, 2);
    origin->setWordWrap(true);
    l->addWidget(origin, 2, 0);
    l->addWidget(reset, 2, 1);
    connect(check, &QCheckBox::toggled, this, [this, section, refresh](bool on) {
        std::string err;
        if (!ctx_.settings->setValue(section, "ENABLED", on ? "1" : "0", &err)) {
            problem(qs8(err));
            return;
        }
        refresh();
        ctx_.saveSoon();
    });
    connect(reset, &QToolButton::clicked, this, [this, section, check, refresh] {
        ctx_.settings->clearOverride(section, "ENABLED");
        SettingsModel::Value cur;
        if (ctx_.settings->value(section, "ENABLED", &cur)) {
            QSignalBlocker b(check);
            check->setChecked(cur.text == "1");
        }
        refresh();
        ctx_.saveSoon();
    });
    return box;
}

QWidget *ProfilesPage::buildBindings(size_t section) {
    const BindingContext ctxKind = bindingContextOf(section);
    const std::vector<BindingRow> allRows = bindingRows(*ctx_.settings, section);
    if (hasAdvancedSection(ctxKind) && anyAdvancedBinding(allRows, ctxKind)) advancedOpen_ = true;
    // Search box: a button row is found by "Button <n>", its key BUTTON_<n> or its action text.
    std::vector<BindingRow> rows;
    for (const BindingRow &r : allRows)
        if (settingMatches(T("Button") + QStringLiteral(" %1 ").arg(r.number) + qs8(r.text),
                           "BUTTON_" + std::to_string(r.number)))
            rows.push_back(r);
    if (rows.empty() && !settingsFilter_->text().isEmpty()) return nullptr;
    settingRows_ += int(rows.size());
    {   // where each action comes from (inheritance made explicit)
        const spn::ui::IdentityStrings fallback;
        const spn::ui::StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
        for (BindingRow &r : rows) {
            const spn::ui::KeyOrigin o = spn::ui::buttonOrigin(*ctx_.settings, section, r.number);
            r.origin = spn::ui::originBadgeText(o, strings);
            if (section == 0) {
                std::vector<std::string> names;
                const std::string hint = spn::ui::overrideHint(*ctx_.settings, "BUTTON_" + std::to_string(r.number), strings, &names);
                if (!hint.empty()) {
                    r.origin = hint;
                    r.originTip = spn::ui::overrideHintTooltip(names, strings);
                }
            }
        }
    }

    auto *box = new QGroupBox(T("Button bindings"));
    auto *outer = new QVBoxLayout(box);
    outer->setContentsMargins(tk::kCardPadding, tk::kCardPadding, tk::kCardPadding, tk::kCardPadding);
    outer->addWidget(wrapLabel(T("Each button can use a button mode, switch the joystick mode or hold the navigation mode "
                                 "(arrow keys instead of scrolling). All buttons are set here, one row each.")));
    auto *grid = new QGridLayout;
    grid->setColumnStretch(2, 1);
    outer->addLayout(grid);

    // Joystick modes offered by "Switch to mode": every installed one (built-in and files).
    const ModeSet installedModes = loadModes(ctx_.modesDir, nullptr);
    const std::vector<std::string> offered = bindingModeIds(installedModes);
    const ButtonModeSet buttonModes = loadButtonModes(ctx_.buttonModesDir, nullptr);

    ButtonRowEnv env;
    env.context = ctxKind;
    env.advancedOpen = !hasAdvancedSection(ctxKind) || advancedOpen_;
    env.buttonModes = &buttonModes;
    env.modeIds = offered;
    env.strings = ctx_.strings;
    {   // the description of every joystick mode is the tooltip of its entry
        const spn::ui::IdentityStrings fallback;
        const spn::ui::StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
        for (const ModeDef &m : installedModes.modes) env.modeTips[m.id] = spn::ui::joystickModeTooltip(m, strings);
    }
    // kMaxButtons: the device is not known, so there is no real count to name
    env.deviceButtons = ctx_.settings->buttonLimit() < int(spn::kMaxButtons) ? ctx_.settings->buttonLimit() : 0;
    env.resetText = section == 0 ? T("Unbind") : T("Inherit");
    env.resetTip = section == 0 ? T("Remove this binding")
                                : T("Reset to inherited: remove this profile's own binding");
    auto changed = [this](bool ok, const std::string &err) {
        if (ok) ctx_.saveSoon();
        else if (!err.empty()) problem(qs8(err));
        queueRebuild();
    };
    int r = 0;
    for (const BindingRow &row : rows) {
        ButtonRowHandlers h;
        h.choose = [this, section, row, offered, changed](const ButtonChoice &c) {
            std::string err;
            const bool ok = applyButtonChoice(*ctx_.settings, section, row, c, offered, &err);
            changed(ok, err);
        };
        h.setArgument = [this, section, row, changed](const Binding &b) {
            std::string err;
            const bool ok = applyBinding(*ctx_.settings, section, row.number, b, &err);
            changed(ok, err);
        };
        h.reset = [this, section, row, changed] {
            ctx_.settings->clearButton(section, row.number);
            changed(true, std::string());
        };
        addButtonRow(this, box, grid, r, row, env, h);
        ++r;
    }

    // Global only: plain key/click bindings behind a collapsed section with a short hint.
    if (hasAdvancedSection(ctxKind)) {
        auto *adv = new QGroupBox(T("Advanced: let dofscroll send keys or clicks"));
        adv->setCheckable(true);
        adv->setChecked(advancedOpen_);
        auto *al = new QVBoxLayout(adv);
        auto *note = wrapLabel(
            T("A mapping that is the same everywhere is usually better set in the vendor driver "
              "(see docs/modes.md, \"Buttons and the vendor driver\"). Bindings that only apply in "
              "a certain joystick mode or profile are a dofscroll feature: set them in that profile."));
        al->addWidget(note);
        note->setVisible(advancedOpen_);
        connect(adv, &QGroupBox::toggled, this, [this](bool on) {
            advancedOpen_ = on;
            queueRebuild();
        });
        outer->addWidget(adv);
    }
    return box;
}

QWidget *ProfilesPage::buildModeInfo(size_t section) {
    const SettingsModel::Visible v = ctx_.settings->visibleSettings(section);
    auto *box = new QGroupBox(qs8(v.mode.label));
    auto *l = new QVBoxLayout(box);
    l->setContentsMargins(tk::kCardPadding, tk::kCardPadding, tk::kCardPadding, tk::kCardPadding);
    if (!v.mode.description.empty()) l->addWidget(wrapLabel(qs8(v.mode.description)));
    auto *form = new QFormLayout;
    form->addRow(T("Vertical scroll:"), new QLabel(qs8(v.mode.v)));
    form->addRow(T("Horizontal scroll:"), new QLabel(qs8(v.mode.h)));
    form->addRow(T("Twist:"), new QLabel(qs8(v.mode.twist)));
    l->addLayout(form);
    // The mode's own bindings (including key and click actions) are shown as they are.
    if (v.mode.buttons.empty()) {
        l->addWidget(new QLabel(T("This joystick mode binds no buttons.")));
    } else {
        l->addWidget(new QLabel(T("Button bindings of this joystick mode:")));
        auto *bf = new QFormLayout;
        for (const auto &b : v.mode.buttons)
            bf->addRow(T("Button") + QStringLiteral(" %1:").arg(b.first), new QLabel(qs8(b.second)));
        l->addLayout(bf);
    }
    l->addWidget(wrapLabel(T("These settings come from the joystick mode file and are read-only here.")));
    auto *edit = new QPushButton(T("Edit joystick mode file"), box);
    edit->setEnabled(v.mode.hasFile);
    edit->setToolTip(v.mode.hasFile ? T("Open the joystick mode file in your default editor")
                                    : T("Built-in joystick modes have no file; duplicate the joystick mode on the Joystick modes "
                                        "page to edit it"));
    connect(edit, &QPushButton::clicked, this, [this, section] {
        std::string err;
        if (!ctx_.settings->editModeFile(section, *ctx_.runner, &err)) problem(qs8(err));
    });
    l->addWidget(edit, 0, Qt::AlignLeft);
    return box;
}

QWidget *ProfilesPage::buildOverrides(size_t section) {
    const SettingsModel::Visible v = ctx_.settings->visibleSettings(section);
    const bool filtering = !settingsFilter_->text().isEmpty();
    auto *box = new QGroupBox(T("Overrides for this profile"));
    auto *outer = new QVBoxLayout(box);
    outer->setContentsMargins(tk::kCardPadding, tk::kCardPadding, tk::kCardPadding, tk::kCardPadding);
    auto *inner = new QWidget(box);
    auto *il = new QVBoxLayout(inner);
    il->setContentsMargins(0, 0, 0, 0);
    int shownRows = 0;
    il->addWidget(wrapLabel(T("The channel values this profile sets itself, as raw text. Everything else is inherited. "
                              "The Channels tab shows the same values with their origin and the controls to change them.")));
    auto *form = new QFormLayout;
    for (const std::string &key : v.overrides.keys) {
        SettingsModel::Value val;
        if (!ctx_.settings->value(section, key, &val)) continue;
        if (!settingMatches(qs8(key), key)) continue;
        ++shownRows;
        auto *row = new QWidget(inner);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto *edit = new QLineEdit(qs8(val.text), row);
        edit->setToolTip(T("Inherited value:") + QLatin1Char(' ') + qs8(val.inherited));
        edit->setAccessibleName(qs8(key));
        auto *hint = new QLabel(row);
        hint->setProperty("error", true);
        hint->setVisible(false);
        auto *rm = new QToolButton(row);
        rm->setText(T("Remove"));
        rm->setToolTip(T("Remove this override and inherit the value again"));
        rm->setAccessibleName(T("Remove") + QLatin1Char(' ') + qs8(key));
        h->addWidget(edit, 1);
        h->addWidget(hint);
        h->addWidget(rm);
        // typing: validated at every change, written after a pause (an invalid text is not written)
        connect(edit, &QLineEdit::textEdited, this, [this, section, key, edit, hint](const QString &t) {
            std::string err;
            const bool ok = ctx_.settings->setValue(section, key, t.toUtf8().toStdString(), &err);
            hint->setText(ok ? QString() : mark(spn::ui::StatusKind::Error, T("Invalid value: ") + qs8(err)));
            hint->setVisible(!ok);
            edit->setProperty("invalid", !ok);
            edit->style()->unpolish(edit);
            edit->style()->polish(edit);
            if (ok) ctx_.saveTyped();
        });
        connect(edit, &QLineEdit::editingFinished, this, [this] { ctx_.saveSoon(); });
        connect(rm, &QToolButton::clicked, this, [this, section, key] {
            ctx_.settings->clearOverride(section, key);
            ctx_.saveSoon();
            queueRebuild();
        });
        form->addRow(qs8(key) + QLatin1Char(':'), row);
    }
    il->addLayout(form);
    if (!v.addableOverrides.empty() && !filtering) {
        auto *h = new QHBoxLayout;
        auto *combo = new QComboBox(inner);
        combo->setAccessibleName(T("Setting to override"));
        for (const std::string &k : v.addableOverrides) combo->addItem(qs8(k), qs8(k));
        auto *add = new QPushButton(T("Add override"), inner);
        h->addWidget(combo, 1);
        h->addWidget(add);
        il->addLayout(h);
        connect(add, &QPushButton::clicked, this, [this, section, combo] {
            std::string err;
            if (!ctx_.settings->addOverride(section, combo->currentData().toString().toUtf8().toStdString(), &err))
                problem(qs8(err));
            else
                ctx_.saveSoon();
            queueRebuild();
        });
    }
    outer->addWidget(inner);
    if (shownRows == 0 && filtering) {
        delete box;
        return nullptr;
    }
    settingRows_ += shownRows;
    return box;
}

void ProfilesPage::addApplication() {
    AddAppDialog dlg(env_, ProfileTreeModel::patterns(*ctx_.settings, Section::APP), this);
    if (dlg.exec() != QDialog::Accepted) return;
    std::string err;
    const size_t idx = ProfileTreeModel::add(*ctx_.settings, Section::APP, dlg.matchString().toUtf8().toStdString(), &err);
    if (idx == ProfileSet::npos) {
        problem(qs8(err));
        return;
    }
    ctx_.saveSoon();
    rebuildTree(idx);
}

void ProfilesPage::addDevice() {
    AddDeviceDialog dlg(env_, ProfileTreeModel::patterns(*ctx_.settings, Section::DEVICE), this);
    if (dlg.exec() != QDialog::Accepted) return;
    std::string err;
    const size_t idx = ProfileTreeModel::add(*ctx_.settings, Section::DEVICE, dlg.deviceName().toUtf8().toStdString(), &err);
    if (idx == ProfileSet::npos) {
        problem(qs8(err));
        return;
    }
    ctx_.saveSoon();
    rebuildTree(idx);
}

// "Change application..." / "Change device...": the same pickers as for a new profile (running programs, capture
// the next focused one, device list, manual entry) choose the new match string; the settings stay.
void ProfilesPage::renameProfile() {
    const size_t s = currentSection();
    if (s == 0) return;
    const Section::Kind kind = ctx_.settings->sections()[s].kind;
    const std::string cur = ctx_.settings->sections()[s].pattern;
    std::vector<std::string> others = ProfileTreeModel::patterns(*ctx_.settings, kind);
    others.erase(std::remove(others.begin(), others.end(), cur), others.end());  // keeping the current one is fine
    QString chosen;
    if (kind == Section::APP) {
        AddAppDialog dlg(env_, others, this, qs8(cur));
        if (dlg.exec() != QDialog::Accepted) return;
        chosen = dlg.matchString();
    } else {
        AddDeviceDialog dlg(env_, others, this, qs8(cur));
        if (dlg.exec() != QDialog::Accepted) return;
        chosen = dlg.deviceName();
    }
    if (chosen.toUtf8().toStdString() == cur) return;
    std::string err;
    if (!ProfileTreeModel::rename(*ctx_.settings, s, chosen.toUtf8().toStdString(), &err)) {
        problem(qs8(err));
        return;
    }
    ctx_.saveSoon();
    rebuildTree(s);
}

void ProfilesPage::removeProfile() {
    const size_t s = currentSection();
    if (s == 0) return;
    const QString name = qs8(ctx_.settings->sections()[s].pattern);
    if (QMessageBox::question(this, T("Remove profile"),
                              T("Remove the profile \"%1\" and all its settings?").arg(name)) != QMessageBox::Yes)
        return;
    std::string err;
    if (!ProfileTreeModel::remove(*ctx_.settings, s, &err)) {
        problem(qs8(err));
        return;
    }
    ctx_.saveSoon();
    rebuildTree(0);
}

bool ProfilesPage::exportSelected(const QString &file) {
    lastError_.clear();
    const size_t s = currentSection();
    const std::string text = profileExportText(*ctx_.settings, s);
    if (text.empty()) {
        lastError_ = T("No such profile.");
        return false;
    }
    std::string err;
    if (!spn::fs::writeFileReplace(file.toStdString(), text, &err)) {
        lastError_ = qs8(err);
        return false;
    }
    return true;
}

void ProfilesPage::exportProfile() {
    const std::string name = profileExportFileName(*ctx_.settings, currentSection());
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    const QString file = QFileDialog::getSaveFileName(this, T("Export profile"), dir + QLatin1Char('/') + qs8(name),
                                                      T("Configuration files (*.conf);;All files (*)"));
    if (file.isEmpty()) return;
    if (!exportSelected(file)) problem(T("Could not export the profile: ") + lastError_);
    else ctx_.report(T("Profile exported"), false);
}
