// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "channels_editor.hpp"

#include <QApplication>
#include <QCheckBox>
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
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

#include "ui/model/channels_page_model.hpp"
#include "ui/model/curve_preview.hpp"
#include "ui/model/design_tokens.hpp"
#include "ui/model/inheritance_model.hpp"
#include "ui/model/ui_filter.hpp"
#include "ui_i18n.hpp"

namespace {

using spn::ui::SettingsModel;
namespace tk = spn::ui::tokens;

constexpr double kPreviewMaxInput = 350.0;

// Repolishes a widget after a dynamic property (error / invalid) changed, so the style sheet applies.
void setFlag(QWidget *w, const char *name, bool on) {
    if (!w || w->property(name).toBool() == on) return;
    w->setProperty(name, on);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

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

}  // namespace

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
        p.drawText(QPointF(r.left() + 4, r.top() + p.fontMetrics().ascent()), T("%1 steps/s").arg(top, 0, 'f', 1));
        p.drawText(QRectF(r.left(), r.bottom() + 2, r.width(), 16), Qt::AlignRight, QString::number(int(kPreviewMaxInput)));
        p.drawText(QRectF(r.left(), r.bottom() + 2, r.width(), 16), Qt::AlignLeft, QStringLiteral("0"));
    }

private:
    std::vector<spn::ui::CurvePoint> pts_;
    double maxSpeed_ = 0;
};

ChannelsEditor::ChannelsEditor(const UiContext &ctx, QWidget *parent) : QWidget(parent), ctx_(ctx) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("channelsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);  // a very wide window: the content stays centered
    auto *content = new QWidget;
    content->setMaximumWidth(tk::kMaxContentWidth);
    auto *col = new QVBoxLayout(content);
    col->setContentsMargins(tk::kSpaceSm, tk::kSpaceLg, tk::kSpaceSm, tk::kPageMargin);
    col->setSpacing(tk::kSectionGap);
    auto *top = new QHBoxLayout;
    auto *intro = new QLabel(T("How the cap movement becomes scrolling and zooming. A change is saved at once and the "
                               "daemon reloads it."));
    intro->setWordWrap(true);
    intro->setForegroundRole(QPalette::PlaceholderText);
    top->addWidget(intro, 1);
    auto *restore = new QPushButton(T("Restore defaults"));
    restore->setObjectName(QStringLiteral("restoreDefaults"));
    restore->setToolTip(T("Remove every value of this tab from the profile: the inherited values (the built-in defaults "
                          "for Global) apply again"));
    connect(restore, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, T("Restore defaults"),
                                  T("Reset all settings on this tab to the inherited values?")) == QMessageBox::Yes)
            restoreDefaults();
    });
    top->addWidget(restore, 0, Qt::AlignTop);
    col->addLayout(top);
    noMatch_ = new QLabel(T("No setting matches the filter."));
    noMatch_->setForegroundRole(QPalette::PlaceholderText);
    noMatch_->setVisible(false);
    col->addWidget(noMatch_);
    for (const SettingsModel::Group &g : spn::ui::channelPageGroups()) col->addWidget(buildGroup(g));
    col->addStretch(1);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    setSection(0);
}

// ---- model access ----

std::string ChannelsEditor::displayText(const std::string &key) const {
    const spn::Settings s = ctx_.settings->effectiveWithMode(section_);
    for (const spn::KV &kv : spn::allKeys(s))
        if (kv.first == key) return kv.second;
    return std::string();
}

double ChannelsEditor::number(const std::string &key) const { return qs8(displayText(key)).toDouble(); }

void ChannelsEditor::showProblem(QWidget *editor, QLabel *help, const QString &helpText, const QString &error) {
    const bool bad = !error.isEmpty();
    if (help) {
        help->setText(bad ? mark(spn::ui::StatusKind::Error, T("Invalid value: ") + error) : helpText);
        setFlag(help, "error", bad);
    }
    setFlag(editor, "invalid", bad);
}

// A valid mix with lift/press (y) is accepted but flagged: the cap moves when it is grabbed.
void ChannelsEditor::showLiftPressWarning(QLabel *help, const QString &helpText, const std::string &text) {
    const bool warn = spn::ui::mixUsesLiftPress(text);
    if (!help) return;
    help->setText(warn ? mark(spn::ui::StatusKind::Warning,
                              T("Warning: lift/press (y) is prone to false triggers when you grab or release the cap"))
                       : helpText);
    setFlag(help, "warning", warn);
}

// Validates and stores in the profile; shows the model's message at the row on failure (nothing is
// written then, the file stays valid). typed: text still being typed, written after a pause.
bool ChannelsEditor::apply(const std::string &key, const std::string &text, QWidget *editor, QLabel *help,
                           const QString &helpText, bool typed) {
    std::string err;
    if (ctx_.settings->setValue(section_, key, text, &err)) {
        showProblem(editor, help, helpText, QString());
        changed(key, typed);
        return true;
    }
    showProblem(editor, help, helpText, qs8(err));
    return false;
}

void ChannelsEditor::changed(const std::string &, bool typed) {
    refreshMeta();
    refreshPreviews();
    if (typed) ctx_.saveTyped();
    else ctx_.saveSoon();
}

void ChannelsEditor::setSection(size_t section) {
    section_ = section;
    refreshAll();
}

void ChannelsEditor::refreshAll() {
    for (auto &r : refreshers_) r();
    refreshMeta();
    refreshPreviews();
}

void ChannelsEditor::refreshPreviews() {
    const spn::Settings s = ctx_.settings->effectiveWithMode(section_);
    for (auto &p : previews_) {
        const spn::ChannelCfg &c = p.first == "scroll_v" ? s.scroll_v : p.first == "scroll_h" ? s.scroll_h : s.zoom;
        p.second->setChannel(c);
    }
}

// Origin text and Reset button of every row: "Inherited from Global: 5", "Set in this profile
// (inherited: 5)"; in [global] the plain value and, when device or program profiles override the
// key, "N profiles override this" (the list is the tooltip).
void ChannelsEditor::refreshMeta() {
    const spn::ui::IdentityStrings fallback;
    const spn::ui::StringProvider &strings = ctx_.strings ? *ctx_.strings : fallback;
    for (Meta &m : metas_) {
        const spn::ui::KeyOrigin o = spn::ui::keyOrigin(*ctx_.settings, section_, m.key);
        std::string text = spn::ui::originBadgeText(o, strings);
        QString tip;
        if (section_ == 0) {
            std::vector<std::string> names;
            const std::string hint = spn::ui::overrideHint(*ctx_.settings, m.key, strings, &names);
            if (!hint.empty()) {
                text += (text.empty() ? "" : "; ") + hint;
                tip = qs8(spn::ui::overrideHintTooltip(names, strings));
            }
        }
        m.origin->setText(qs8(text));
        m.origin->setToolTip(tip);
        m.reset->setEnabled(o.canReset);
        m.reset->setToolTip(qs8(spn::ui::resetTooltip(section_, o.canReset, strings)));
        m.reset->setAccessibleName(T("Reset to inherited") + QLatin1Char(' ') + qs8(m.key));
    }
}

void ChannelsEditor::onResetKey(const std::string &key) {
    if (!ctx_.settings->clearOverride(section_, key)) return;
    refreshAll();
    ctx_.saveSoon();
}

void ChannelsEditor::onResetGroup(const std::vector<std::string> &keys) {
    bool any = false;
    for (const std::string &k : keys) {
        SettingsModel::Value v;
        if (!ctx_.settings->value(section_, k, &v)) continue;
        if (section_ == 0 ? v.text != v.inherited : v.overridden) {
            ctx_.settings->clearOverride(section_, k);
            any = true;
        }
    }
    if (!any) return;
    refreshAll();
    ctx_.saveSoon();
}

bool ChannelsEditor::restoreDefaults() {
    bool any = false;
    for (const SettingsModel::Group &g : spn::ui::channelPageGroups())
        for (const std::string &k : g.keys) {
            SettingsModel::Value v;
            if (!ctx_.settings->value(section_, k, &v)) continue;
            if (section_ == 0 ? v.text != v.inherited : v.overridden) {
                ctx_.settings->clearOverride(section_, k);
                any = true;
            }
        }
    if (!any) return false;
    refreshAll();
    ctx_.saveSoon();
    return true;
}

// ---- rows ----

QWidget *ChannelsEditor::editorFor(const std::string &key) const {
    for (const auto &e : editors_)
        if (e.first == key) return e.second;
    return nullptr;
}

QToolButton *ChannelsEditor::resetFor(const std::string &key) const {
    for (const Meta &m : metas_)
        if (m.key == key) return m.reset;
    return nullptr;
}

QString ChannelsEditor::originFor(const std::string &key) const {
    for (const Meta &m : metas_)
        if (m.key == key) return m.origin->text();
    return QString();
}

QAbstractSpinBox *ChannelsEditor::intSpin(const std::string &key, int lo, int hi, const QString &help, QLabel *helpLabel) {
    auto *s = new QSpinBox;
    s->setFocusPolicy(Qt::StrongFocus);
    s->setRange(lo, hi);
    s->setKeyboardTracking(false);  // valueChanged on commit (Enter, arrows, focus out)
    connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this, s, key, helpLabel, help](int v) { apply(key, std::to_string(v), s, helpLabel, help, false); });
    refreshers_.push_back([this, s, key] {
        QSignalBlocker b(s);
        s->setValue(int(number(key)));
    });
    editors_.emplace_back(key, s);
    return s;
}

QAbstractSpinBox *ChannelsEditor::dblSpin(const std::string &key, double lo, double hi, double step, int dec,
                                          const QString &help, QLabel *helpLabel) {
    auto *s = new QDoubleSpinBox;
    s->setFocusPolicy(Qt::StrongFocus);
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setDecimals(dec);
    s->setKeyboardTracking(false);
    connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, s, key, helpLabel, help](double v) {
        apply(key, QString::number(v, 'g', 10).toUtf8().toStdString(), s, helpLabel, help, false);
    });
    refreshers_.push_back([this, s, key] {
        QSignalBlocker b(s);
        s->setValue(number(key));
    });
    editors_.emplace_back(key, s);
    return s;
}

// Label, editor (made with the helper label it reports errors to), the origin and Reset of the
// row, and the helper text below it.
template <class Make>
void ChannelsEditor::spinRow(QGridLayout *grid, int *row, const std::string &key, const QString &label, const QString &help,
                             Make make) {
    QLabel *hl = new QLabel(help);
    QWidget *editor = make(hl);
    auto *l = new QLabel(label);
    l->setBuddy(editor);
    grid->addWidget(l, *row, 0, Qt::AlignTop);
    grid->addWidget(editor, *row, 1);
    hl->setWordWrap(true);
    hl->setForegroundRole(QPalette::PlaceholderText);
    grid->addWidget(hl, *row + 2, 1, 1, 2);
    registerRow(key, label, help, {l, editor, hl});
    addMeta(grid, *row, key, &rows_.back());
    *row += 3;
}

void ChannelsEditor::addMeta(QGridLayout *grid, int row, const std::string &key, FilterRow *fr) {
    auto *origin = new QLabel;
    origin->setForegroundRole(QPalette::PlaceholderText);
    origin->setWordWrap(true);
    origin->setMaximumWidth(300);
    origin->setAccessibleName(qs8(key) + QStringLiteral(": ") + T("Origin"));
    auto *reset = new QToolButton;
    reset->setText(T("Reset"));
    reset->setAccessibleName(T("Reset to inherited") + QLatin1Char(' ') + qs8(key));
    connect(reset, &QToolButton::clicked, this, [this, key] { onResetKey(key); });
    grid->addWidget(origin, row + 1, 1, 1, 2, Qt::AlignVCenter);
    grid->addWidget(reset, row, 2, Qt::AlignVCenter);
    fr->widgets.push_back(origin);
    fr->widgets.push_back(reset);
    metas_.push_back({key, origin, reset});
}

// One setting row for the filter box and the hints: every widget of the row gets the help text
// and the config key as tooltip, the editors also as accessible description.
void ChannelsEditor::registerRow(const std::string &key, const QString &label, const QString &help,
                                 std::vector<QWidget *> widgets) {
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

void ChannelsEditor::setFilter(const QString &text) {
    filter_ = text.toUtf8().toStdString();
    applyFilter();
}

// Shows the rows that match the filter; a group without a matching row is hidden with its reset button.
void ChannelsEditor::applyFilter() {
    std::vector<QGroupBox *> shown;
    for (const FilterRow &r : rows_) {
        const bool match = spn::ui::matchesFilter(filter_, r.fields);
        for (QWidget *w : r.widgets) w->setVisible(match);
        if (match && std::find(shown.begin(), shown.end(), r.group) == shown.end()) shown.push_back(r.group);
    }
    for (QGroupBox *g : groups_) g->setVisible(std::find(shown.begin(), shown.end(), g) != shown.end());
    noMatch_->setVisible(shown.empty());
}

int ChannelsEditor::visibleRowCount() const {
    int n = 0;
    for (const FilterRow &r : rows_)
        if (spn::ui::matchesFilter(filter_, r.fields)) ++n;
    return n;
}

QGroupBox *ChannelsEditor::buildGroup(const SettingsModel::Group &g) {
    auto *box = new QGroupBox(T(g.title.c_str()));
    currentBox_ = box;
    currentTitle_ = g.title;
    groups_.push_back(box);
    auto *grid = new QGridLayout(box);
    grid->setColumnStretch(1, 1);
    grid->setHorizontalSpacing(tk::kRowGap);
    grid->setContentsMargins(tk::kSpaceMd, tk::kCardPadding, tk::kSpaceMd, tk::kCardPadding);
    int row = 0;
    const bool isChannel = g.id != "steps";
    const std::string pre = g.id == "scroll_v" ? "SCROLL_V_" : g.id == "scroll_h" ? "SCROLL_H_" : "ZOOM_";

    if (isChannel) {
        for (const KeyText &kt : kChannelKeys) {
            const std::string key = pre + kt.suffix;
            const QString help = T(kt.help);
            const std::string suffix = kt.suffix;
            if (suffix == "MIX") {
                spinRow(grid, &row, key, T(kt.label), help, [&](QLabel *hl) -> QWidget * {
                    auto *e = new QLineEdit;
                    connect(e, &QLineEdit::textEdited, this, [this, e, key, hl, help](const QString &t) {
                        const std::string text = t.toUtf8().toStdString();
                        if (apply(key, text, e, hl, help, true)) showLiftPressWarning(hl, help, text);
                    });
                    connect(e, &QLineEdit::editingFinished, this, [this] { ctx_.saveSoon(); });  // focus out: now
                    refreshers_.push_back([this, e, key, hl, help] {
                        QSignalBlocker b(e);
                        e->setText(qs8(displayText(key)));
                        setFlag(e, "invalid", false);
                        showLiftPressWarning(hl, help, displayText(key));
                    });
                    editors_.emplace_back(key, e);
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
    }

    auto *reset = new QPushButton(T("Reset this group"));
    reset->setToolTip(T("Remove the settings of this group from the profile so the inherited values apply"));
    reset->setAccessibleName(T("Reset this group") + QStringLiteral(" - ") + T(g.title.c_str()));
    const std::vector<std::string> keys = g.keys;
    connect(reset, &QPushButton::clicked, this, [this, keys] { onResetGroup(keys); });
    grid->addWidget(reset, row, 0, 1, 3, Qt::AlignRight);
    return box;
}

void ChannelsEditor::addSign(QGridLayout *grid, int *row, const std::string &key) {
    const QString help = T("Reverse the direction of this channel");
    auto *cb = new QCheckBox(T("Invert direction"));
    cb->setToolTip(help);
    connect(cb, &QCheckBox::toggled, this, [this, key, help, cb](bool on) { apply(key, on ? "-1" : "1", cb, nullptr, help, false); });
    refreshers_.push_back([this, cb, key] {
        QSignalBlocker b(cb);
        cb->setChecked(number(key) < 0);
    });
    editors_.emplace_back(key, cb);
    grid->addWidget(cb, *row, 0, 1, 2);
    registerRow(key, T("Invert direction"), help, {cb});
    addMeta(grid, *row, key, &rows_.back());
    *row += 2;
}

void ChannelsEditor::addCurve(QGridLayout *grid, int *row, const std::string &key, const std::string &groupId) {
    const QString expHelp = T("1.0 = linear, >1 = finer at small deflections");
    const QString ptsHelp = T("Optional: ascending \"input:factor\" pairs, e.g. \"0:0 50:0.1 200:1 350:3\". "
                              "Input = deflection after the deadzone (raw sensor units), the factor applies "
                              "to the linear speed, linearly interpolated in between. Overrides the exponent.");
    auto *expLabel = new QLabel(expHelp);
    auto *ptsLabel = new QLabel(ptsHelp);

    // Exponent: slider (0.2..4.0) and spin box (0.1..10) kept in sync.
    auto *slider = new QSlider(Qt::Horizontal);
    slider->setFocusPolicy(Qt::StrongFocus);
    slider->setRange(4, 80);
    slider->setPageStep(5);
    auto *spin = new QDoubleSpinBox;
    spin->setFocusPolicy(Qt::StrongFocus);
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
    connect(slider, &QSlider::valueChanged, this, [spin](int v) { spin->setValue(v / 20.0); });
    connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this, slider, spin, key, expLabel, expHelp](double v) {
                {
                    QSignalBlocker b(slider);
                    slider->setValue(int(v * 20.0 + 0.5));
                }
                // while the slider is dragged the file is written after a pause, on release at once
                apply(key, QString::number(v, 'g', 6).toUtf8().toStdString(), spin, expLabel, expHelp, slider->isSliderDown());
            });
    connect(slider, &QSlider::sliderReleased, this, [this] { ctx_.saveSoon(); });
    auto *l = new QLabel(T("Curve (exponent)"));
    l->setBuddy(spin);
    grid->addWidget(l, *row, 0, Qt::AlignTop);
    grid->addWidget(pair, *row, 1);
    expLabel->setWordWrap(true);
    expLabel->setForegroundRole(QPalette::PlaceholderText);
    grid->addWidget(expLabel, *row + 2, 1, 1, 2);
    registerRow(key, T("Curve (exponent)"), expHelp, {l, pair, slider, spin, expLabel});
    addMeta(grid, *row, key, &rows_.back());
    editors_.emplace_back(key, spin);
    *row += 3;

    auto *pts = new QLineEdit;
    pts->setPlaceholderText(T("empty = exponent applies, e.g. 0:0 50:0.1 200:1 350:3"));
    pts->setToolTip(ptsHelp);
    connect(pts, &QLineEdit::textEdited, this, [this, pts, spin, key, ptsLabel, ptsHelp](const QString &text) {
        const QString t = text.trimmed();
        if (t.isEmpty()) {
            apply(key, QString::number(spin->value(), 'g', 6).toUtf8().toStdString(), pts, ptsLabel, ptsHelp, true);
        } else if (!t.contains(QLatin1Char(':'))) {
            showProblem(pts, ptsLabel, ptsHelp, T("point list expected: \"input:factor ...\" (use the slider for an exponent)"));
        } else {
            apply(key, t.toUtf8().toStdString(), pts, ptsLabel, ptsHelp, true);
        }
    });
    connect(pts, &QLineEdit::editingFinished, this, [this] { ctx_.saveSoon(); });
    auto *pl2 = new QLabel(T("Curve (points)"));
    pl2->setBuddy(pts);
    grid->addWidget(pl2, *row, 0, Qt::AlignTop);
    grid->addWidget(pts, *row, 1);
    ptsLabel->setWordWrap(true);
    ptsLabel->setForegroundRole(QPalette::PlaceholderText);
    grid->addWidget(ptsLabel, *row + 1, 1, 1, 2);
    registerRow(key, T("Curve (points)"), ptsHelp, {pl2, pts, ptsLabel});
    *row += 2;

    const std::string gid = groupId;
    refreshers_.push_back([this, slider, spin, pts, gid, key] {
        const spn::Settings s = ctx_.settings->effectiveWithMode(section_);
        const spn::ChannelCfg &c = gid == "scroll_v" ? s.scroll_v : gid == "scroll_h" ? s.scroll_h : s.zoom;
        QSignalBlocker b1(slider), b2(spin), b3(pts);
        spin->setValue(c.curve_exp);
        slider->setValue(int(c.curve_exp * 20.0 + 0.5));
        const bool hasPts = !c.curve_points.empty();
        slider->setEnabled(!hasPts);
        spin->setEnabled(!hasPts);
        pts->setText(hasPts ? qs8(displayText(key)) : QString());
        setFlag(pts, "invalid", false);
    });

    auto *preview = new CurveWidget(this);
    grid->addWidget(preview, *row, 0, 1, 3);
    rows_.back().widgets.push_back(preview);  // the preview is shown with the points row
    *row += 1;
    previews_.emplace_back(groupId, preview);
}

void ChannelsEditor::addZoomDetection(QGridLayout *grid, int *row) {
    const QString latHelp = T("Zoom only starts if the twist is stronger than the strongest of these axes");
    auto *lat = new QLabel(latHelp);
    auto *box = new QWidget;
    auto *bl = new QGridLayout(box);  // three per line, so the row stays narrow in a small window
    bl->setContentsMargins(0, 0, 0, 0);
    auto boxes = std::make_shared<std::vector<QCheckBox *>>();
    for (int a = 0; a < spn::AX_COUNT; ++a) {
        auto *cb = new QCheckBox(QString::fromLatin1(spn::axisName(spn::Axis(a))));
        cb->setProperty("look", QStringLiteral("box"));
        cb->setToolTip(latHelp);
        boxes->push_back(cb);
        bl->addWidget(cb, a / 3, a % 3);
        connect(cb, &QCheckBox::toggled, this, [this, boxes, lat, latHelp, box](bool) {
            unsigned mask = 0;
            for (size_t i = 0; i < boxes->size(); ++i)
                if ((*boxes)[i]->isChecked()) mask |= 1u << unsigned(i);
            apply("ZOOM_LATERAL", spn::ui::lateralText(mask), box, lat, latHelp, false);
        });
    }
    bl->setColumnStretch(3, 1);
    auto *l = new QLabel(T("Zoom must dominate against"));
    grid->addWidget(l, *row, 0, Qt::AlignTop);
    grid->addWidget(box, *row, 1);
    lat->setWordWrap(true);
    lat->setForegroundRole(QPalette::PlaceholderText);
    grid->addWidget(lat, *row + 2, 1, 1, 2);
    std::vector<QWidget *> latWidgets = {l, box, lat};
    for (QCheckBox *cb : *boxes) latWidgets.push_back(cb);
    registerRow("ZOOM_LATERAL", T("Zoom must dominate against"), latHelp, latWidgets);
    addMeta(grid, *row, "ZOOM_LATERAL", &rows_.back());
    editors_.emplace_back("ZOOM_LATERAL", box);
    *row += 3;
    refreshers_.push_back([this, boxes] {
        const unsigned mask = spn::ui::lateralMask(ctx_.settings->effectiveWithMode(section_));
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
