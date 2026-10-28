// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/ui/model/axes_model.hpp: bar scaling, peak hold, pause, connection states and
// the axis assistant driven through a fake live feed (no daemon, no sockets, no Qt).
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>

#include "testutil.hpp"
#include "ui/model/axes_model.hpp"
#include "ui/model/fresh_state.hpp"
#include "ui/model/ui_settings.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
constexpr int64_t MS = 1000000;

struct FakeConn : FeedConn {
    std::deque<uint8_t> in;
    bool broken = false;
    long read(uint8_t *b, size_t n) override {
        if (broken) return -1;
        size_t k = 0;
        while (k < n && !in.empty()) {
            b[k++] = in.front();
            in.pop_front();
        }
        return static_cast<long>(k);
    }
    long write(const uint8_t *, size_t) override { return broken ? -1 : 0; }
};

// A scripted live feed: the client connects to a new FakeConn whenever `up` is set.
struct FakeFeed {
    FakeConn *conn = nullptr;
    bool up = true;
    int attempts = 0;
    LiveFeedClient::Connector connector() {
        return [this](std::string *err) -> std::unique_ptr<FeedConn> {
            attempts++;
            if (!up) {
                *err = "refused";
                return nullptr;
            }
            auto c = std::make_unique<FakeConn>();
            conn = c.get();
            return c;
        };
    }
    void pushDevices(const std::vector<LiveDevice> &d) {
        for (uint8_t b : encodeLiveDevices(d)) conn->in.push_back(b);
    }
    void push(const RawAxes &a, uint32_t buttons = 0, int source = kLiveNoSource) {
        LiveSample s;
        s.source = source;
        for (uint8_t b : encodeLiveSource(source)) conn->in.push_back(b);
        s.axes = a;
        s.buttons = buttons;
        s.mode = "normal";
        for (uint8_t b : encodeLiveSample(s)) conn->in.push_back(b);
    }
};

RawAxes ax(int x, int y, int z, int rx, int ry, int rz) { return RawAxes{x, y, z, rx, ry, rz}; }

// Pushes `n` samples of `a` (one every 33 ms) through the feed and the session.
void feed(FakeFeed &f, AxesSession &s, int64_t &t, int n, const RawAxes &a = RawAxes{}) {
    for (int i = 0; i < n; i++) {
        f.push(a);
        t += 33 * MS;
        s.poll(t);
    }
}

// Moves `axis` first in direction `first` (+1/-1), then back, after a short rest (the assistant
// starts by movement), and goes on at rest until the step has ended. No click, no start().
void measure(FakeFeed &f, AxesSession &s, int64_t &t, int axis, int first, int v = 200) {
    feed(f, s, t, 10);  // rest: the step arms
    for (int i = 0; i < 30; i++) {
        RawAxes a{};
        a[size_t(axis)] = (i < 15 ? first : -first) * v;
        f.push(a);
        t += 33 * MS;
        s.poll(t);
    }
    for (int i = 0; i < 100 && !s.wizard().done(s.wizard().step()); i++) feed(f, s, t, 1);
    assert(!s.wizard().measuring() && s.wizard().done(s.wizard().step()));
}
}  // namespace

static void testUtf8() {
    const std::string dev = "SpaceMouse \xC3\xBC \xE7\xA9\xBA\xE9\x97\xB4 \xF0\x9F\x96\xB1 \"x\"";
    assert(sanitizeUtf8(dev) == dev);  // valid Unicode, spaces and symbols stay
    assert(sanitizeUtf8("a\xFF" "b") == "a\xEF\xBF\xBD" "b");
    assert(sanitizeUtf8("\xC0\x80") == "\xEF\xBF\xBD\xEF\xBF\xBD");  // overlong
    assert(sanitizeUtf8("\xED\xA0\x80").size() == 9);                  // surrogate
    assert(sanitizeUtf8("\xE7\xA9") == "\xEF\xBF\xBD\xEF\xBF\xBD");  // truncated sequence
    assert(sanitizeUtf8("a\nb") == "a\xEF\xBF\xBD" "b");
    // truncation never splits a sequence ("ue" is 2 bytes, the CJK characters 3, the emoji 4)
    for (size_t n = 0; n < dev.size() + 2; n++) {
        const std::string t = truncateUtf8(dev, n);
        assert(t.size() <= n && sanitizeUtf8(t) == t && dev.compare(0, t.size(), t) == 0);
    }
    // a Unicode mode/device text goes through the live feed model untouched or sanitized
    LiveSample s;
    s.mode = "normal";
    assert(sanitizeUtf8(s.mode) == "normal");
}

static void testScaling() {
    assert(axisFullScale(0) == 350 && axisFullScale(350) == 350 && axisFullScale(351) == 512);
    assert(axisFullScale(100000) == 100000 && axisFullScale(1 << 30) == 100000);
    assert(barFraction(175, 350) == 0.5 && barFraction(-9999, 350) == -1.0 && barFraction(5, 0) == 0.0);
}

static void testPeakHoldAndPause() {
    PeakHold h;
    h.update(ax(0, 0, 100, 0, 0, 0), 0);
    h.update(ax(0, 0, 40, 0, 0, 0), 100 * MS);
    assert(h.held(AX_Z) == 100);                 // held
    h.update(ax(0, 0, 40, 0, 0, 0), 2000 * MS);  // hold time over: starts falling
    assert(h.held(AX_Z) < 100 && h.held(AX_Z) >= 40);
    h.update(ax(0, 0, 40, 0, 0, 0), 60000 * MS);
    assert(h.held(AX_Z) == 40);  // fell back to the current value
    h.update(ax(0, 0, -300, 0, 0, 0), 61000 * MS);
    assert(h.held(AX_Z) == -300);  // signed

    AxesViewModel v;
    LiveSample s;
    s.axes = ax(10, 0, 0, 0, 0, 0);
    v.onSample(s, 0);
    v.setPaused(true);
    s.axes = ax(300, 0, 0, 0, 0, 0);
    s.buttons = 4;
    v.onSample(s, MS);
    assert(v.shown().axes[AX_X] == 10 && v.paused());  // frozen
    assert(v.buttonCount() == 3);                      // but the button still counts as seen
    v.togglePause();
    v.onSample(s, 2 * MS);
    assert(v.shown().axes[AX_X] == 300 && v.buttonDown(3) && !v.buttonDown(1));
    s.axes = ax(900, 0, 0, 0, 0, 0);
    v.onSample(s, 3 * MS);
    assert(v.fullScale() == 1024);
    v.resetPeaks();
    assert(v.fullScale() == 1024 && v.peakValue(AX_X) == 0);
    assert(channelFullScale(0) == 10 && channelFullScale(11) == 25 && channelFullScale(30) == 50 &&
           channelFullScale(60) == 100 && channelFullScale(1e30) > 1e9);
    s.v = 60;
    s.zoom = -20;
    v.onSample(s, 4 * MS);
    assert(v.channelFraction(0) == 0.6 && v.channelFraction(2) == -0.2 && v.channelFraction(1) == 0.0);
    v.setKnownButtonCount(6);
    assert(v.buttonCount() == 6);
}

static void testConnection() {
    FakeFeed f;
    f.up = false;
    LiveFeedClient client(f.connector());
    AxesSession s(client);
    int64_t t = 0;
    s.poll(t);
    assert(s.connection(t) == ConnState::DaemonNotRunning && f.attempts == 1);
    for (int i = 0; i < 20; i++) s.poll(t += 10 * MS);  // backoff, no busy loop
    assert(f.attempts == 1);
    assert(std::strstr(connectionHint(ConnState::DaemonNotRunning, HostOs::Linux), "systemctl"));
    assert(std::strstr(connectionHint(ConnState::DaemonNotRunning, HostOs::Windows), "dofscroll.exe"));
    assert(std::strlen(connectionHint(ConnState::DaemonNotRunning, HostOs::MacOs)) > 0);
    assert(connectionHint(ConnState::Connected, HostOs::Linux)[0] == '\0');

    f.up = true;
    s.poll(t += 600 * MS);
    assert(client.state() == LiveFeedClient::State::Connected);
    assert(s.connection(t) == ConnState::WaitingForFeed);  // transport up, no sample yet
    f.push(ax(1, 2, 3, 4, 5, 6), 1);
    s.poll(t += 33 * MS);
    assert(s.connection(t) == ConnState::Connected && s.view().shown().axes[AX_RZ] == 6);
    assert(s.connection(t + 2000 * MS) == ConnState::WaitingForFeed);  // stale
    assert(s.view().deviceLabel().empty() && s.view().buttonCount() == 2);  // no info frame yet
    {  // device info: name and factory button count (Unicode name, two devices)
        LiveDevice a, b;
        a.id = 1, a.name = "SpaceMouse Pro \xC3\xA4", a.reportedButtons = 2, a.factoryButtons = 15;
        b.id = 2, b.name = "Compact", b.reportedButtons = 2, b.factoryButtons = 2;
        f.pushDevices({a, b});
        f.push(ax(1, 2, 3, 4, 5, 6), 1);
        s.poll(t += 33 * MS);
        assert(s.view().deviceLabel() == "SpaceMouse Pro \xC3\xA4, Compact" && s.view().buttonCount() == 15);
        f.pushDevices({});  // device unplugged
        s.poll(t += 33 * MS);
        assert(s.view().deviceLabel().empty() && s.view().buttonCount() == 2);
        f.pushDevices({a});
        s.poll(t += 33 * MS);
        assert(s.view().buttonCount() == 15);
    }
    f.conn->broken = true;
    s.poll(t += 33 * MS);
    assert(s.connection(t) == ConnState::DaemonNotRunning);
}

// Device selector (issue #139): the source of a sample, the selection and the label.
static void testDeviceSelector() {
    FakeFeed f;
    LiveFeedClient client(f.connector());
    AxesSession s(client);
    int64_t t = 0;
    LiveDevice a, b;
    a.id = 1, a.name = "Pro", a.reportedButtons = 2, a.factoryButtons = 15;
    b.id = 2, b.name = "Compact", b.reportedButtons = 2, b.factoryButtons = 2;
    s.poll(t += 600 * MS);
    assert(s.selectableDevices().empty() && s.selectedDevice() == AxesSession::kAllDevices);
    f.pushDevices({a});  // one device: nothing to choose
    f.push(ax(1, 0, 0, 0, 0, 0), 0, 1);
    s.poll(t += 33 * MS);
    assert(s.selectableDevices().empty() && s.view().deviceLabel() == "Pro");
    f.pushDevices({a, b});
    f.push(ax(10, 0, 0, 0, 0, 0), 0, 2);
    s.poll(t += 33 * MS);
    assert(s.selectableDevices().size() == 2);
    // all devices: every sample is shown, the label names the device that moved last
    assert(s.view().shown().axes[AX_X] == 10 && s.view().shown().source == 2);
    assert(s.view().deviceLabel() == "Compact" && s.view().buttonCount() == 15);
    f.push(ax(20, 0, 0, 0, 0, 0), 0, 1);
    s.poll(t += 33 * MS);
    assert(s.view().shown().axes[AX_X] == 20 && s.view().deviceLabel() == "Pro");
    // pin device 2: samples of device 1 are dropped, the label and the button count follow
    assert(s.selectDevice(2) && s.selectedDevice() == 2);
    f.push(ax(30, 0, 0, 0, 0, 0), 0, 1);
    s.poll(t += 33 * MS);
    assert(s.view().shown().axes[AX_X] == 20);  // unchanged
    assert(s.view().deviceLabel() == "Compact" && s.view().buttonCount() == 2);
    assert(s.connection(t) == ConnState::Connected);  // the dropped sample still keeps the link alive
    f.push(ax(40, 0, 0, 0, 0, 0), 0, 2);
    f.push(ax(50, 0, 0, 0, 0, 0));  // unknown source always passes
    s.poll(t += 33 * MS);
    assert(s.view().shown().axes[AX_X] == 50);
    assert(!s.selectDevice(9) && s.selectedDevice() == 2);  // not in the list: kept
    assert(s.selectDevice(AxesSession::kAllDevices) && s.selectedDevice() == AxesSession::kAllDevices);
    // the pinned device is unplugged: back to all devices
    assert(s.selectDevice(1));
    f.pushDevices({b});
    s.poll(t += 33 * MS);
    assert(s.selectedDevice() == AxesSession::kAllDevices);
    // the assistant only sees the pinned device
    f.pushDevices({a, b});
    s.poll(t += 33 * MS);
    assert(s.selectDevice(1));
    s.wizard().reset();
    feed(f, s, t, 1);
    const uint64_t before = s.feed().sampleCount();
    f.push(ax(300, 0, 0, 0, 0, 0), 0, 2);
    s.poll(t += 33 * MS);
    assert(s.feed().sampleCount() == before + 1 && s.view().shown().axes[AX_X] != 300);
}

static void testWizard() {
    FakeFeed f;
    LiveFeedClient client(f.connector());
    AxesSession s(client);
    int64_t t = 0;
    s.poll(t);
    f.push(ax(0, 0, 0, 0, 0, 0));
    s.poll(t += 33 * MS);
    WizardController &w = s.wizard();
    assert(w.step() == 0 && !w.canNext() && !w.canBack() && !w.canSkip() && w.canStart(s.connected(t)));

    measure(f, s, t, AX_Z, +1);
    assert(w.stepResult(0).valid && w.stepResult(0).axis == AX_Z && w.stepResult(0).sign == 1);
    assert(w.canNext() && w.next() && w.step() == 1);
    assert(w.canSkip() && w.skip() && w.skipped(1) && w.step() == 2);
    measure(f, s, t, AX_X, -1, 150);
    assert(w.next() && w.step() == 3 && w.skip() && w.step() == 4);
    assert(!w.canSkip());  // twist is required
    measure(f, s, t, AX_RY, +1, 250);
    assert(w.next() && w.onResultPage());
    assert(w.final().ok && w.final().mix_v == "z" && w.final().mix_h == "-x" && w.final().mix_zoom == "ry");
    assert(w.canApply() && !w.canStart(true));

    // Apply writes the three mixes and resets the signs in [global] only.
    SettingsModel m;
    m.setProfiles(ProfileSet(), "unused.conf");
    std::string err;
    assert(w.applyTo(m, &err));
    SettingsModel::Value v;
    assert(m.value(0, "SCROLL_V_MIX", &v) && v.text == "z");
    assert(m.value(0, "SCROLL_H_MIX", &v) && v.text == "-x");
    assert(m.value(0, "ZOOM_MIX", &v) && v.text == "ry");
    assert(m.value(0, "ZOOM_SIGN", &v) && v.text == "1");
    assert(m.value(0, "ZOOM_LATERAL", &v) && v.text == "x z");  // the scroll axes, never the twist

    // Going back to repeat a step is possible; an incomplete mapping cannot be applied and leaves
    // the model alone (cancel = never calling applyTo).
    assert(w.back() && w.step() == 4);
    WizardController w2;
    SettingsModel untouched;
    untouched.setProfiles(ProfileSet(), "unused.conf");
    assert(!w2.applyTo(untouched, &err));
    assert(!untouched.dirty());
}

static void testWizardEdgeCases() {
    FakeFeed f;
    LiveFeedClient client(f.connector());
    AxesSession s(client);
    int64_t t = 0;
    s.poll(t);
    f.push(ax(0, 0, 0, 0, 0, 0));
    s.poll(t += 33 * MS);
    WizardController &w = s.wizard();

    // A weak movement is reported and does not allow "next".
    measure(f, s, t, AX_Z, +1, 60);  // above the start threshold, below the minimum peak
    assert(w.stepResult(0).too_weak && !w.stepResult(0).valid && !w.canNext());
    assert(!w.stepResult(0).message.empty());

    // Connection lost while measuring aborts with a note.
    assert(w.start(t, s.connected(t)) && w.measuring());
    f.push(ax(0, 0, 200, 0, 0, 0));
    s.poll(t += 33 * MS);
    f.conn->broken = true;
    s.poll(t += 33 * MS);
    assert(!w.measuring() && !w.note().empty() && !w.done(0));

    // The same axis for two channels is an error with the formatted (translatable) message.
    std::array<wiz::StepResult, wiz::ROLE_COUNT> res{};
    for (int r : {int(wiz::ROLE_PUSH_V), int(wiz::ROLE_PUSH_H), int(wiz::ROLE_TWIST)}) {
        res[size_t(r)].valid = true;
        res[size_t(r)].axis = AX_Z;
        res[size_t(r)].sign = 1;
    }
    std::array<bool, wiz::ROLE_COUNT> sk{};
    sk[wiz::ROLE_TILT_V] = sk[wiz::ROLE_TILT_H] = true;
    const wiz::WizardResult wr = wiz::evaluate(res, sk);
    assert(!wr.ok && !wr.errors.empty() && wr.errors[0].find("Axis z") != std::string::npos);
}

// Frames the test steps by hand: a controller driven with synthetic samples and a fake clock.
struct Clock {
    WizardController w;
    int64_t t = 0;
    void rest(int n) {
        for (int i = 0; i < n; i++) {
            t += 33 * MS;
            w.onSample(RawAxes{}, t);
            w.tick(t);
        }
    }
    void hold(const RawAxes &a, int n) {
        for (int i = 0; i < n; i++) {
            t += 33 * MS;
            w.onSample(a, t);
            w.tick(t);
        }
    }
    // out and back on one axis
    void outBack(int axis, int first, int v = 200, int n = 12) {
        RawAxes o{}, b{};
        o[size_t(axis)] = first * v;
        b[size_t(axis)] = -first * v;
        hold(o, n);
        hold(b, n);
    }
};

static void testWizardByMovementOnly() {
    FakeFeed f;
    LiveFeedClient client(f.connector());
    AxesSession s(client);
    int64_t t = 0;
    s.poll(t);
    f.push(ax(0, 0, 0, 0, 0, 0));
    s.poll(t += 33 * MS);
    WizardController &w = s.wizard();
    assert(w.waiting() && w.step() == 0 && wizardStateText(w, true) == "Waiting for you to move the cap...");

    // The whole run: only the cap moves. No start(), next() or skip() is called.
    measure(f, s, t, AX_Z, +1);
    assert(w.phase() == WizardPhase::Detected && w.stepResult(0).valid);
    assert(wizardStateText(w, true) == "Detected");
    feed(f, s, t, 40);  // shown for about a second, then the next step starts by itself
    assert(w.step() == 1 && w.waiting());
    feed(f, s, t, 170);  // optional: no movement for 5 s -> skipped by itself
    assert(w.step() == 2 && w.skipped(1) && !w.notice().empty());
    assert(w.notice().find("skipped, no movement") != std::string::npos);
    measure(f, s, t, AX_X, -1, 150);
    feed(f, s, t, 40);
    assert(w.step() == 3);
    feed(f, s, t, 170);
    assert(w.step() == 4 && w.skipped(3));
    measure(f, s, t, AX_RY, +1, 250);
    feed(f, s, t, 40);
    assert(w.onResultPage() && w.phase() == WizardPhase::ResultPage);
    assert(w.final().ok && w.final().mix_v == "z" && w.final().mix_h == "-x" && w.final().mix_zoom == "ry");
    assert(w.canApply());

    const auto rows = wizardMappingRows(w);
    assert(rows.size() == size_t(WizardController::kSteps));
    assert(rows[0].direction == "Push forward/back" && rows[0].detail == "z, positive");
    assert(rows[1].detail == "Skipped" && rows[2].detail == "x, negative");
    assert(rows[4].detail == "ry, positive");
}

static void testWizardStartAndEnd() {
    // A spike or a movement that is already going on does not start a step.
    {
        Clock c;
        c.rest(10);
        RawAxes a{};
        a[AX_Z] = 200;
        c.hold(a, 1);
        c.rest(10);
        assert(c.w.waiting());  // one sample is a spike
        // Moving from before: no rest seen yet, so it does not start...
        Clock d;
        d.hold(a, 20);
        assert(d.w.waiting());
        d.rest(10);  // ...until the cap rested
        d.outBack(AX_Z, 1);
        assert(d.w.measuring() || d.w.done(0));
    }
    // Ends by rest after out and back (not before the rest time), well before the longest step.
    {
        Clock c;
        c.rest(10);
        c.outBack(AX_Z, 1);
        assert(c.w.measuring() && c.w.halves() >= 1);
        c.rest(8);  // 264 ms: not yet
        assert(c.w.measuring());
        c.rest(8);  // > 400 ms
        assert(c.w.phase() == WizardPhase::Detected && c.w.stepResult(0).sign == 1);
    }
    // A pause between out and back does not end the step (only one half-movement so far).
    {
        Clock c;
        c.rest(10);
        RawAxes o{}, b{};
        o[AX_Z] = 200;
        b[AX_Z] = -200;
        c.hold(o, 12);
        c.rest(20);  // 660 ms of rest, but only one half-movement
        assert(c.w.measuring());
        c.hold(b, 12);
        c.rest(16);
        assert(c.w.phase() == WizardPhase::Detected);
    }
    // One half-movement only: ends after a long rest, the sign is the first movement.
    {
        Clock c;
        c.rest(10);
        RawAxes o{};
        o[AX_Z] = -200;
        c.hold(o, 15);
        c.rest(40);
        assert(c.w.measuring());
        c.rest(40);  // > 2 s
        assert(c.w.phase() == WizardPhase::Detected && c.w.stepResult(0).sign == -1);
    }
    // Longest step: a movement that never rests ends after max_s; a weak one says what to do.
    {
        Clock c;
        c.rest(10);
        RawAxes weak{};
        weak[AX_Z] = 60;
        c.hold(weak, 300);  // 9.9 s
        assert(c.w.waiting() && c.w.done(0) && !c.w.stepResult(0).valid && !c.w.note().empty());
        assert(std::string(wizardStepHint(c.w.stepResult(0))).find("weak") != std::string::npos);
        assert(!c.w.canNext());
        // the same step measures again with the next movement
        c.rest(10);
        c.outBack(AX_Z, 1);
        c.rest(20);
        assert(c.w.phase() == WizardPhase::Detected && c.w.stepResult(0).valid);
    }
    // Ambiguous or weak: stays on the step, no axis name in the hint.
    {
        Clock c;
        c.rest(10);
        RawAxes both{};
        both[AX_X] = 200;
        both[AX_Z] = 200;
        RawAxes neg{};
        neg[AX_X] = -200;
        neg[AX_Z] = -200;
        c.hold(both, 12);
        c.hold(neg, 12);
        c.hold(RawAxes{}, 20);
        assert(c.w.step() == 0 && c.w.waiting() && c.w.stepResult(0).ambiguous && !c.w.canNext());
        const std::string hint = wizardStepHint(c.w.stepResult(0));
        assert(!hint.empty() && hint.find("only one direction") != std::string::npos);
        c.rest(40);
        assert(c.w.step() == 0);  // a required step never moves on by itself
    }
    // A required step without movement shows a hint after the longest time and keeps waiting.
    {
        Clock c;
        c.rest(100);
        assert(c.w.note().empty());
        c.rest(200);  // > 8 s
        assert(!c.w.note().empty() && c.w.waiting() && c.w.step() == 0 && c.w.canAgain());
    }
    // Without a connection nothing is skipped or timed out.
    {
        Clock c;
        c.w.setConnected(false);
        c.rest(400);
        assert(c.w.step() == 0 && c.w.note().empty());
    }
}

static void testWizardNavigation() {
    Clock c;
    c.rest(10);
    c.outBack(AX_Z, 1);
    c.rest(16);
    assert(c.w.phase() == WizardPhase::Detected);
    // Next skips the one second of showing the result
    assert(c.w.canNext() && c.w.next() && c.w.step() == 1);
    // Back shows the finished step and does not start or advance by itself
    assert(c.w.back() && c.w.step() == 0 && c.w.phase() == WizardPhase::Review);
    c.rest(100);
    c.outBack(AX_Z, -1);
    c.rest(40);
    assert(c.w.step() == 0 && c.w.phase() == WizardPhase::Review && c.w.stepResult(0).sign == 1);
    // Measure again throws the result away and waits for a movement
    assert(c.w.again(c.t) && c.w.waiting() && !c.w.done(0));
    c.rest(10);
    c.outBack(AX_Z, -1);
    c.rest(16);
    assert(c.w.phase() == WizardPhase::Detected && c.w.stepResult(0).sign == -1);
    // The button start still works at once
    Clock d;
    assert(d.w.start(d.t, true) && d.w.measuring());
    assert(!d.w.start(d.t, true));  // already measuring
}

static void testWizardTuning() {
    Clock c;
    AxisAssistantTuning t;
    t.start_threshold = 100;
    t.rest_ms = 1000;
    t.max_s = 4;
    t.auto_skip_s = 2;
    c.w.setTuning(t);
    c.rest(10);
    RawAxes a{};
    a[AX_Z] = 60;
    c.hold(a, 10);
    assert(c.w.waiting());  // below the configured start threshold
    c.outBack(AX_Z, 1);
    assert(c.w.measuring());
    c.rest(20);  // 660 ms: shorter than the configured rest
    assert(c.w.measuring());
    c.rest(20);
    assert(c.w.phase() == WizardPhase::Detected);
    c.rest(40);
    assert(c.w.step() == 1);
    c.rest(70);  // 2 s: optional step skipped
    assert(c.w.step() == 2 && c.w.skipped(1));
    c.w.reset();  // the tuning survives a reset
    assert(c.w.tuning() == t);

    // ui.conf: missing, valid, invalid
    assert(parseUiSettings("").assistant == AxisAssistantTuning());
    UiSettings u = parseUiSettings("axis_assistant_start_threshold = 60\naxis_assistant_rest_ms=700\n"
                                   "axis_assistant_max_s = 12\naxis_assistant_auto_skip_s = 3\n");
    assert(u.assistant.start_threshold == 60 && u.assistant.rest_ms == 700 && u.assistant.max_s == 12 &&
           u.assistant.auto_skip_s == 3);
    u = parseUiSettings("axis_assistant_start_threshold = lots\naxis_assistant_rest_ms = 1.5\n"
                        "axis_assistant_max_s =\naxis_assistant_auto_skip_s = 99999999999\n");
    assert(u.assistant.start_threshold == 40 && u.assistant.rest_ms == 400 && u.assistant.max_s == 8 &&
           u.assistant.auto_skip_s == 5);  // not a number: the default
    u = parseUiSettings("axis_assistant_start_threshold = 0\naxis_assistant_rest_ms = 100000\n"
                        "axis_assistant_max_s = -4\naxis_assistant_auto_skip_s = 1\n");
    assert(u.assistant.start_threshold == kStartThresholdMin && u.assistant.rest_ms == kRestMsMax &&
           u.assistant.max_s == kMaxSMin && u.assistant.auto_skip_s == kAutoSkipSMin);  // clamped
    // written back only when changed, and a hand-set value survives saving another setting
    assert(serializeUiSettings(UiSettings()).find("axis_assistant") == std::string::npos);
    UiSettings x = parseUiSettings("theme = dark\naxis_assistant_rest_ms = 700\n");
    x.language = Language::German;
    const UiSettings y = parseUiSettings(serializeUiSettings(x));
    assert(y.assistant.rest_ms == 700 && y.language == Language::German && y.theme == Theme::Dark);
}

// Apply shows "now -> new" first and can be undone; the plain words, the lift fallback and the new assistant setting.
static void testApplyPreviewUndoAndWords() {
    FakeFeed f;
    LiveFeedClient client(f.connector());
    AxesSession s(client);
    int64_t t = 0;
    s.poll(t);
    f.push(ax(0, 0, 0, 0, 0, 0));
    s.poll(t += 33 * MS);
    WizardController &w = s.wizard();
    measure(f, s, t, AX_Z, +1);
    assert(w.next() && w.skip());
    measure(f, s, t, AX_X, -1, 150);
    assert(w.next() && w.skip());
    measure(f, s, t, AX_RY, +1, 250);
    assert(w.next() && w.onResultPage());

    SettingsModel m;
    m.setProfiles(ProfileSet(), "unused.conf");
    std::string err;
    assert(m.setValue(0, "SCROLL_V_SIGN", "-1", &err));
    const std::vector<WizardChangeRow> rows = wizardChangeRows(m, w);
    assert(rows.size() == 4 && std::string(rows[0].label) == "Scroll V" && rows[0].next == "z" && rows[0].changed());
    assert(std::string(rows[3].label) == "Reversed directions" && rows[3].now == "SCROLL_V_SIGN");
    SettingsModel::Value v;
    assert(m.value(0, "SCROLL_V_MIX", &v) && v.text != "z");  // nothing was written by the preview
    const GlobalSnapshot before = snapshotGlobal(m);
    assert(before.valid() && w.applyTo(m, &err));
    assert(m.value(0, "SCROLL_V_MIX", &v) && v.text == "z" && m.value(0, "SCROLL_V_SIGN", &v) && v.text == "1");
    assert(restoreGlobal(m, before, &err));
    assert(m.value(0, "SCROLL_V_MIX", &v) && v.text != "z" && m.value(0, "SCROLL_V_SIGN", &v) && v.text == "-1");
    assert(!restoreGlobal(m, GlobalSnapshot(), &err));
    // a plain-word name for the axes the configuration names; none for an unknown pattern
    const AxisRoles roles = deriveAxisRoles(freshSettings());
    const auto words = axisPlainWords(roles);
    if (roles.matched) assert(words[size_t(roles.axis(Motion::Twist))] != nullptr);
    Settings odd;
    assert(axisPlainWords(deriveAxisRoles(odd))[0] == nullptr || deriveAxisRoles(odd).matched);
    // the lift fallback follows the input path of the settings: Windows driver order, but HID uses the Linux order
    Settings g;
    g.input = "auto";
    assert(wizardFallbackLiftAxis(g, "windows") == AX_Z && wizardFallbackLiftAxis(g, "linux") == AX_Y);
    g.input = "hid";
    assert(wizardFallbackLiftAxis(g, "windows") == AX_Y && wizardInputPath(g) == "hid");
    g.input = "driver";
    assert(wizardFallbackLiftAxis(g, "windows") == AX_Z);
    // "Don't advance automatically" is remembered in ui.conf and off by default
    UiSettings u;
    assert(!u.assistant.manual_advance && serializeUiSettings(u).find("manual_advance") == std::string::npos);
    u.assistant.manual_advance = true;
    const UiSettings back = parseUiSettings(serializeUiSettings(u));
    assert(back.assistant.manual_advance && back == u);
    assert(!parseUiSettings("axis_assistant_manual_advance = 7\n").assistant.manual_advance == false);  // clamped to 1
}

int main() {
    testApplyPreviewUndoAndWords();
    testUtf8();
    testScaling();
    testPeakHoldAndPause();
    testConnection();
    testDeviceSelector();
    testWizard();
    testWizardEdgeCases();
    testWizardByMovementOnly();
    testWizardStartAndEnd();
    testWizardNavigation();
    testWizardTuning();
    puts("test_ui_axes: ok");
    return 0;
}
