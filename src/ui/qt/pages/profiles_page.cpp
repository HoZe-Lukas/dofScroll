// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "profiles_page.hpp"

#include <algorithm>
#include <cstdlib>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QCloseEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QTabBar>
#include <QTabWidget>
#include <QStackedWidget>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>
#include <QHideEvent>

#include "add_app_dialog.hpp"
#include "add_device_dialog.hpp"
#include "button_row.hpp"
#include "key_combo.hpp"
#include "ui/model/binding_editor_model.hpp"
#include "ui/model/button_choice_model.hpp"
#include <QStandardItem>
#include "ui/model/effective_model.hpp"
#include "ui/model/key_list.hpp"
#include "ui/model/profile_tree_model.hpp"
#include "platform_hooks.hpp"
#include "ui_i18n.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
enum { kRoleSection = Qt::UserRole };  // section index, -1 for group headers


QLabel *wrapLabel(const QString &text, QWidget *parent = nullptr) {
    auto *l = new QLabel(text, parent);
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

}  // namespace

QWidget *createProfilesPage(const UiContext &ctx) { return new ProfilesPage(ctx); }

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
    auto *split = new QSplitter(Qt::Horizontal, this);

    auto *left = new QWidget(split);
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    profileFilter_ = new QLineEdit(left);
    profileFilter_->setClearButtonEnabled(true);
    profileFilter_->setPlaceholderText(T("Search profiles..."));
    profileFilter_->setAccessibleName(T("Search profiles"));
    profileFilter_->setToolTip(T("Show only the programs and devices whose name contains this text"));
    ll->addWidget(profileFilter_);
    tree_ = new QTreeWidget(left);
    tree_->setHeaderHidden(true);
    tree_->setAccessibleName(T("Profiles"));
    ll->addWidget(tree_, 1);
    add_app_ = new QPushButton(T("Add application..."), left);
    add_app_->setToolTip(T("Create a profile for a program, matched by its window class or name"));
    add_dev_ = new QPushButton(T("Add device..."), left);
    add_dev_->setToolTip(T("Create a profile for a connected device"));
    rename_ = new QPushButton(T("Rename..."), left);
    rename_->setToolTip(T("Change the match string of this profile"));
    remove_ = new QPushButton(T("Remove"), left);
    remove_->setToolTip(T("Delete this profile and its settings"));
    for (QPushButton *b : {add_app_, add_dev_, rename_, remove_}) ll->addWidget(b);

    // Right side: the search box of the settings and the sub-tabs of the selected profile. Every
    // tab scrolls on its own; the buttons below the split stay visible.
    auto *right = new QWidget(split);
    auto *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
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
    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({230, 640});
    outer->addWidget(split, 1);

    auto *bar = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    revert_ = new QPushButton(T("Revert"), this);
    revert_->setToolTip(T("Discard the unsaved changes of this profile and load the saved values"));
    save_ = new QPushButton(T("Save"), this);
    save_->setToolTip(T("Write the configuration and tell the daemon to reload it"));
    bar->addWidget(status_, 1);
    bar->addWidget(revert_);
    bar->addWidget(save_);
    outer->addLayout(bar);

    setTabOrder(profileFilter_, tree_);
    setTabOrder(tree_, add_app_);
    setTabOrder(add_app_, add_dev_);
    setTabOrder(add_dev_, rename_);
    setTabOrder(rename_, remove_);
    setTabOrder(remove_, settingsFilter_);
    setTabOrder(settingsFilter_, tabs_);
    setTabOrder(tabs_, revert_);
    setTabOrder(revert_, save_);

    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *cur, QTreeWidgetItem *prev) { onSelectionChanged(cur, prev); });
    connect(profileFilter_, &QLineEdit::textChanged, this, [this] { applyProfileFilter(); });
    connect(settingsFilter_, &QLineEdit::textChanged, this, [this] { queueRebuild(); });
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int i) {
        if (i >= 0 && tabs_->widget(i)) lastTab_ = tabs_->widget(i)->property("tabId").toString();
    });
    connect(add_app_, &QPushButton::clicked, this, &ProfilesPage::addApplication);
    connect(add_dev_, &QPushButton::clicked, this, &ProfilesPage::addDevice);
    connect(rename_, &QPushButton::clicked, this, &ProfilesPage::renameProfile);
    connect(remove_, &QPushButton::clicked, this, &ProfilesPage::removeProfile);
    connect(revert_, &QPushButton::clicked, this, &ProfilesPage::revert);
    connect(save_, &QPushButton::clicked, this, &ProfilesPage::save);

    rebuildTree(0);
}

int ProfilesPage::profileCount() const {
    return int(ProfileTreeModel::entries(*ctx_.settings).size()) - 1;
}
bool ProfilesPage::modeSelectorVisible() const { return modeSelectorVisible_; }

size_t ProfilesPage::currentSection() const {
    const QTreeWidgetItem *it = tree_->currentItem();
    const int s = it ? it->data(0, kRoleSection).toInt() : 0;
    return s < 0 ? 0 : size_t(s);
}

void ProfilesPage::setStatus(const QString &text) { status_->setText(text); }

void ProfilesPage::updateButtons() {
    const size_t s = currentSection();
    rename_->setEnabled(s != 0);
    remove_->setEnabled(s != 0);
    save_->setEnabled(ctx_.settings->dirty());
    revert_->setEnabled(ctx_.settings->sectionModified(s));
}

bool ProfilesPage::selectedModified() const { return ctx_.settings->sectionModified(currentSection()); }
bool ProfilesPage::revertEnabled() const { return revert_->isEnabled(); }
bool ProfilesPage::saveEnabled() const { return save_->isEnabled(); }

QString ProfilesPage::shownName() const {
    return shown_ == 0 ? T("Default mode") : qs8(ctx_.settings->sections()[shown_].pattern);
}

void ProfilesPage::rebuildTree(size_t selectSection) {
    QSignalBlocker block(tree_);
    tree_->clear();
    QTreeWidgetItem *dev = nullptr, *app = nullptr, *select = nullptr;
    for (const ProfileEntry &e : ProfileTreeModel::entries(*ctx_.settings)) {
        QTreeWidgetItem *it;
        if (e.isDefault) {
            it = new QTreeWidgetItem(tree_, QStringList(T("Default mode")));
        } else {
            QTreeWidgetItem *&group = e.kind == Section::DEVICE ? dev : app;
            if (!group) {
                group = new QTreeWidgetItem(tree_, QStringList(T(ProfileTreeModel::groupTitle(e.kind))));
                group->setData(0, kRoleSection, -1);
                group->setFlags(Qt::ItemIsEnabled);
                group->setExpanded(true);
            }
            it = new QTreeWidgetItem(group, QStringList(qs8(e.pattern)));
        }
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
            tree_->setCurrentItem(*it);  // onSelectionChanged rebuilds the content
            return true;
        }
    }
    return false;
}

// Selects the profile of a kind and name after a revert changed the indices (default mode if gone).
void ProfilesPage::rebuildTreeByName(Section::Kind kind, const std::string &pattern) {
    const size_t idx = kind == Section::GLOBAL ? 0 : ctx_.settings->profiles().find(kind, pattern);
    rebuildTree(idx == ProfileSet::npos ? 0 : idx);
}

// Search box of the list: hides the profiles whose name does not contain the text, and a group
// header without a visible profile. The default mode is always shown.
void ProfilesPage::applyProfileFilter() {
    const std::string text = profileFilter_->text().toUtf8().toStdString();
    std::vector<size_t> keep;
    for (const ProfileEntry &e : ProfileTreeModel::filter(ProfileTreeModel::entries(*ctx_.settings), text))
        keep.push_back(e.section);
    for (int g = 0; g < tree_->topLevelItemCount(); ++g) {
        QTreeWidgetItem *top = tree_->topLevelItem(g);
        if (top->data(0, kRoleSection).toInt() >= 0) continue;  // the default mode
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
    rebuildContent();  // at once, not queued: callers read the rows
}

// Kind and name of the profile of a section (the section numbers change when a profile is
// removed or restored, the name does not).
static void profileIdentity(const SettingsModel &m, size_t section, Section::Kind *kind, std::string *pattern) {
    *kind = Section::GLOBAL;
    pattern->clear();
    if (section == 0 || section >= m.sectionCount()) return;
    const SettingsModel::SectionInfo info = m.sections()[section];
    *kind = info.kind;
    *pattern = info.pattern;
}

// Another profile was picked. Unsaved edits of the profile that is left are asked about first;
// Cancel keeps it selected.
void ProfilesPage::onSelectionChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous) {
    const size_t target = current ? size_t(std::max(0, current->data(0, kRoleSection).toInt())) : 0;
    if (!leaving_ && target != shown_ && shown_ < ctx_.settings->sectionCount() && ctx_.settings->sectionModified(shown_)) {
        Section::Kind kind;
        std::string pattern;
        profileIdentity(*ctx_.settings, target, &kind, &pattern);
        leaving_ = true;
        const bool go = resolveUnsaved(false);
        leaving_ = false;
        if (!go) {
            QSignalBlocker block(tree_);
            tree_->setCurrentItem(previous);
            return;
        }
        rebuildTreeByName(kind, pattern);  // a Discard can renumber the profiles
        return;
    }
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

// Revert button: the unsaved edits of the selected profile are dropped.
bool ProfilesPage::revertSelected() {
    const size_t s = currentSection();
    if (!ctx_.settings->sectionModified(s)) return false;
    Section::Kind kind;
    std::string pattern;
    profileIdentity(*ctx_.settings, s, &kind, &pattern);
    bool removed = false;
    ctx_.settings->revertSection(s, &removed);
    if (removed) {
        kind = Section::GLOBAL;
        pattern.clear();
    } else if (s > 0) {
        // a restored rename has its saved name again
        pattern = ctx_.settings->sections()[s].pattern;
    }
    rebuildTreeByName(kind, pattern);
    setStatus(ctx_.settings->dirty() ? T("Changes of this profile discarded.")
                                     : T("Changes discarded. Nothing is unsaved."));
    updateButtons();
    return true;
}

void ProfilesPage::revert() { revertSelected(); }

// The guard: true if the caller may continue (nothing unsaved, saved, or discarded). After a
// Discard of a profile the caller rebuilds the tree (the profiles can be renumbered).
bool ProfilesPage::resolveUnsaved(bool wholeFile) {
    SettingsModel &m = *ctx_.settings;
    const bool modified = wholeFile ? m.dirty() : (shown_ < m.sectionCount() && m.sectionModified(shown_));
    if (!modified) return true;
    const QString what = wholeFile ? QString() : shownName();
    UnsavedChoice c = UnsavedChoice::Cancel;
    if (prompt_) {
        c = prompt_(what);
    } else {
        QMessageBox box(QMessageBox::Question, T("Unsaved changes"),
                        what.isEmpty() ? T("There are unsaved changes. Save them?")
                                       : T("The profile \"%1\" has unsaved changes. Save them?").arg(what),
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, this);
        box.setDefaultButton(QMessageBox::Save);
        const int r = box.exec();
        c = r == QMessageBox::Save ? UnsavedChoice::Save
            : r == QMessageBox::Discard ? UnsavedChoice::Discard : UnsavedChoice::Cancel;
    }
    if (c == UnsavedChoice::Save) {
        save();
        return wholeFile ? !m.dirty() : !m.sectionModified(shown_);
    }
    if (c == UnsavedChoice::Discard) {
        if (wholeFile) {
            Section::Kind kind;
            std::string pattern;
            profileIdentity(m, shown_, &kind, &pattern);
            m.revertAll();
            rebuildTreeByName(kind, pattern);  // the window may be shown again later
        } else {
            m.revertSection(shown_);
        }
        updateButtons();
        return true;
    }
    return false;
}

// The window closes: ask once for the whole configuration (the edits live in one model).
void ProfilesPage::showEvent(QShowEvent *e) {
    QWidget::showEvent(e);
    QWidget *w = window();
    if (w && w != filtered_) {
        if (filtered_) filtered_->removeEventFilter(this);
        w->installEventFilter(this);
        filtered_ = w;
    }
}

bool ProfilesPage::eventFilter(QObject *obj, QEvent *e) {
    if (obj == filtered_ && e->type() == QEvent::Close && !leaving_) {
        leaving_ = true;
        const bool go = resolveUnsaved(true);
        leaving_ = false;
        if (!go) {
            e->ignore();
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}

// Another page was shown while this profile has unsaved edits: ask; Cancel comes back here.
void ProfilesPage::hideEvent(QHideEvent *e) {
    QWidget::hideEvent(e);
    if (leaving_ || e->spontaneous()) return;
    QWidget *w = window();
    if (!w || !w->isVisible() || w->isMinimized()) return;
    if (shown_ >= ctx_.settings->sectionCount() || !ctx_.settings->sectionModified(shown_)) return;
    QTimer::singleShot(0, this, [this] { guardLeave(); });
}

void ProfilesPage::guardLeave() {
    if (leaving_ || isVisible() || !window()->isVisible()) return;
    if (shown_ >= ctx_.settings->sectionCount() || !ctx_.settings->sectionModified(shown_)) return;
    leaving_ = true;
    Section::Kind kind;
    std::string pattern;
    profileIdentity(*ctx_.settings, shown_, &kind, &pattern);
    if (resolveUnsaved(false))
        rebuildTreeByName(kind, pattern);
    else
        returnToPage();
    leaving_ = false;
}

// Makes this page the current one again: its tab and its category.
void ProfilesPage::returnToPage() {
    QWidget *child = this;
    for (QWidget *p = parentWidget(); p; child = p, p = p->parentWidget()) {
        auto *st = qobject_cast<QStackedWidget *>(p);
        if (!st) continue;
        if (auto *tw = qobject_cast<QTabWidget *>(st->parentWidget())) {
            tw->setCurrentWidget(child);
            continue;
        }
        st->setCurrentWidget(child);
        const QList<QTabBar *> bars = window()->findChildren<QTabBar *>();
        for (QTabBar *b : bars)
            if (b->accessibleName() == T("Categories")) b->setCurrentIndex(st->indexOf(child));
    }
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

void ProfilesPage::rebuildContent() {
    const size_t section = currentSection();
    shown_ = section;
    updateButtons();
    refreshDeviceLimit(section);
    const ProfilePageLayout layout = profilePageLayout(*ctx_.settings, section);
    modeSelectorVisible_ = layout.modeSelector;
    settingRows_ = 0;
    const bool filtering = !settingsFilter_->text().isEmpty();

    // Keep the scroll position of the tab when the same profile is rebuilt after an edit.
    int scrollPos = 0;
    if (auto *cur = qobject_cast<QScrollArea *>(tabs_->currentWidget()))
        if (builtSection_ == section) scrollPos = cur->verticalScrollBar()->value();
    builtSection_ = section;

    QSignalBlocker block(tabs_);
    while (tabs_->count() > 0) {
        QWidget *w = tabs_->widget(0);
        tabs_->removeTab(0);
        w->deleteLater();
    }

    // One scrolling page per sub-tab; the buttons below the tabs are not part of it.
    auto addTab = [this](const char *id, const QString &title, const QList<QWidget *> &parts) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setProperty("tabId", QString::fromLatin1(id));
        auto *content = new QWidget;
        auto *l = new QVBoxLayout(content);
        for (QWidget *w : parts)
            if (w) l->addWidget(w);
        l->addStretch(1);
        scroll->setWidget(content);
        tabs_->addTab(scroll, title);
    };
    // A tab whose rows are all filtered out says so.
    auto noMatch = [filtering](const QList<QWidget *> &parts) {
        QList<QWidget *> out = parts;
        bool any = false;
        for (QWidget *w : parts) any = any || w;
        if (!any && filtering) out << wrapLabel(T("No setting of this tab matches the search."));
        return out;
    };

    // General: what the profile is, its mode.
    const QString title = section == 0 ? T("Default mode") : qs8(ctx_.settings->sections()[section].pattern);
    QList<QWidget *> general;
    general << new QLabel(QStringLiteral("<b>") + title.toHtmlEscaped() + QStringLiteral("</b>"));
    if (section == 0)
        general << wrapLabel(T("Settings of the default mode. Device and application profiles inherit "
                               "from it unless they choose another mode or override a value."));
    if (layout.modeSelector) general << buildModeSelector(section);
    if (layout.unknownMode)
        general << wrapLabel(T("This profile names a mode that is not installed; it behaves like the "
                               "default mode until you choose another one."));
    if (layout.modeInfo) general << buildModeInfo(section);
    addTab("general", T("General"), general);

    // Channels: the value of every axis setting of the profile and where it comes from.
    if (layout.overridesSection) addTab("channels", T("Channels"), noMatch({buildEffective(section)}));

    // Buttons: bindings and the navigation/click buttons (not for a mode with its own bindings).
    if (layout.bindingEditor)
        addTab("buttons", T("Buttons"), noMatch({buildBindings(section), buildButtonSettings(section)}));

    // Overrides: channel values that only apply to this profile.
    if (layout.overridesSection) addTab("overrides", T("Overrides"), noMatch({buildOverrides(section)}));

    int idx = 0;
    for (int i = 0; i < tabs_->count(); ++i)
        if (tabs_->widget(i)->property("tabId").toString() == lastTab_) idx = i;
    tabs_->setCurrentIndex(idx);
    block.unblock();
    if (scrollPos > 0)
        if (auto *cur = qobject_cast<QScrollArea *>(tabs_->currentWidget()))
            QTimer::singleShot(0, cur, [cur, scrollPos] { cur->verticalScrollBar()->setValue(scrollPos); });
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
    auto *l = new QFormLayout(box);
    auto *combo = new QComboBox(box);
    combo->setAccessibleName(T("Joystick mode"));
    combo->setToolTip(T("The joystick mode (what the axes do) that applies while this profile is active"));
    const std::vector<SettingsModel::ModeOption> opts = ctx_.settings->modeOptions(section);
    for (size_t i = 0; i < opts.size(); ++i) {
        const SettingsModel::ModeOption &o = opts[i];
        QString text = i == 0 ? T("Default mode") : qs8(o.label);
        if (i > 0 && o.label != o.id) text += QStringLiteral(" (") + qs8(o.id) + QLatin1Char(')');
        if (!o.known) text += QStringLiteral(" - ") + T("not installed");
        combo->addItem(text);
    }
    combo->setCurrentIndex(std::max(0, ctx_.settings->selectedModeOption(section)));
    l->addRow(T("Joystick mode:"), combo);
    connect(combo, &QComboBox::activated, this, [this, section](int idx) {
        std::string err;
        bool changed = false;
        if (!ctx_.settings->setMode(section, idx, &err, &changed))
            setStatus(qs8(err));
        else
            setStatus(T("Unsaved changes"));
        updateButtons();
        queueRebuild();
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

    auto *box = new QGroupBox(T("Button bindings"));
    auto *outer = new QVBoxLayout(box);
    outer->addWidget(wrapLabel(T("Each button can use a button mode, switch the mode or hold the navigation mode "
                                 "(arrow keys instead of scrolling).")));
    auto *grid = new QGridLayout;
    grid->setColumnStretch(2, 1);
    outer->addLayout(grid);

    // Modes offered by "Switch to mode": every installed mode (built-in and files).
    const std::vector<std::string> offered = bindingModeIds(loadModes(ctx_.modesDir, nullptr));
    const ButtonModeSet buttonModes = loadButtonModes(ctx_.buttonModesDir, nullptr);

    ButtonRowEnv env;
    env.context = ctxKind;
    env.advancedOpen = !hasAdvancedSection(ctxKind) || advancedOpen_;
    env.buttonModes = &buttonModes;
    env.modeIds = offered;
    env.strings = ctx_.strings;
    env.resetText = section == 0 ? T("Unbind") : T("Inherit");
    env.resetTip = section == 0 ? T("Remove this binding")
                                : T("Remove this profile's own binding and use the inherited one");
    auto changed = [this](bool ok, const std::string &err) {
        if (ok) setStatus(T("Unsaved changes"));
        else if (!err.empty()) setStatus(qs8(err));
        updateButtons();
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

    // Default mode only: plain key/click bindings behind a collapsed section with a short hint.
    if (hasAdvancedSection(ctxKind)) {
        auto *adv = new QGroupBox(T("Advanced: let dofscroll send keys or clicks"));
        adv->setCheckable(true);
        adv->setChecked(advancedOpen_);
        auto *al = new QVBoxLayout(adv);
        auto *note = wrapLabel(
            T("A mapping that is the same everywhere is usually better set in the vendor driver "
              "(see docs/modes.md, \"Buttons and the vendor driver\"). Bindings that only apply in "
              "a certain mode or profile are a dofscroll feature: set them in that profile."));
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

QWidget *ProfilesPage::buildButtonSettings(size_t section) {
    auto *box = new QGroupBox(T("Navigation and click buttons"));
    auto *form = new QFormLayout(box);
    int shownRows = 0;
    const struct {
        const char *enabledKey, *numberKey, *label, *tip;
    } rows[] = {
        {"NAV_BUTTON_ENABLED", "NAV_BUTTON", "Navigation button",
         "Hold this button to send arrow keys instead of scrolling"},
        {"CLICK_BUTTON_ENABLED", "CLICK_BUTTON", "Click button", "Press this button to click the left mouse button"}};
    for (const auto &r : rows) {
        SettingsModel::Value en, num;
        if (!ctx_.settings->value(section, r.enabledKey, &en) || !ctx_.settings->value(section, r.numberKey, &num))
            continue;
        if (!settingMatches(T(r.label), r.numberKey)) continue;
        ++shownRows;
        auto *row = new QWidget(box);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto *check = new QCheckBox(T("Enabled"), row);
        check->setChecked(en.text == "1");
        auto *spin = new QSpinBox(row);
        spin->setRange(0, ctx_.settings->buttonLimit());
        spin->setValue(num.text.empty() ? 0 : std::atoi(num.text.c_str()));
        spin->setToolTip(T(r.tip));
        spin->setAccessibleName(T(r.label));
        h->addWidget(check);
        h->addWidget(new QLabel(T("Button number:")));
        h->addWidget(spin);
        h->addStretch(1);
        const std::string ek = r.enabledKey, nk = r.numberKey;
        connect(check, &QCheckBox::toggled, this, [this, section, ek](bool on) {
            std::string err;
            if (!ctx_.settings->setValue(section, ek, on ? "1" : "0", &err))
                setStatus(qs8(err));
            else
                setStatus(T("Unsaved changes"));
            updateButtons();
        });
        connect(spin, &QSpinBox::editingFinished, this, [this, section, nk, spin] {
            std::string err;
            if (!ctx_.settings->setValue(section, nk, std::to_string(spin->value()), &err))
                setStatus(qs8(err));
            else
                setStatus(T("Unsaved changes"));
            updateButtons();
        });
        form->addRow(T(r.label) + QLatin1Char(':'), row);
    }
    if (shownRows == 0) {
        delete box;
        return nullptr;
    }
    settingRows_ += shownRows;
    return box;
}

QWidget *ProfilesPage::buildModeInfo(size_t section) {
    const SettingsModel::Visible v = ctx_.settings->visibleSettings(section);
    auto *box = new QGroupBox(qs8(v.mode.label));
    auto *l = new QVBoxLayout(box);
    if (!v.mode.description.empty()) l->addWidget(wrapLabel(qs8(v.mode.description)));
    auto *form = new QFormLayout;
    form->addRow(T("Vertical scroll:"), new QLabel(qs8(v.mode.v)));
    form->addRow(T("Horizontal scroll:"), new QLabel(qs8(v.mode.h)));
    form->addRow(T("Twist:"), new QLabel(qs8(v.mode.twist)));
    l->addLayout(form);
    // The mode's own bindings (including key and click actions) are shown as they are.
    if (v.mode.buttons.empty()) {
        l->addWidget(new QLabel(T("This mode binds no buttons.")));
    } else {
        l->addWidget(new QLabel(T("Button bindings of this mode:")));
        auto *bf = new QFormLayout;
        for (const auto &b : v.mode.buttons)
            bf->addRow(T("Button") + QStringLiteral(" %1:").arg(b.first), new QLabel(qs8(b.second)));
        l->addLayout(bf);
    }
    l->addWidget(wrapLabel(T("These settings come from the mode file and are read-only here.")));
    auto *edit = new QPushButton(T("Edit mode file"), box);
    edit->setEnabled(v.mode.hasFile);
    edit->setToolTip(v.mode.hasFile ? T("Open the mode file in your default editor")
                                    : T("Built-in modes have no file; duplicate the mode on the Modes page to edit it"));
    connect(edit, &QPushButton::clicked, this, [this, section] {
        std::string err;
        if (!ctx_.settings->editModeFile(section, *ctx_.runner, &err)) setStatus(qs8(err));
    });
    l->addWidget(edit, 0, Qt::AlignLeft);
    return box;
}

QWidget *ProfilesPage::buildEffective(size_t section) {
    auto *box = new QGroupBox(T("Effective values"));
    auto *outer = new QVBoxLayout(box);
    outer->addWidget(wrapLabel(T("The value that applies in this profile and where it comes from.")));
    auto *grid = new QGridLayout;
    grid->setColumnStretch(2, 1);
    int row = 0;
    for (const EffectiveRow &r : effectiveRows(*ctx_.settings, section, *ctx_.strings)) {
        if (!settingMatches(qs8(r.key), r.key)) continue;
        ++settingRows_;
        auto *key = new QLabel(qs8(r.key), box);
        auto *val = new QLabel(qs8(r.value), box);
        auto *src = new QLabel(qs8(r.sourceText), box);
        key->setAccessibleName(qs8(r.key));
        val->setAccessibleName(qs8(r.key) + QStringLiteral(" = ") + qs8(r.value));
        src->setAccessibleName(qs8(r.key) + QStringLiteral(": ") + qs8(r.sourceText));
        grid->addWidget(key, row, 0);
        grid->addWidget(val, row, 1);
        grid->addWidget(src, row, 2);
        if (r.canReset) {
            auto *reset = new QToolButton(box);
            reset->setText(T("Reset"));
            reset->setToolTip(T("Remove the value of this profile and inherit it again"));
            reset->setAccessibleName(T("Reset") + QLatin1Char(' ') + qs8(r.key));
            const std::string k = r.key;
            connect(reset, &QToolButton::clicked, this, [this, section, k] {
                ctx_.settings->clearOverride(section, k);
                setStatus(T("Unsaved changes"));
                updateButtons();
                queueRebuild();
            });
            grid->addWidget(reset, row, 3);
        }
        ++row;
    }
    outer->addLayout(grid);
    if (row == 0 && !settingsFilter_->text().isEmpty()) {
        delete box;
        return nullptr;
    }
    return box;
}

QWidget *ProfilesPage::buildOverrides(size_t section) {
    const SettingsModel::Visible v = ctx_.settings->visibleSettings(section);
    const bool filtering = !settingsFilter_->text().isEmpty();
    auto *box = new QGroupBox(T("Overrides for this profile"));
    auto *outer = new QVBoxLayout(box);
    auto *inner = new QWidget(box);
    auto *il = new QVBoxLayout(inner);
    il->setContentsMargins(0, 0, 0, 0);
    int shownRows = 0;
    il->addWidget(wrapLabel(T("Channel values that only apply to this profile. Everything else is "
                              "inherited; change shared values on the Channels page.")));
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
        auto *rm = new QToolButton(row);
        rm->setText(T("Remove"));
        rm->setToolTip(T("Remove this override and inherit the value again"));
        h->addWidget(edit, 1);
        h->addWidget(rm);
        connect(edit, &QLineEdit::editingFinished, this, [this, section, key, edit] {
            std::string err;
            SettingsModel::Value cur;
            if (ctx_.settings->value(section, key, &cur) && cur.text == edit->text().toUtf8().toStdString()) return;
            if (!ctx_.settings->setValue(section, key, edit->text().toUtf8().toStdString(), &err)) {
                setStatus(qs8(key) + QStringLiteral(": ") + qs8(err));
                if (ctx_.settings->value(section, key, &cur)) edit->setText(qs8(cur.text));
            } else {
                setStatus(T("Unsaved changes"));
            }
            updateButtons();
        });
        connect(rm, &QToolButton::clicked, this, [this, section, key] {
            ctx_.settings->clearOverride(section, key);
            setStatus(T("Unsaved changes"));
            updateButtons();
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
                setStatus(qs8(err));
            else
                setStatus(T("Unsaved changes"));
            updateButtons();
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
        setStatus(qs8(err));
        return;
    }
    setStatus(T("Unsaved changes"));
    rebuildTree(idx);
}

void ProfilesPage::addDevice() {
    AddDeviceDialog dlg(env_, ProfileTreeModel::patterns(*ctx_.settings, Section::DEVICE), this);
    if (dlg.exec() != QDialog::Accepted) return;
    std::string err;
    const size_t idx = ProfileTreeModel::add(*ctx_.settings, Section::DEVICE, dlg.deviceName().toUtf8().toStdString(), &err);
    if (idx == ProfileSet::npos) {
        setStatus(qs8(err));
        return;
    }
    setStatus(T("Unsaved changes"));
    rebuildTree(idx);
}

void ProfilesPage::renameProfile() {
    const size_t s = currentSection();
    if (s == 0) return;
    const std::string cur = ctx_.settings->sections()[s].pattern;
    bool ok = false;
    const QString text = QInputDialog::getText(this, T("Rename profile"), T("Match string:"), QLineEdit::Normal,
                                               qs8(cur), &ok);
    if (!ok) return;
    std::string err;
    if (!ProfileTreeModel::rename(*ctx_.settings, s, text.toUtf8().toStdString(), &err)) {
        setStatus(qs8(err));
        return;
    }
    setStatus(T("Unsaved changes"));
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
        setStatus(qs8(err));
        return;
    }
    setStatus(T("Unsaved changes"));
    rebuildTree(0);
}

void ProfilesPage::save() {
    const SettingsModel::SaveResult r = ctx_.settings->save(*ctx_.strings);
    if (!r.saved)
        setStatus(T("Not saved:") + QLatin1Char(' ') + qs8(r.error));
    else if (!r.reloaded)
        setStatus(T("Saved. The daemon was not notified:") + QLatin1Char(' ') + qs8(r.error));
    else
        setStatus(T("Saved."));
    updateButtons();
}
