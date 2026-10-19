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
    void push(const RawAxes &a, uint32_t buttons = 0) {
        LiveSample s;
        s.axes = a;
        s.buttons = buttons;
        s.mode = "normal";
        for (uint8_t b : encodeLiveSample(s)) conn->in.push_back(b);
    }
};

RawAxes ax(int x, int y, int z, int rx, int ry, int rz) { return RawAxes{x, y, z, rx, ry, rz}; }

// Moves `axis` first in direction `first` (+1/-1), then back, for ~3 s at 30 Hz and waits for the
// measurement to finish.
void measure(FakeFeed &f, AxesSession &s, int64_t &t, int axis, int first, int v = 200) {
    const bool started = s.wizard().start(t, s.connected(t));
    assert(started);
    (void)started;
    for (int i = 0; i < 100; i++) {
        RawAxes a{};
        const int phase = i < 15 ? 0 : (i < 30 ? 1 : 2);
        a[size_t(axis)] = phase == 0 ? first * v : (phase == 1 ? -first * v : 0);
        f.push(a);
        t += 33 * MS;
        s.poll(t);
    }
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
    measure(f, s, t, AX_Z, +1, 30);
    assert(w.stepResult(0).too_weak && !w.stepResult(0).valid && !w.canNext());
    assert(!w.stepResult(0).message.empty());

    // Connection lost while measuring aborts with a note.
    assert(w.start(t, s.connected(t)));
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

int main() {
    testUtf8();
    testScaling();
    testPeakHoldAndPause();
    testConnection();
    testWizard();
    testWizardEdgeCases();
    puts("test_ui_axes: ok");
    return 0;
}
