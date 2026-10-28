// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "autosaver.hpp"

#include <algorithm>
#include <vector>

#include "a11y_announce.hpp"
#include "ui_i18n.hpp"

namespace {
std::vector<Autosaver *> &registry() {
    static std::vector<Autosaver *> r;
    return r;
}
}  // namespace

Autosaver::Autosaver(spn::ui::Autosave::DirtyFn dirty, spn::ui::Autosave::SaveFn save,
                     const spn::ui::StringProvider &strings, QObject *parent)
    : QObject(parent), rule_(std::move(dirty), std::move(save)), strings_(strings) {
    clock_.start();
    timer_.setSingleShot(true);
    statusTimer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, [this] { onTimer(); });
    connect(&statusTimer_, &QTimer::timeout, this, [this] {
        const QString t = qs8(rule_.statusText(now(), strings_));
        if (t != status_) {
            status_ = t;
            emit statusChanged(status_, failed());
        }
    });
    registry().push_back(this);
}

Autosaver::~Autosaver() { registry().erase(std::remove(registry().begin(), registry().end(), this), registry().end()); }

void Autosaver::schedule() {
    const qint64 ms = rule_.msUntilDue(now());
    if (ms >= 0) timer_.start(static_cast<int>(ms));
}

void Autosaver::requestImmediate() {
    rule_.request(spn::ui::Autosave::Urgency::Immediate, now());
    schedule();
}

void Autosaver::requestDebounced() {
    rule_.request(spn::ui::Autosave::Urgency::Debounced, now());
    schedule();
}

void Autosaver::onTimer() {
    if (rule_.tick(now())) afterWrite();
    else if (rule_.pending()) schedule();  // the timer fired a little early
}

bool Autosaver::flush() {
    timer_.stop();
    if (rule_.flush(now())) afterWrite();
    return !failed();
}

void Autosaver::afterWrite() {
    status_ = qs8(rule_.statusText(now(), strings_));
    emit statusChanged(status_, failed());
    if (failed()) spn::ui::announce(nullptr, status_);  // a screen reader hears a failed write; "Saved" stays quiet
    emit written();
    if (!failed()) statusTimer_.start(static_cast<int>(spn::ui::Autosave::kStatusMs) + 50);
}

void Autosaver::report(const QString &text, bool error) {
    status_ = text;
    emit statusChanged(status_, error);
    spn::ui::announce(nullptr, text);  // results of import, export, take over and errors of an edit
    if (!error && !text.isEmpty()) statusTimer_.start(static_cast<int>(spn::ui::Autosave::kStatusMs) + 50);
}

bool Autosaver::flushAll() {
    bool ok = true;
    const std::vector<Autosaver *> all = registry();  // a flush may add or remove writers
    for (Autosaver *a : all)
        if (std::find(registry().begin(), registry().end(), a) != registry().end()) ok = a->flush() && ok;
    return ok;
}
