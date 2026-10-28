// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Boundary values of the axis assistant flow (WizardController, src/ui/model/axes_model.hpp) to the nanosecond: the
// start threshold (strictly above), the arming rest, the longest step, the automatic skip of an optional step, the
// "no movement" note, the time a result is shown, the sample bound, the paused timers without a connection, and the
// buttons that must refuse in the wrong state. test_ui_axes.cpp covers the flow with realistic 33 ms frames; this
// file pins the exact edges. No toolkit, no feed.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "ui/model/axes_model.hpp"

using namespace spn;
using namespace spn::ui;

static constexpr int64_t MS = 1000000, S = 1000000000;

static RawAxes z(int v) {
    RawAxes a{};
    a[AX_Z] = v;
    return a;
}

// Drives one valid out-and-back measurement on `axis` and rests until it ends. Returns the time after it.
static int64_t completeStep(WizardController &w, int64_t t, int axis, int sign) {
    for (int i = 0; i < 10; i++) { t += 33 * MS; w.onSample(RawAxes{}, t); w.tick(t); }
    for (int i = 0; i < 24; i++) {
        RawAxes a{};
        a[size_t(axis)] = (i < 12 ? sign : -sign) * 200;
        t += 33 * MS; w.onSample(a, t); w.tick(t);
    }
    for (int i = 0; i < 40 && w.measuring(); i++) { t += 33 * MS; w.onSample(RawAxes{}, t); w.tick(t); }
    assert(w.phase() == WizardPhase::Detected);
    return t;
}

static void testStartThresholdAndArming() {
    {  // exactly the threshold is not a movement, one above is
        WizardController w;
        w.onSample(RawAxes{}, 0);  // quiet since 0
        const int th = w.tuning().start_threshold;
        w.onSample(z(th), 300 * MS);
        w.onSample(z(th), 333 * MS);
        w.onSample(z(-th), 366 * MS);
        assert(w.waiting());
        w.onSample(z(th + 1), 400 * MS);
        assert(w.waiting());  // MIN_RUN = 2: one sample is a spike
        w.onSample(z(th + 1), 433 * MS);
        assert(w.measuring());
        assert(w.sampleCount() == 2);
    }
    {  // armed after exactly kArmRestNs of rest, not one nanosecond before
        WizardController w;
        w.onSample(RawAxes{}, 1000);
        w.onSample(z(200), 1000 + WizardController::kArmRestNs - 1);  // too early: resets the rest
        w.onSample(z(200), 1000 + WizardController::kArmRestNs);
        assert(w.waiting());  // still moving from before, the rest restarts
        WizardController v;
        v.onSample(RawAxes{}, 1000);
        v.onSample(z(200), 1000 + WizardController::kArmRestNs);
        v.onSample(z(200), 1000 + WizardController::kArmRestNs + 1);
        assert(v.measuring());
    }
    {  // a movement from before the rest never counts, however long (no rest seen: not armed)
        WizardController w;
        for (int i = 0; i < 100; i++) w.onSample(z(200), int64_t(i) * 33 * MS);
        assert(w.waiting() && w.sampleCount() == 0);
    }
    {  // the configured threshold is the boundary, not the default
        WizardController w;
        AxisAssistantTuning t;
        t.start_threshold = 100;
        w.setTuning(t);
        w.onSample(RawAxes{}, 0);
        w.onSample(z(100), 300 * MS);
        w.onSample(z(100), 333 * MS);
        assert(w.waiting());
        w.onSample(z(101), 366 * MS);
        w.onSample(z(101), 399 * MS);
        assert(w.measuring());
    }
}

static void testLongestStep() {
    WizardController w;
    assert(w.start(5 * S, true) && w.measuring());
    const int64_t max = w.tuning().maxNs();
    assert(!w.tick(5 * S + max - 1) && w.measuring());
    assert(w.tick(5 * S + max) && !w.measuring());
    // no samples: nothing detected, the step stays and says it took too long
    assert(w.done(0) && !w.stepResult(0).valid && w.waiting() && w.step() == 0);
    assert(w.note().find("too long") != std::string::npos);
    // progress and seconds left
    WizardController v;
    v.start(0, true);
    assert(v.progress(0) == 0.0 && v.progress(max / 2) == 0.5 && v.progress(max) == 1.0 && v.progress(10 * max) == 1.0 && v.progress(-5) == 0.0);
    assert(v.secondsLeft(0) == double(w.tuning().max_s) && v.secondsLeft(max) == 0.0 && v.secondsLeft(2 * max) == 0.0);
    assert(WizardController().secondsLeft(0) == 0.0 && WizardController().progress(0) == 0.0);
    // the minimum measuring time: a step with samples cannot end earlier than kMinMeasureNs by rest
    WizardController m;
    m.start(0, true);
    for (int i = 0; i < 8; i++) m.onSample(z(i < 4 ? 200 : -200), int64_t(i) * 10 * MS);  // activity ends at 70 ms
    assert(!m.tick(WizardController::kMinMeasureNs - 1) && m.measuring());
}

static void testSampleBound() {
    WizardController w;
    w.start(0, true);
    for (size_t i = 0; i < WizardController::kMaxSamples + 500; i++) w.onSample(z(200), int64_t(i));
    assert(w.sampleCount() == WizardController::kMaxSamples);
}

static void testWaitingTimers() {
    {  // a required step: the note appears at exactly max_s, once, and the step is never skipped
        WizardController w;
        const int64_t max = w.tuning().maxNs();
        assert(!w.tick(max - 1) && w.note().empty());
        assert(w.tick(max) && !w.note().empty());
        const std::string n = w.note();
        assert(!w.tick(max + 1) && !w.tick(100 * S) && w.note() == n && w.step() == 0 && !w.skipped(0));
        assert(w.canAgain() && w.again(100 * S) && w.note().empty());  // again clears it and restarts the timer
        assert(!w.tick(100 * S + max - 1) && w.tick(100 * S + max));
    }
    {  // an optional step is skipped at exactly auto_skip_s, the next one gets a fresh timer
        WizardController w;
        int64_t t = completeStep(w, 0, AX_Z, 1);
        assert(w.next() && w.step() == 1 && w.canSkip());
        const int64_t as = w.tuning().autoSkipNs();
        assert(!w.tick(t + as - 1) && w.step() == 1 && !w.skipped(1));
        assert(w.tick(t + as) && w.step() == 2 && w.skipped(1) && !w.notice().empty());
        // step 2 is required: its timer starts at the skip, nothing is skipped
        assert(!w.tick(t + as + w.tuning().maxNs() - 1) && w.step() == 2);
        assert(w.tick(t + as + w.tuning().maxNs()) && w.step() == 2 && !w.skipped(2));
    }
    {  // tuning boundaries are honoured
        WizardController w;
        AxisAssistantTuning tu;
        tu.auto_skip_s = kAutoSkipSMin;
        tu.max_s = kMaxSMin;
        w.setTuning(tu);
        int64_t t = completeStep(w, 0, AX_Z, 1);
        assert(w.next() && !w.tick(t + 2 * S - 1) && w.tick(t + 2 * S) && w.skipped(1));
    }
    {  // without a connection the timers stand still and restart from the reconnect
        WizardController w;
        w.setConnected(false);
        assert(!w.tick(1000 * S) && w.note().empty() && !w.canStart(false));
        w.setConnected(true);
        // the timer restarts from the last tick made while disconnected, not from the start of the step
        assert(!w.tick(1000 * S + w.tuning().maxNs() - 1) && w.note().empty());
        assert(w.tick(1000 * S + w.tuning().maxNs()) && !w.note().empty());
    }
    {  // connectionLost: waiting only pauses; measuring aborts with a note and keeps the step
        WizardController w;
        w.connectionLost();
        assert(w.waiting() && w.note().empty() && w.step() == 0);
        WizardController v;
        v.start(0, true);
        v.onSample(z(200), 10 * MS);
        v.connectionLost();
        assert(v.waiting() && !v.done(0) && v.sampleCount() == 0 && v.note().find("lost") != std::string::npos);
        assert(!v.canStart(false) && !v.start(0, false) && v.waiting());
    }
}

static void testAdvanceAfterResult() {
    WizardController w;
    const int64_t t = completeStep(w, 0, AX_Z, 1);
    // the result is shown for exactly kAdvanceNs from its detection; the sample feed may keep going
    const int64_t shown = t;  // completeStep returns the time of the tick that detected it
    assert(!w.tick(shown + WizardController::kAdvanceNs - 1) && w.phase() == WizardPhase::Detected && w.step() == 0);
    assert(w.tick(shown + WizardController::kAdvanceNs) && w.step() == 1 && w.waiting());
    // the run to the result page by buttons only; a skipped optional step needs no movement
    assert(w.skip() && w.step() == 2);
    assert(!w.canSkip() && !w.skip() && w.step() == 2);  // a required step cannot be skipped
    int64_t u = t + 10 * S;
    u = completeStep(w, u, AX_X, 1);
    assert(w.next() && w.step() == 3 && w.skip() && w.step() == 4);
    completeStep(w, u + 10 * S, AX_RY, 1);
    assert(w.next() && w.onResultPage() && w.phase() == WizardPhase::ResultPage);
    // on the result page nothing starts, skips, repeats or advances; back and apply work
    assert(!w.canStart(true) && !w.start(0, true) && !w.canAgain() && !w.again(0) && !w.canSkip() && !w.canNext() && !w.next());
    assert(w.canApply() == w.final().ok && !w.tick(1000 * S) && w.onResultPage());
    assert(w.canBack() && w.back() && w.step() == 4);
    // goTo never leaves the range: back from step 0 refuses
    WizardController f;
    assert(!f.canBack() && !f.back() && f.step() == 0 && !f.canNext() && !f.next());
}

int main() {
    testStartThresholdAndArming();
    testLongestStep();
    testSampleBound();
    testWaitingTimers();
    testAdvanceAfterResult();
    std::puts("test_wizard_boundaries: OK");
    return 0;
}
