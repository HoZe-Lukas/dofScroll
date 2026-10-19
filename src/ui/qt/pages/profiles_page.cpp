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
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

#include "add_app_dialog.hpp"
#include "add_device_dialog.hpp"
#include "feed_dir.hpp"
#include "ui/model/binding_editor_model.hpp"
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

// Key chooser: grouped list, group names are disabled header rows.
QComboBox *makeKeyCombo(const QString &current, QWidget *parent) {
    auto *c = new QComboBox(parent);
    c->setMaxVisibleItems(20);
    auto *model = qobject_cast<QStandardItemModel *>(c->model());
    for (const KeyGroup &g : keyGroups()) {
        c->addItem(T(g.title.c_str()));
        if (model) model->item(c->count() - 1)->setFlags(Qt::NoItemFlags);
        for (const std::string &k : g.keys) c->addItem(qs8(k), qs8(k));
    }
    const int i = c->findData(current);
    if (i >= 0) c->setCurrentIndex(i);
    return c;
}
}  // namespace

QWidget *createProfilesPage(const UiContext &ctx) { return new ProfilesPage(ctx); }

ProfilesPage::ProfilesPage(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx) {
    env_.runningApps = [] { return spn::makeRunningAppsProvider(); };
    env_.activeWindow = dofscroll_ui::makeActiveWindowPoll();
    {
        const std::string dir = ctx_.runtimeDir.empty() ? dofscroll_ui::feedRuntimeDir() : ctx_.runtimeDir;
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

    scroll_ = new QScrollArea(split);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    split->addWidget(left);
    split->addWidget(scroll_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({230, 640});
    outer->addWidget(split, 1);

    auto *bar = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    save_ = new QPushButton(T("Save"), this);
    save_->setToolTip(T("Write the configuration and tell the daemon to reload it"));
    bar->addWidget(status_, 1);
    bar->addWidget(save_);
    outer->addLayout(bar);

    setTabOrder(tree_, add_app_);
    setTabOrder(add_app_, add_dev_);
    setTabOrder(add_dev_, rename_);
    setTabOrder(rename_, remove_);
    setTabOrder(remove_, scroll_);
    setTabOrder(scroll_, save_);

    connect(tree_, &QTreeWidget::currentItemChanged, this, [this] { onSelectionChanged(); });
    connect(add_app_, &QPushButton::clicked, this, &ProfilesPage::addApplication);
    connect(add_dev_, &QPushButton::clicked, this, &ProfilesPage::addDevice);
    connect(rename_, &QPushButton::clicked, this, &ProfilesPage::renameProfile);
    connect(remove_, &QPushButton::clicked, this, &ProfilesPage::removeProfile);
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

void ProfilesPage::onSelectionChanged() { rebuildContent(); }

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
    updateButtons();
    refreshDeviceLimit(section);
    const ProfilePageLayout layout = profilePageLayout(*ctx_.settings, section);
    modeSelectorVisible_ = layout.modeSelector;

    auto *content = new QWidget;
    auto *l = new QVBoxLayout(content);
    const QString title = section == 0 ? T("Default mode") : qs8(ctx_.settings->sections()[section].pattern);
    auto *head = new QLabel(QStringLiteral("<b>") + title.toHtmlEscaped() + QStringLiteral("</b>"), content);
    l->addWidget(head);
    if (section == 0)
        l->addWidget(wrapLabel(T("Settings of the default mode. Device and application profiles inherit "
                                 "from it unless they choose another mode or override a value.")));

    if (layout.modeSelector) l->addWidget(buildModeSelector(section));
    if (layout.unknownMode)
        l->addWidget(wrapLabel(T("This profile names a mode that is not installed; it behaves like the "
                                 "default mode until you choose another one.")));
    if (layout.bindingEditor) {
        l->addWidget(buildBindings(section));
        l->addWidget(buildButtonSettings(section));
    }
    if (layout.modeInfo) l->addWidget(buildModeInfo(section));
    if (layout.overridesSection) l->addWidget(buildOverrides(section));
    if (layout.overridesSection) l->addWidget(buildEffective(section));
    l->addStretch(1);

    QWidget *old = scroll_->takeWidget();
    scroll_->setWidget(content);
    if (old) old->deleteLater();
}

QWidget *ProfilesPage::buildModeSelector(size_t section) {
    auto *box = new QGroupBox(T("Mode"));
    auto *l = new QFormLayout(box);
    auto *combo = new QComboBox(box);
    combo->setAccessibleName(T("Mode"));
    combo->setToolTip(T("The mode that applies while this profile is active"));
    const std::vector<SettingsModel::ModeOption> opts = ctx_.settings->modeOptions(section);
    for (size_t i = 0; i < opts.size(); ++i) {
        const SettingsModel::ModeOption &o = opts[i];
        QString text = i == 0 ? T("Default mode") : qs8(o.label);
        if (i > 0 && o.label != o.id) text += QStringLiteral(" (") + qs8(o.id) + QLatin1Char(')');
        if (!o.known) text += QStringLiteral(" - ") + T("not installed");
        combo->addItem(text);
    }
    combo->setCurrentIndex(std::max(0, ctx_.settings->selectedModeOption(section)));
    l->addRow(T("Mode:"), combo);
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
    const std::vector<BindingRow> rows = bindingRows(*ctx_.settings, section);
    if (hasAdvancedSection(ctxKind) && anyAdvancedBinding(rows, ctxKind)) advancedOpen_ = true;

    auto *box = new QGroupBox(T("Button bindings"));
    auto *outer = new QVBoxLayout(box);
    outer->addWidget(wrapLabel(T("Each button can switch the mode or hold the navigation mode (arrow keys "
                                 "instead of scrolling).")));
    auto *grid = new QGridLayout;
    grid->setColumnStretch(2, 1);
    outer->addLayout(grid);

    // Modes offered by "Switch to mode": every installed mode (built-in and files).
    const std::vector<std::string> offered = bindingModeIds(loadModes(ctx_.modesDir, nullptr));

    const bool advanced = !hasAdvancedSection(ctxKind) || advancedOpen_;
    int r = 0;
    for (const BindingRow &row : rows) {
        auto *label = new QLabel(T("Button") + QStringLiteral(" %1").arg(row.number));
        grid->addWidget(label, r, 0);
        if (row.beyondLimit || !row.valid) {
            auto *ro = new QLabel(qs8(row.text) + QStringLiteral("  ") +
                                  (row.beyondLimit ? T("(above this device's button count)")
                                                   : T("(not a valid action)")));
            grid->addWidget(ro, r, 1, 1, 3);
            ++r;
            continue;
        }
        auto *kind = new QComboBox;
        kind->setAccessibleName(T("Action of button") + QStringLiteral(" %1").arg(row.number));
        const std::vector<BindingKindInfo> kinds = bindingKindsFor(ctxKind, advanced);
        int cur = 0;
        for (size_t i = 0; i < kinds.size(); ++i) {
            kind->addItem(T(kinds[i].label), int(kinds[i].kind));
            if (kinds[i].kind == row.binding.kind) cur = int(i);
        }
        kind->setCurrentIndex(cur);
        grid->addWidget(kind, r, 1);

        QWidget *arg = nullptr;
        if (row.binding.kind == BindingKind::Key) {
            auto *kc = makeKeyCombo(qs8(row.binding.arg), box);
            kc->setAccessibleName(T("Key of button") + QStringLiteral(" %1").arg(row.number));
            connect(kc, &QComboBox::activated, this, [this, section, row, kc](int) {
                std::string err;
                Binding b{BindingKind::Key, kc->currentData().toString().toUtf8().toStdString()};
                if (kc->currentData().isValid() && applyBinding(*ctx_.settings, section, row.number, b, &err))
                    setStatus(T("Unsaved changes"));
                else if (!err.empty())
                    setStatus(qs8(err));
                updateButtons();
                queueRebuild();
            });
            arg = kc;
        } else if (row.binding.kind == BindingKind::Mode || row.binding.kind == BindingKind::ToggleMode) {
            auto *mc = new QComboBox(box);
            mc->setAccessibleName(T("Mode of button") + QStringLiteral(" %1").arg(row.number));
            std::vector<std::string> ids = offered;
            if (std::find(ids.begin(), ids.end(), row.binding.arg) == ids.end()) ids.push_back(row.binding.arg);
            for (const std::string &id : ids) mc->addItem(qs8(id), qs8(id));
            mc->setCurrentIndex(std::max(0, mc->findData(qs8(row.binding.arg))));
            connect(mc, &QComboBox::activated, this, [this, section, row, mc](int) {
                std::string err;
                Binding b{row.binding.kind, mc->currentData().toString().toUtf8().toStdString()};
                if (applyBinding(*ctx_.settings, section, row.number, b, &err))
                    setStatus(T("Unsaved changes"));
                else
                    setStatus(qs8(err));
                updateButtons();
                queueRebuild();
            });
            arg = mc;
        }
        if (arg) grid->addWidget(arg, r, 2);

        connect(kind, &QComboBox::activated, this, [this, section, row, kind, kinds, offered](int idx) {
            if (idx < 0 || size_t(idx) >= kinds.size()) return;
            const Binding b = withKind(row.binding, kinds[size_t(idx)].kind, offered);
            std::string err;
            if (applyBinding(*ctx_.settings, section, row.number, b, &err))
                setStatus(T("Unsaved changes"));
            else
                setStatus(qs8(err));
            updateButtons();
            queueRebuild();
        });

        if (row.overridden) {
            auto *reset = new QToolButton;
            reset->setText(section == 0 ? T("Unbind") : T("Inherit"));
            reset->setToolTip(section == 0 ? T("Remove this binding")
                                           : T("Remove this profile's own binding and use the inherited one"));
            connect(reset, &QToolButton::clicked, this, [this, section, row] {
                ctx_.settings->clearButton(section, row.number);
                setStatus(T("Unsaved changes"));
                updateButtons();
                queueRebuild();
            });
            grid->addWidget(reset, r, 3);
        }
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
    return box;
}

QWidget *ProfilesPage::buildOverrides(size_t section) {
    const SettingsModel::Visible v = ctx_.settings->visibleSettings(section);
    auto *box = new QGroupBox(T("Overrides for this profile"));
    box->setCheckable(true);
    box->setChecked(!v.overrides.keys.empty());  // off by default; the Channels page is the normal place
    auto *outer = new QVBoxLayout(box);
    auto *inner = new QWidget(box);
    auto *il = new QVBoxLayout(inner);
    il->setContentsMargins(0, 0, 0, 0);
    il->addWidget(wrapLabel(T("Channel values that only apply to this profile. Everything else is "
                              "inherited; change shared values on the Channels page.")));
    auto *form = new QFormLayout;
    for (const std::string &key : v.overrides.keys) {
        SettingsModel::Value val;
        if (!ctx_.settings->value(section, key, &val)) continue;
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
    if (!v.addableOverrides.empty()) {
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
    inner->setVisible(box->isChecked());
    connect(box, &QGroupBox::toggled, inner, &QWidget::setVisible);
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
