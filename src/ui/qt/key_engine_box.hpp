// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The analog / digital check box of a key function with its rate and trigger fields, and the "no key in this direction" boxes
// (stage 3 of #224). Shared by the profile motions page and the joystick mode editor; the rules and words are in
// ui/model/key_engine_model.hpp. The widgets of the key chords themselves are the existing chordPicker (key_combo.hpp).
#pragma once
#include <QCheckBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>
#include <string>

#include "config.hpp"
#include "ui/model/key_engine_model.hpp"
#include "ui_i18n.hpp"

struct KeyEngineEnv {
    std::string motionId;                     // "v_push" ... "lift"
    spn::ui::ChannelForm *form = nullptr;     // the function that is edited in place (keys: or step:)
    spn::Settings effective;                  // what applies now (for the words and the placeholders)
    // The text the motion itself holds for a key ("" when it holds none), and the writer ("" removes the key).
    std::function<std::string(const std::string &)> get;
    std::function<bool(const std::string &, const std::string &, std::string *)> set;
    std::function<void()> commit;  // the kind or a side switched: write the action and redraw
    std::function<void()> typed;   // a field is being typed: save soon
    std::function<void()> flush;   // a field was left
};

// Returns nullptr when the form presses no keys. Object names for tests: keyAnalog, keyNoNeg, keyNoPos, keyRateMin, keyRateMax,
// keyTrigger, keyEngineWords, keyFieldNote.
inline QWidget *keyEngineBox(QWidget *parent, QObject *owner, const KeyEngineEnv &env, const spn::ui::StringProvider &strings) {
    using namespace spn;
    using namespace spn::ui;
    if (!env.form || !env.form->usesKeys()) return nullptr;
    auto *box = new QWidget(parent);
    auto *col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    const AnalogChoice choice = analogChoice(env.motionId, *env.form, strings);
    const KeyEngineWords words = keyEngineWords(choice.analog, env.effective, env.motionId, strings);

    auto *analog = new QCheckBox(T("Analog (the key repeats faster the further you push)"), box);
    analog->setObjectName(QStringLiteral("keyAnalog"));
    analog->setProperty("look", QStringLiteral("box"));
    analog->setChecked(choice.analog);
    analog->setEnabled(choice.allowed);
    analog->setAccessibleName(T("Analog (the key repeats faster the further you push)"));
    if (!choice.allowed) analog->setToolTip(qs8(choice.reason));
    col->addWidget(analog);
    if (!choice.allowed) {
        auto *why = new QLabel(qs8(choice.reason), box);
        why->setObjectName(QStringLiteral("keyAnalogReason"));
        why->setWordWrap(true);
        why->setTextFormat(Qt::PlainText);
        col->addWidget(why);
    }
    auto *mode = new QLabel(qs8(words.mode), box);
    mode->setObjectName(QStringLiteral("keyEngineWords"));
    mode->setWordWrap(true);
    mode->setTextFormat(Qt::PlainText);
    col->addWidget(mode);

    // "No key in this direction": the side presses nothing (none)
    for (int side = 0; side < 2; ++side) {
        std::string ChannelForm::*chord = side == 0 ? &ChannelForm::neg : &ChannelForm::pos;
        const bool none = env.form->*chord == "none";
        auto *cb = new QCheckBox(side == 0 ? T("No key for the negative direction") : T("No key for the positive direction"), box);
        cb->setObjectName(side == 0 ? QStringLiteral("keyNoNeg") : QStringLiteral("keyNoPos"));
        cb->setProperty("look", QStringLiteral("box"));
        cb->setChecked(none);
        const std::string other = env.form->*(side == 0 ? &ChannelForm::pos : &ChannelForm::neg);
        cb->setEnabled(none || other != "none");  // at least one side keeps a key
        cb->setAccessibleName(cb->text());
        col->addWidget(cb);
        QObject::connect(cb, &QCheckBox::clicked, owner, [env, chord](bool on) {
            env.form->*chord = on ? "none" : (chord == &ChannelForm::neg ? "up" : "down");
            if (env.commit) env.commit();
        });
    }

    // the rate and trigger fields: empty = not set (the behavior of older versions)
    auto *fieldsHost = new QWidget(box);  // a widget of its own: the layouts around are emptied by takeAt()
    auto *fields = new QFormLayout(fieldsHost);
    fields->setContentsMargins(0, 0, 0, 0);
    auto addField = [&](KeyField f, const QString &label, const char *objectName) {
        const std::string key = keyEngineKey(env.motionId, f);
        auto *edit = new QLineEdit(qs8(env.get ? env.get(key) : std::string()), box);
        edit->setObjectName(QString::fromLatin1(objectName));
        edit->setAccessibleName(label);
        edit->setToolTip(qs8(key));
        const int km = keyMotionOf(env.motionId);
        if (km >= 0) {
            const KeyTuning &t = env.effective.key_tuning[km];
            const bool has = f == KeyField::RateMin ? (t.set & KeyTuning::F_RATE_MIN) : f == KeyField::RateMax ? (t.set & KeyTuning::F_RATE_MAX) : (t.set & KeyTuning::F_TRIGGER);
            const double v = f == KeyField::RateMin ? t.rate_min : f == KeyField::RateMax ? t.rate_max : t.trigger;
            const double shown = has ? v : f == KeyField::Trigger ? env.effective.step_trigger : -1.0;
            if (shown >= 0.0) edit->setPlaceholderText(QString::number(shown, 'g', 6));  // what applies when the field is empty
        }
        fields->addRow(label + QLatin1Char(':'), edit);
        auto *note = new QLabel(box);
        note->setObjectName(QStringLiteral("keyFieldNote"));
        note->setWordWrap(true);
        note->setTextFormat(Qt::PlainText);
        note->hide();
        QObject::connect(edit, &QLineEdit::textEdited, owner, [env, key, note](const QString &text) {
            std::string err;
            const std::string value = text.trimmed().toUtf8().toStdString();
            if (!value.empty() && !checkKeyField(key, value, &err)) {  // shown next to the field, never written
                note->setText(mark(StatusKind::Error, qs8(err)));
                note->show();
                return;
            }
            note->hide();
            if (env.set && !env.set(key, value, &err)) {
                note->setText(mark(StatusKind::Error, qs8(err)));
                note->show();
                return;
            }
            if (env.typed) env.typed();
        });
        QObject::connect(edit, &QLineEdit::editingFinished, owner, [env] {
            if (env.flush) env.flush();
        });
        fields->addRow(note);
    };
    if (choice.analog) {
        addField(KeyField::RateMin, qs8(words.rateMin), "keyRateMin");
        addField(KeyField::RateMax, qs8(words.rateMax), "keyRateMax");
    }
    addField(KeyField::Trigger, qs8(words.trigger), "keyTrigger");
    col->addWidget(fieldsHost);
    auto *defaults = new QLabel(qs8(words.defaults), box);
    defaults->setObjectName(QStringLiteral("keyDefaultsWords"));
    defaults->setWordWrap(true);
    defaults->setTextFormat(Qt::PlainText);
    col->addWidget(defaults);

    QObject::connect(analog, &QCheckBox::clicked, owner, [env](bool on) {
        if (!setAnalog(env.form, on)) return;
        if (on && env.get && env.set)  // a new analog function starts with the UI defaults (3 to 25 presses a second)
            for (const auto &kv : analogStartValues(env.motionId, env.get)) env.set(kv.first, kv.second, nullptr);
        if (env.commit) env.commit();
    });
    return box;
}
