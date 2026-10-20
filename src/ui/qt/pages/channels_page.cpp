// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "channels_page.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSlider>
#include <QStringList>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "ui/model/channels_page_model.hpp"
#include "ui/model/curve_preview.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/ui_filter.hpp"
#include "ui_i18n.hpp"

namespace {

using spn::ui::SettingsModel;

constexpr double kPreviewMaxInput = 350.0;

// Response curve of one channel (computeCurvePreview): x = deflection after the sensor scale,
// y = steps per second. Only palette colors, so it follows the light/dark theme.
class CurveWidget : public QWidget {
public:
    explicit CurveWidget(QWidget *parent) : QWidget(parent) {
        setMinimumHeight(120);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setToolTip(T("Preview: speed in steps per second for a deflection of the cap"));
        setAccessibleName(T("Curve preview"));
    }
    void setChannel(const spn::ChannelCfg &c) {
        pts_ = spn::ui::computeCurvePreview(c, 64, kPreviewMaxInput);
        maxSpeed_ = spn::ui::previewMaxSpeed(pts_);
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QPalette &pal = palette();
        const QRectF r = QRectF(rect()).adjusted(8, 8, -8, -20);
        p.fillRect(rect(), pal.color(QPalette::Base));
        p.setPen(QPen(pal.color(QPalette::Mid), 1));
        for (int i = 0; i <= 4; ++i) {
            const double y = r.top() + r.height() * i / 4.0;
            p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        }
        p.drawRect(r);
        const double top = maxSpeed_ > 1.0 ? maxSpeed_ : 1.0;
        if (pts_.size() >= 2) {
            QPainterPath path;
            for (size_t i = 0; i < pts_.size(); ++i) {
                const QPointF q(r.left() + r.width() * pts_[i].input / kPreviewMaxInput,
                                r.bottom() - r.height() * pts_[i].speed / top);
                if (i == 0) path.moveTo(q);
                else path.lineTo(q);
            }
            p.setPen(QPen(pal.color(QPalette::Highlight), 2));
            p.drawPath(path);
        }
        p.setPen(pal.color(QPalette::Text));
        p.drawText(QPointF(r.left() + 4, r.top() + p.fontMetrics().ascent()),
                   T("%1 steps/s").arg(top, 0, 'f', 1));
        p.drawText(QRectF(r.left(), r.bottom() + 2, r.width(), 16), Qt::AlignRight,
                   QString::number(int(kPreviewMaxInput)));
        p.drawText(QRectF(r.left(), r.bottom() + 2, r.width(), 16), Qt::AlignLeft, QStringLiteral("0"));
    }

private:
    std::vector<spn::ui::CurvePoint> pts_;
    double maxSpeed_ = 0;
};

// Wheel events over an editor go to the page scrolling unless the editor has focus (no value is
// changed by accident while scrolling).
class WheelGuard : public QObject {
public:
    WheelGuard(QScrollBar *bar, QObject *parent) : QObject(parent), bar_(bar) {}

protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (e->type() != QEvent::Wheel) return false;
        auto *w = qobject_cast<QWidget *>(o);
        if (w && w->hasFocus()) return false;
        QApplication::sendEvent(bar_, e);
        return true;
    }

private:
    QScrollBar *bar_;
};

struct KeyText {
    const char *suffix;
    const char *label;
    const char *help;
};

// Texts of the rows (English source strings; translated at use).
const KeyText kChannelKeys[] = {
    {"MIX", "Axis mix", "Which sensor axes drive this channel, e.g. \"z + rx\" or \"0.5*rx - z\""},
    {"SENS", "Sensitivity", "0..255, 0 = channel off"},
    {"DEADZONE", "Deadzone", "Raw sensor units; nothing happens below this"},
    {"SMOOTH_MS", "Smoothing (ms)", "Low-pass time constant, 0 = off"},
};

class ChannelsPage : public QWidget {
public:
    explicit ChannelsPage(const UiContext &ctx) : QWidget(nullptr), ctx_(ctx) {
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        auto *scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);  // a very wide window: the content stays centered
        guard_ = new WheelGuard(scroll->verticalScrollBar(), this);
        auto *content = new QWidget;
        content->setMaximumWidth(spn::ui::tokens::kMaxContentWidth);
        auto *col = new QVBoxLayout(content);
        auto *intro = new QLabel(T("These settings apply to every mode and profile. Changes are saved "
                                   "immediately and the daemon reloads them."));
        intro->setWordWrap(true);
        col->addWidget(intro);
        filter_ = new QLineEdit;
        filter_->setClearButtonEnabled(true);
        filter_->setPlaceholderText(T("Filter settings by name, help text or config key"));
        filter_->setAccessibleName(T("Filter settings"));
        QObject::connect(filter_, &QLineEdit::textChanged, this, [this] { applyFilter(); });
        col->addWidget(filter_);
        noMatch_ = new QLabel(T("No setting matches the filter."));
        noMatch_->setForegroundRole(QPalette::PlaceholderText);
        noMatch_->setVisible(false);
        col->addWidget(noMatch_);
        for (const SettingsModel::Group &g : spn::ui::channelPageGroups()) col->addWidget(buildGroup(g));
        col->addStretch(1);
        scroll->setWidget(content);
        outer->addWidget(scroll, 1);

        auto *bar = new QHBoxLayout;
        bar->setContentsMargins(12, 4, 12, 8);
        message_ = new QLabel(this);
        message_->setWordWrap(true);
        message_->setForegroundRole(QPalette::PlaceholderText);
        bar->addWidget(message_, 1);
        auto *restore = new QPushButton(T("Restore defaults"), this);
        restore->setToolTip(T("Reset all channel and daemon settings on this page to the built-in defaults"));
        QObject::connect(restore, &QPushButton::clicked, this, [this] { onRestoreAll(); });
        bar->addWidget(restore);
        outer->addLayout(bar);

        saveTimer_.setSingleShot(true);
        saveTimer_.setInterval(300);
        QObject::connect(&saveTimer_, &QTimer::timeout, this, [this] { flush(); });
        refreshAll();
    }
    ~ChannelsPage() override { flush(); }

protected:
    void hideEvent(QHideEvent *e) override {
        flush();
        QWidget::hideEvent(e);
    }

private:
    // ---- model access ----
    bool value(const std::string &key, std::string *out) {
        SettingsModel::Value v;
        if (!ctx_.settings->value(0, key, &v)) return false;
        *out = v.text;
        return true;
    }
    double number(const std::string &key) {
        std::string t;
        return value(key, &t) ? qs8(t).toDouble() : 0.0;
    }
    // Validates and stores; shows the model's message at the row on failure.
    bool apply(const std::string &key, const std::string &text, QLabel *help, const QString &helpText) {
        std::string err;
        if (ctx_.settings->setValue(0, key, text, &err)) {
            if (help) setHelp(help, helpText, false);
            changed();
            return true;
        }
        if (help) setHelp(help, qs8(err), true);
        else message_->setText(T("Invalid value: ") + qs8(err));
        return false;
    }
    void setHelp(QLabel *l, const QString &text, bool error) {
        l->setText(error ? T("Invalid value: ") + text : text);
        l->setForegroundRole(error ? QPalette::WindowText : QPalette::PlaceholderText);
        QFont f = l->font();
        f.setBold(error);
        l->setFont(f);
    }
    void changed() {
        refreshPreviews();
        message_->setText(QString());
        saveTimer_.start();
    }
    void flush() {
        saveTimer_.stop();
        if (!ctx_.settings->dirty()) return;
        const SettingsModel::SaveResult r = ctx_.settings->save(*ctx_.strings);
        if (r.saved && r.reloaded) message_->setText(T("Saved."));
        else if (r.saved) message_->setText(T("Saved, but the daemon was not notified: ") + qs8(r.error));
        else message_->setText(T("Not saved: ") + qs8(r.error));
    }
    void refreshAll() {
        for (auto &f : refreshers_) f();
        refreshPreviews();
    }
    void refreshPreviews() {
        const spn::Settings s = ctx_.settings->effective(0);
        for (auto &p : previews_) {
            const spn::ChannelCfg &c = p.first == "scroll_v" ? s.scroll_v : p.first == "scroll_h" ? s.scroll_h : s.zoom;
            p.second->setChannel(c);
        }
    }
    void onResetGroup(const SettingsModel::Group &g) {
        if (spn::ui::resetGroup(*ctx_.settings, g)) {
            refreshAll();
            changed();
        }
    }
    void onRestoreAll() {
        if (QMessageBox::question(this, T("Restore defaults"),
                                  T("Reset all settings on this page to the built-in defaults?")) !=
            QMessageBox::Yes)
            return;
        if (spn::ui::restoreAllDefaults(*ctx_.settings)) {
            refreshAll();
            changed();
        }
    }

    // ---- widgets ----
    template <class W>
    W *guarded(W *w) {
        w->setFocusPolicy(Qt::StrongFocus);
        w->installEventFilter(guard_);
        return w;
    }
    QSpinBox *intSpin(const std::string &key, int lo, int hi, const QString &help, QLabel *helpLabel) {
        auto *s = guarded(new QSpinBox);
        s->setRange(lo, hi);
        s->setKeyboardTracking(false);
        QObject::connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this,
                         [this, key, helpLabel, help](int v) { apply(key, std::to_string(v), helpLabel, help); });
        refreshers_.push_back([this, s, key] {
            QSignalBlocker b(s);
            s->setValue(int(number(key)));
        });
        return s;
    }
    QDoubleSpinBox *dblSpin(const std::string &key, double lo, double hi, double step, int dec,
                            const QString &help, QLabel *helpLabel) {
        auto *s = guarded(new QDoubleSpinBox);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(dec);
        s->setKeyboardTracking(false);
        QObject::connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                         [this, key, helpLabel, help](double v) {
                             apply(key, QString::number(v, 'g', 10).toUtf8().toStdString(), helpLabel, help);
                         });
        refreshers_.push_back([this, s, key] {
            QSignalBlocker b(s);
            s->setValue(number(key));
        });
        return s;
    }
    // Label, editor (made with the helper label it reports errors to) and the helper text below it.
    template <class Make>
    void spinRow(QGridLayout *grid, int *row, const std::string &key, const QString &label, const QString &help,
                 Make make) {
        QLabel *hl = new QLabel(help);
        QWidget *editor = make(hl);
        auto *l = new QLabel(label);
        l->setBuddy(editor);
        grid->addWidget(l, *row, 0, Qt::AlignTop);
        grid->addWidget(editor, *row, 1);
        hl->setWordWrap(true);
        hl->setForegroundRole(QPalette::PlaceholderText);
        grid->addWidget(hl, *row + 1, 1);
        registerRow(key, label, help, {l, editor, hl});
        *row += 2;
    }

    // One setting row for the filter box and the hints: every widget of the row gets the help text
    // and the config key as tooltip, the editors also as accessible description.
    void registerRow(const std::string &key, const QString &label, const QString &help, std::vector<QWidget *> widgets) {
        const spn::ui::IdentityStrings fallback;
        const spn::ui::StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
        const QString tip = qs8(spn::ui::helpWithKey(help.toUtf8().toStdString(), key, strings));
        for (QWidget *w : widgets) {
            w->setToolTip(tip);
            if (!qobject_cast<QLabel *>(w)) w->setAccessibleDescription(tip);
        }
        FilterRow r;
        r.widgets = std::move(widgets);
        r.fields = {label.toUtf8().toStdString(), help.toUtf8().toStdString(), key, currentTitle_};
        r.group = currentBox_;
        rows_.push_back(std::move(r));
    }

    // Shows the rows that match the filter box; a group without a matching row is hidden with its
    // reset button.
    void applyFilter() {
        const std::string text = filter_->text().toUtf8().toStdString();
        std::vector<QGroupBox *> shown;
        for (const FilterRow &r : rows_) {
            const bool match = spn::ui::matchesFilter(text, r.fields);
            for (QWidget *w : r.widgets) w->setVisible(match);
            if (match && std::find(shown.begin(), shown.end(), r.group) == shown.end()) shown.push_back(r.group);
        }
        for (QGroupBox *g : groups_) g->setVisible(std::find(shown.begin(), shown.end(), g) != shown.end());
        noMatch_->setVisible(shown.empty());
    }

    QGroupBox *buildGroup(const SettingsModel::Group &g) {
        auto *box = new QGroupBox(T(g.title.c_str()));
        currentBox_ = box;
        currentTitle_ = g.title;
        groups_.push_back(box);
        auto *grid = new QGridLayout(box);
        grid->setColumnStretch(1, 1);
        int row = 0;
        const bool isChannel = g.id != "daemon";
        const std::string pre = g.id == "scroll_v" ? "SCROLL_V_" : g.id == "scroll_h" ? "SCROLL_H_" : "ZOOM_";

        if (isChannel) {
            for (const KeyText &kt : kChannelKeys) {
                const std::string key = pre + kt.suffix;
                const QString help = T(kt.help);
                const std::string suffix = kt.suffix;
                if (suffix == "MIX") {
                    spinRow(grid, &row, key, T(kt.label), help, [&](QLabel *hl) -> QWidget * {
                        auto *e = new QLineEdit;
                        QObject::connect(e, &QLineEdit::editingFinished, this, [this, e, key, hl, help] {
                            if (apply(key, e->text().toUtf8().toStdString(), hl, help)) refreshAll();
                        });
                        refreshers_.push_back([this, e, key] {
                            std::string t;
                            if (value(key, &t)) e->setText(qs8(t));
                        });
                        return e;
                    });
                } else if (suffix == "SENS") {
                    spinRow(grid, &row, key, T(kt.label), help,
                            [&](QLabel *hl) -> QWidget * { return intSpin(key, 0, 255, help, hl); });
                } else if (suffix == "DEADZONE") {
                    spinRow(grid, &row, key, T(kt.label), help,
                            [&](QLabel *hl) -> QWidget * { return intSpin(key, 0, 500, help, hl); });
                } else {
                    spinRow(grid, &row, key, T(kt.label), help,
                            [&](QLabel *hl) -> QWidget * { return dblSpin(key, 0, 500, 1, 1, help, hl); });
                }
            }
            addSign(grid, &row, pre + "SIGN");
            addCurve(grid, &row, pre + "CURVE", g.id);
            if (g.id == "zoom") addZoomDetection(grid, &row);
        } else {
            const QString stepHelp = T("Push strength at which a step: channel action taps its key once "
                                       "(steps per second after sensitivity)");
            spinRow(grid, &row, "STEP_TRIGGER", T("Step trigger"), stepHelp,
                    [&](QLabel *hl) -> QWidget * { return dblSpin("STEP_TRIGGER", 0.5, 10000, 1, 1, stepHelp, hl); });
            const QString tickHelp = T("How many times per second the daemon generates events (30..1000)");
            spinRow(grid, &row, "TICK_HZ", T("Tick rate (Hz)"), tickHelp,
                    [&](QLabel *hl) -> QWidget * { return intSpin("TICK_HZ", 30, 1000, tickHelp, hl); });
#if !defined(_WIN32) && !defined(__APPLE__)  // X11/uinput exist on Linux only (Windows: SendInput, macOS: CGEvent)
            const QString beHelp = T("auto: Wayland -> uinput, otherwise X11 (with fallback). Takes effect "
                                     "after the daemon is restarted.");
            spinRow(grid, &row, "BACKEND", T("Output backend"), beHelp, [&](QLabel *hl) -> QWidget * {
                auto *c = guarded(new QComboBox);
                for (const char *id : {"auto", "x11", "uinput"}) c->addItem(QString::fromLatin1(id), QString::fromLatin1(id));
                QObject::connect(c, QOverload<int>::of(&QComboBox::activated), this, [this, c, hl, beHelp](int) {
                    apply("BACKEND", c->currentData().toString().toUtf8().toStdString(), hl, beHelp);
                });
                refreshers_.push_back([this, c] {
                    std::string t;
                    value("BACKEND", &t);
                    const int i = c->findData(qs8(t));
                    QSignalBlocker b(c);
                    c->setCurrentIndex(i < 0 ? 0 : i);
                });
                return c;
            });
#endif
        }

        auto *reset = new QPushButton(T("Reset to defaults"));
        reset->setToolTip(T("Remove the settings of this group from the configuration so the built-in defaults apply"));
        QObject::connect(reset, &QPushButton::clicked, this, [this, g] { onResetGroup(g); });
        grid->addWidget(reset, row, 0, 1, 2, Qt::AlignRight);
        return box;
    }

    void addSign(QGridLayout *grid, int *row, const std::string &key) {
        const QString help = T("Reverse the direction of this channel");
        auto *cb = new QCheckBox(T("Invert direction"));
        cb->setToolTip(help);
        QObject::connect(cb, &QCheckBox::toggled, this,
                         [this, key, help](bool on) { apply(key, on ? "-1" : "1", nullptr, help); });
        refreshers_.push_back([this, cb, key] {
            QSignalBlocker b(cb);
            cb->setChecked(number(key) < 0);
        });
        grid->addWidget(cb, *row, 0, 1, 2);
        registerRow(key, T("Invert direction"), help, {cb});
        *row += 1;
    }

    void addCurve(QGridLayout *grid, int *row, const std::string &key, const std::string &groupId) {
        const QString expHelp = T("1.0 = linear, >1 = finer at small deflections");
        const QString ptsHelp = T("Optional: ascending \"input:factor\" pairs, e.g. \"0:0 50:0.1 200:1 350:3\". "
                                  "Input = deflection after the deadzone (raw sensor units), the factor applies "
                                  "to the linear speed, linearly interpolated in between. Overrides the exponent.");
        auto *expLabel = new QLabel(expHelp);
        auto *ptsLabel = new QLabel(ptsHelp);

        // Exponent: slider (0.2..4.0) and spin box (0.1..10) kept in sync.
        auto *slider = guarded(new QSlider(Qt::Horizontal));
        slider->setRange(4, 80);
        slider->setPageStep(5);
        auto *spin = guarded(new QDoubleSpinBox);
        spin->setRange(0.1, 10.0);
        spin->setSingleStep(0.05);
        spin->setDecimals(2);
        spin->setKeyboardTracking(false);
        const QString curveName = T("Curve (exponent)") + QStringLiteral(" - ") + qs8(groupId);
        slider->setAccessibleName(curveName);
        spin->setAccessibleName(curveName);
        auto *pair = new QWidget;
        auto *pl = new QHBoxLayout(pair);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->addWidget(slider, 1);
        pl->addWidget(spin);
        pair->setToolTip(expHelp);
        QObject::connect(slider, &QSlider::valueChanged, this, [spin](int v) { spin->setValue(v / 20.0); });
        QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                         [this, slider, key, expLabel, expHelp](double v) {
                             {
                                 QSignalBlocker b(slider);
                                 slider->setValue(int(v * 20.0 + 0.5));
                             }
                             apply(key, QString::number(v, 'g', 6).toUtf8().toStdString(), expLabel, expHelp);
                         });
        auto *l = new QLabel(T("Curve (exponent)"));
        l->setBuddy(spin);
        grid->addWidget(l, *row, 0, Qt::AlignTop);
        grid->addWidget(pair, *row, 1);
        expLabel->setWordWrap(true);
        expLabel->setForegroundRole(QPalette::PlaceholderText);
        grid->addWidget(expLabel, *row + 1, 1);
        registerRow(key, T("Curve (exponent)"), expHelp, {l, pair, slider, spin, expLabel});
        *row += 2;

        auto *pts = new QLineEdit;
        pts->setPlaceholderText(T("empty = exponent applies, e.g. 0:0 50:0.1 200:1 350:3"));
        pts->setToolTip(ptsHelp);
        QObject::connect(pts, &QLineEdit::editingFinished, this,
                         [this, pts, spin, key, ptsLabel, ptsHelp] {
                             const QString t = pts->text().trimmed();
                             if (t.isEmpty()) {
                                 if (apply(key, QString::number(spin->value(), 'g', 6).toUtf8().toStdString(), ptsLabel, ptsHelp))
                                     refreshAll();
                             } else if (!t.contains(QLatin1Char(':'))) {
                                 setHelp(ptsLabel, T("point list expected: \"input:factor ...\" (use the slider for an exponent)"), true);
                             } else if (apply(key, t.toUtf8().toStdString(), ptsLabel, ptsHelp)) {
                                 refreshAll();
                             }
                         });
        auto *pl2 = new QLabel(T("Curve (points)"));
        pl2->setBuddy(pts);
        grid->addWidget(pl2, *row, 0, Qt::AlignTop);
        grid->addWidget(pts, *row, 1);
        ptsLabel->setWordWrap(true);
        ptsLabel->setForegroundRole(QPalette::PlaceholderText);
        grid->addWidget(ptsLabel, *row + 1, 1);
        registerRow(key, T("Curve (points)"), ptsHelp, {pl2, pts, ptsLabel});
        *row += 2;

        const std::string gid = groupId;
        refreshers_.push_back([this, slider, spin, pts, gid] {
            const spn::Settings s = ctx_.settings->effective(0);
            const spn::ChannelCfg &c = gid == "scroll_v" ? s.scroll_v : gid == "scroll_h" ? s.scroll_h : s.zoom;
            QSignalBlocker b1(slider), b2(spin);
            spin->setValue(c.curve_exp);
            slider->setValue(int(c.curve_exp * 20.0 + 0.5));
            const bool hasPts = !c.curve_points.empty();
            slider->setEnabled(!hasPts);
            spin->setEnabled(!hasPts);
            std::string txt;
            if (hasPts)
                for (const spn::KV &kv : spn::allKeys(s)) {
                    const std::string suffix = "CURVE";
                    const std::string want = (gid == "scroll_v" ? "SCROLL_V_" : gid == "scroll_h" ? "SCROLL_H_" : "ZOOM_") + suffix;
                    if (kv.first == want) txt = kv.second;
                }
            pts->setText(qs8(txt));
        });

        auto *preview = new CurveWidget(this);
        grid->addWidget(preview, *row, 0, 1, 2);
        rows_.back().widgets.push_back(preview);  // the preview is shown with the points row
        *row += 1;
        previews_.emplace_back(groupId, preview);
    }

    void addZoomDetection(QGridLayout *grid, int *row) {
        const QString latHelp = T("Zoom only starts if the twist is stronger than the strongest of these axes");
        auto *lat = new QLabel(latHelp);
        auto *box = new QWidget;
        auto *bl = new QHBoxLayout(box);
        bl->setContentsMargins(0, 0, 0, 0);
        auto boxes = std::make_shared<std::vector<QCheckBox *>>();
        for (int a = 0; a < spn::AX_COUNT; ++a) {
            auto *cb = new QCheckBox(QString::fromLatin1(spn::axisName(spn::Axis(a))));
            cb->setToolTip(latHelp);
            boxes->push_back(cb);
            bl->addWidget(cb);
            QObject::connect(cb, &QCheckBox::toggled, this, [this, boxes, lat, latHelp](bool) {
                unsigned mask = 0;
                for (size_t i = 0; i < boxes->size(); ++i)
                    if ((*boxes)[i]->isChecked()) mask |= 1u << unsigned(i);
                apply("ZOOM_LATERAL", spn::ui::lateralText(mask), lat, latHelp);
            });
        }
        bl->addStretch(1);
        auto *l = new QLabel(T("Zoom must dominate against"));
        grid->addWidget(l, *row, 0, Qt::AlignTop);
        grid->addWidget(box, *row, 1);
        lat->setWordWrap(true);
        lat->setForegroundRole(QPalette::PlaceholderText);
        grid->addWidget(lat, *row + 1, 1);
        std::vector<QWidget *> latWidgets = {l, box, lat};
        for (QCheckBox *cb : *boxes) latWidgets.push_back(cb);
        registerRow("ZOOM_LATERAL", T("Zoom must dominate against"), latHelp, latWidgets);
        *row += 2;
        refreshers_.push_back([this, boxes] {
            const unsigned mask = spn::ui::lateralMask(ctx_.settings->effective(0));
            for (size_t i = 0; i < boxes->size(); ++i) {
                QSignalBlocker b((*boxes)[i]);
                (*boxes)[i]->setChecked((mask >> unsigned(i)) & 1u);
            }
        });

        const QString enterHelp = T("How long the twist must dominate before zoom engages");
        spinRow(grid, row, "ZOOM_ENTER_MS", T("Entry time (ms)"), enterHelp,
                [&](QLabel *hl) -> QWidget * { return intSpin("ZOOM_ENTER_MS", 0, 1000, enterHelp, hl); });
        const QString exitHelp = T("Hysteresis: zoom only ends once a lateral axis is stronger by this factor");
        spinRow(grid, row, "ZOOM_EXIT_RATIO", T("Exit ratio"), exitHelp,
                [&](QLabel *hl) -> QWidget * { return dblSpin("ZOOM_EXIT_RATIO", 1.0, 10.0, 0.05, 2, exitHelp, hl); });
    }

    struct FilterRow {
        std::vector<QWidget *> widgets;
        std::vector<std::string> fields;  // label, help, config key, group title
        QGroupBox *group = nullptr;
    };
    UiContext ctx_;
    WheelGuard *guard_ = nullptr;
    QLineEdit *filter_ = nullptr;
    QLabel *noMatch_ = nullptr;
    std::vector<FilterRow> rows_;
    std::vector<QGroupBox *> groups_;
    QGroupBox *currentBox_ = nullptr;  // group being built
    std::string currentTitle_;
    QLabel *message_ = nullptr;
    QTimer saveTimer_;
    std::vector<std::function<void()>> refreshers_;
    std::vector<std::pair<std::string, CurveWidget *>> previews_;
};

}  // namespace

QWidget *createChannelsPage(const UiContext &ctx) { return new ChannelsPage(ctx); }
