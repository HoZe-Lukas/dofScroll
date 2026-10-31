// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Toolkit-free logic of the "Axes / assistant" page: bar scaling, peak hold, pause/freeze, the
// connection state line with the per-OS "start the daemon" hint, the axis assistant controller
// (steps started and ended by movement, evaluation through axis_wizard.hpp, apply into [global]) and AxesSession,
// which feeds all of them from one LiveFeedClient. The Qt page is a thin view on top of this.
//
// Only the live feed is used as data source (all three OSes); nothing here touches libspnav.
// English source strings are returned as const char* (marked with N_); the view translates them.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "axis_convention.hpp"
#include "axis_wizard.hpp"
#include "live_feed/live_feed.hpp"
#include "ui/model/assistant_tuning.hpp"
#include "ui/model/axis_roles_model.hpp"
#include "ui/model/live_feed_model.hpp"
#include "ui/model/settings_model.hpp"

namespace spn::ui {

// ---- UTF-8 text from outside (feed, config, errors) ------------------------------------------------

// Length of the valid UTF-8 sequence at s[i] (1..4), 0 if invalid (overlong, surrogate, > U+10FFFF).
inline size_t utf8SeqLen(const std::string &s, size_t i) {
    const auto b = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned c = b(i);
    size_t n = c < 0x80 ? 1 : (c >= 0xC2 && c <= 0xDF) ? 2 : (c >= 0xE0 && c <= 0xEF) ? 3
               : (c >= 0xF0 && c <= 0xF4) ? 4 : 0;
    if (n == 0 || i + n > s.size()) return 0;
    for (size_t k = 1; k < n; k++)
        if ((b(i + k) & 0xC0) != 0x80) return 0;
    if (n == 3 && ((c == 0xE0 && b(i + 1) < 0xA0) || (c == 0xED && b(i + 1) > 0x9F))) return 0;
    if (n == 4 && ((c == 0xF0 && b(i + 1) < 0x90) || (c == 0xF4 && b(i + 1) > 0x8F))) return 0;
    return n;
}

// Untrusted text to valid UTF-8: invalid bytes become U+FFFD, control characters (except none)
// become U+FFFD too; never throws.
inline std::string sanitizeUtf8(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const size_t n = utf8SeqLen(s, i);
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (n == 0 || (n == 1 && (c < 0x20 || c == 0x7F))) {
            out += "\xEF\xBF\xBD";
            i++;
        } else {
            out.append(s, i, n);
            i += n;
        }
    }
    return out;
}

// Cuts to at most maxBytes without splitting a multi-byte sequence (input is sanitized first).
inline std::string truncateUtf8(const std::string &text, size_t maxBytes) {
    const std::string s = sanitizeUtf8(text);
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) cut--;
    return s.substr(0, cut);
}

// ---- bar scaling -------------------------------------------------------------------------------

// Full-scale value of the bars: the smallest step that holds `peak` (devices stay near +-350).
inline int axisFullScale(int peak) {
    static const int steps[] = {350, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536, 100000};
    for (int s : steps)
        if (peak <= s) return s;
    return steps[sizeof steps / sizeof steps[0] - 1];
}

// Bar position in [-1, 1].
inline double barFraction(int value, int fullScale) {
    if (fullScale <= 0) return 0.0;
    return std::max(-1.0, std::min(1.0, double(value) / double(fullScale)));
}

// Full scale of the channel bars (steps per second after DSP): 10, 25, 50, 100, 250, ... (1-2.5-5).
inline double channelFullScale(double peak) {
    double base = 10.0;
    for (int i = 0; i < 12; i++, base *= 10.0) {
        if (peak <= base) return base;
        if (peak <= base * 2.5) return base * 2.5;
        if (peak <= base * 5.0) return base * 5.0;
    }
    return base;
}

// ---- peak hold ---------------------------------------------------------------------------------

// Peak marker per axis: jumps to a larger deflection at once, stays for kHoldNs, then falls back
// linearly (kFallPerSec units per second) towards the current value.
class PeakHold {
public:
    static constexpr int64_t kHoldNs = 1500000000;
    static constexpr double kFallPerSec = 300.0;

    void update(const RawAxes &axes, int64_t nowNs) {
        const double dt = started_ ? double(std::max<int64_t>(0, nowNs - lastNs_)) / 1e9 : 0.0;
        for (int i = 0; i < AX_COUNT; i++) {
            const int a = std::abs(axes[i]);
            if (!started_ || a >= std::abs(held_[i])) {
                held_[i] = axes[i];
                since_[i] = nowNs;
            } else if (nowNs - since_[i] > kHoldNs) {
                const double fallen = double(std::abs(held_[i])) - kFallPerSec * dt;
                if (fallen <= double(a)) held_[i] = axes[i];
                else held_[i] = (held_[i] < 0 ? -1 : 1) * int(std::lround(fallen));
            }
        }
        started_ = true;
        lastNs_ = nowNs;
    }
    int held(Axis a) const { return held_[a]; }  // signed value of the marker
    void reset() { *this = PeakHold(); }

private:
    RawAxes held_{};
    std::array<int64_t, AX_COUNT> since_{};
    int64_t lastNs_ = 0;
    bool started_ = false;
};

// ---- the live view (bars, buttons, channels, mode) -----------------------------------------------

class AxesViewModel {
public:
    static constexpr int kMinButtons = 2;

    // Called for every received sample. While paused the displayed values stay as they are.
    void onSample(const LiveSample &s, int64_t nowNs) {
        everButtons_ |= s.buttons;
        if (paused_) return;
        shown_ = s;
        lastSampleNs_ = nowNs;
        hold_.update(s.axes, nowNs);
        for (int i = 0; i < AX_COUNT; i++) scale_ = std::max(scale_, axisFullScale(std::abs(s.axes[i])));
        chanScale_ = std::max({chanScale_, channelFullScale(std::fabs(double(s.v))),
                               channelFullScale(std::fabs(double(s.h))),
                               channelFullScale(std::fabs(double(s.zoom)))});
        has_ = true;
    }
    bool hasData() const { return has_; }
    // The shown device has been quiet for `quietNs` (nothing was shown since): the bars go to zero (axes and the
    // three channel outputs; buttons stay as they were, a release arrives as a sample of its own, and the peak
    // marks stay). Returns true when something changed. Not while paused (the display is frozen then).
    bool settleIdle(int64_t nowNs, int64_t quietNs) {
        if (paused_ || !has_ || nowNs - lastSampleNs_ < quietNs) return false;
        bool changed = shown_.v != 0 || shown_.h != 0 || shown_.zoom != 0;
        for (int i = 0; i < AX_COUNT; i++) changed |= shown_.axes[i] != 0;
        shown_.axes = RawAxes{};
        shown_.v = shown_.h = shown_.zoom = 0;
        return changed;
    }
    const LiveSample &shown() const { return shown_; }
    int fullScale() const { return scale_; }
    double fraction(Axis a) const { return barFraction(shown_.axes[a], scale_); }
    double peakFraction(Axis a) const { return barFraction(hold_.held(a), scale_); }
    int peakValue(Axis a) const { return hold_.held(a); }

    // Channel outputs V, H, zoom (index 0..2) as a position in [-1, 1] and as the value.
    double channelValue(int i) const { return i == 0 ? shown_.v : (i == 1 ? shown_.h : shown_.zoom); }
    double channelFraction(int i) const {
        return std::max(-1.0, std::min(1.0, channelValue(i) / chanScale_));
    }

    bool paused() const { return paused_; }
    void setPaused(bool p) { paused_ = p; }
    void togglePause() { paused_ = !paused_; }
    void resetPeaks() {
        hold_.reset();
        chanScale_ = 10.0;
        scale_ = axisFullScale(0);
        for (int i = 0; i < AX_COUNT; i++) scale_ = std::max(scale_, axisFullScale(std::abs(shown_.axes[i])));
    }

    // Buttons shown as 1..N: the known device count if set (factoryButtonCount), but at least the
    // highest button seen so far (for devices without a known count), at least kMinButtons.
    // Device from the feed's info frame (name "" and 0 buttons when unknown).
    void setDevice(const std::string &label, int factoryButtons) {
        label_ = label;
        setKnownButtonCount(factoryButtons);
    }
    const std::string &deviceLabel() const { return label_; }
    void setKnownButtonCount(int n) { known_ = std::max(0, std::min(n, int(kMaxButtons))); }
    int buttonCount() const {
        int highest = 0;
        for (int b = 0; b < 32; b++)
            if ((everButtons_ >> b) & 1u) highest = b + 1;
        return std::max({known_, highest, kMinButtons});
    }
    bool buttonDown(int number1) const {  // 1-based
        return number1 >= 1 && number1 <= 32 && ((shown_.buttons >> (number1 - 1)) & 1u);
    }

private:
    LiveSample shown_;
    PeakHold hold_;
    uint32_t everButtons_ = 0;
    std::string label_;
    int scale_ = axisFullScale(0), known_ = 0;
    double chanScale_ = 10.0;
    int64_t lastSampleNs_ = 0;
    bool paused_ = false, has_ = false;
};

// ---- connection line ---------------------------------------------------------------------------

enum class ConnState { DaemonNotRunning, WaitingForFeed, Connected, Incompatible };
enum class HostOs { Linux, Windows, MacOs };

inline HostOs currentHostOs() {
#if defined(_WIN32)
    return HostOs::Windows;
#elif defined(__APPLE__)
    return HostOs::MacOs;
#else
    return HostOs::Linux;
#endif
}

// Connected = transport up and a recent sample; WaitingForFeed = transport up but no (fresh)
// sample (the daemon only publishes while a device is present and LIVE_FEED is not 0).
inline ConnState connectionState(LiveFeedClient::State client, const LiveFeedModel &feed, int64_t nowNs) {
    if (client == LiveFeedClient::State::Incompatible) return ConnState::Incompatible;
    if (client != LiveFeedClient::State::Connected) return ConnState::DaemonNotRunning;
    return feed.stale(nowNs) ? ConnState::WaitingForFeed : ConnState::Connected;
}

inline const char *connectionText(ConnState s) {
    switch (s) {
    case ConnState::DaemonNotRunning: return N_("Daemon not running");
    case ConnState::WaitingForFeed: return N_("Waiting for the live feed");
    case ConnState::Connected: return N_("Connected");
    case ConnState::Incompatible: return N_("The daemon speaks an incompatible live feed version");
    }
    return "";
}

// Hint shown under the line when there is no data (empty when connected).
inline const char *connectionHint(ConnState s, HostOs os) {
    switch (s) {
    case ConnState::Connected: return "";
    case ConnState::Incompatible:
        return N_("Update dofscroll and the settings window to the same version.");
    case ConnState::WaitingForFeed:
        return N_("The daemon is running but sends no data: check that the SpaceMouse is connected "
                  "and that LIVE_FEED is not set to 0 in [global].");
    case ConnState::DaemonNotRunning:
        switch (os) {
        case HostOs::Linux:
            return N_("Start the daemon, for example with: systemctl --user start dofscroll.service");
        case HostOs::Windows: return N_("Start the daemon: run dofscroll.exe (it has no window).");
        case HostOs::MacOs:
            return N_("Start the daemon: run dofscroll, or load its LaunchAgent "
                      "(see the macOS installation notes).");
        }
    }
    return "";
}

// A plain-word second name for each raw axis (x, y, z, rx, ry, rz) where the configuration says what the axis is
// (axis_roles_model.hpp; the words follow the driver of this system, never a fixed table). nullptr = no word known,
// the view then shows the axis name alone. English source text (N_), the view translates it.
inline std::array<const char *, AX_COUNT> axisPlainWords(const AxisRoles &r) {
    std::array<const char *, AX_COUNT> out{};
    if (!r.matched) return out;
    struct Row {
        Motion m;
        const char *words;
    };
    const Row rows[] = {{Motion::VPush, N_("Push forward / back")}, {Motion::VTilt, N_("Tilt forward / back")},
                        {Motion::HPush, N_("Push sideways")},       {Motion::HTilt, N_("Tilt sideways")},
                        {Motion::Twist, N_("Twist the cap")},       {Motion::Lift, N_("Push down / pull up")}};
    for (const Row &row : rows)
        if (r.has(row.m)) out[size_t(r.axis(row.m))] = row.words;
    return out;
}

// ---- axis assistant -----------------------------------------------------------------------------
//
// Flow of one step (all by movement, no click needed):
//   Waiting    the cap must first be at rest (kArmRestNs), then a movement above the start
//              threshold (MIN_RUN samples in a row) starts the measurement. An optional step is
//              skipped by itself after `auto_skip_s` without movement; a required step only shows
//              a hint after `max_s`.
//   Measuring  ends when the cap moved out and back (two half-movements) and has been at rest for
//              `rest_ms`; or after a single half-movement and a long rest; or after `max_s`.
//   Detected   a valid result is shown for kAdvanceNs, then the next step starts by itself (not with
//              tuning.manual_advance: it stays until Next, and no optional step is skipped by itself).
//   Review     a step that is revisited with Back: result shown, nothing starts by itself.
// An invalid result goes back to Waiting on the same step (the message stays visible), so the
// next movement measures it again.

struct WizardStepText {
    const char *instruction;
    const char *meaning;
};

// One short line per step in direction words (never axis names) and what "positive" means.
inline const WizardStepText &wizardStepText(int step) {
    static const WizardStepText t[wiz::ROLE_COUNT] = {
        {N_("Pull the cap toward you, then push it away from you."),
         N_("Toward you counts as positive (scroll down).")},
        {N_("Tilt the cap toward you, then away from you."),
         N_("Tilting toward you counts as positive (scroll down). Optional.")},
        {N_("Push the cap to the right, then back to the left."),
         N_("Right counts as positive (scroll right).")},
        {N_("Tilt the cap to the right, then back to the left."),
         N_("Tilting right counts as positive (scroll right). Optional.")},
        {N_("Twist the cap clockwise, then back."),
         N_("Clockwise counts as positive (zoom in).")},
    };
    static const WizardStepText bad = {"", ""};
    return step >= 0 && step < wiz::ROLE_COUNT ? t[step] : bad;
}

// The first half-movement of a step: an arrow and the words for it.
struct WizardFirstMove {
    const char *arrow;
    const char *words;
};
inline const WizardFirstMove &wizardFirstMove(int step) {
    static const WizardFirstMove t[wiz::ROLE_COUNT] = {
        {"\xE2\x86\x93", N_("toward you")},         {"\xE2\x86\x93", N_("toward you")},
        {"\xE2\x86\x92", N_("to the right")},       {"\xE2\x86\x92", N_("to the right")},
        {"\xE2\x86\xBB", N_("clockwise")},
    };
    static const WizardFirstMove bad = {"", ""};
    return step >= 0 && step < wiz::ROLE_COUNT ? t[step] : bad;
}

// What to change after a result that is not valid, in words (no axis names).
inline const char *wizardStepHint(const wiz::StepResult &r) {
    if (r.valid) return "";
    if (r.axis < 0) return N_("No sustained movement seen. Move the cap steadily and a little longer.");
    if (r.too_weak) return N_("Too weak. Move the cap more firmly in the direction shown.");
    if (r.ambiguous)
        return N_("Not clear. Move the cap in only one direction, without tilting or pushing sideways.");
    return N_("The direction could not be told. Start with the movement shown, then go back.");
}

enum class WizardPhase { Waiting, Measuring, Detected, Review, ResultPage };

class WizardController {
public:
    static constexpr int64_t kArmRestNs = 200000000;     // rest needed before a movement starts a step
    static constexpr int64_t kMinMeasureNs = 500000000;  // a spike cannot end a step sooner
    static constexpr int64_t kAdvanceNs = 1000000000;    // a valid result is shown this long
    static constexpr int64_t kOneHalfRestNs = 2000000000;  // rest that ends a step with one half-movement
    static constexpr size_t kMaxSamples = 8192;          // bound for a measurement
    static constexpr int kSteps = wiz::ROLE_COUNT;

    int step() const { return step_; }  // 0..kSteps-1 measuring steps, kSteps = result page
    bool onResultPage() const { return step_ >= kSteps; }
    WizardPhase phase() const { return phase_; }
    bool measuring() const { return phase_ == WizardPhase::Measuring; }
    bool waiting() const { return phase_ == WizardPhase::Waiting; }
    bool done(int s) const { return inRange(s) && done_[size_t(s)]; }
    bool skipped(int s) const { return inRange(s) && skipped_[size_t(s)]; }
    const wiz::StepResult &stepResult(int s) const { return res_[size_t(inRange(s) ? s : 0)]; }
    const wiz::WizardResult &final() const { return final_; }
    // Something went wrong or needs a word (connection lost, nothing moved for a long time).
    const std::string &note() const { return note_; }
    // Remark on the step before ("Skipped, no movement"); gone when the step is measured.
    const std::string &notice() const { return notice_; }
    size_t sampleCount() const { return samples_.size(); }
    // Half-movements seen so far in the running measurement (0, 1, 2).
    int halves() const { return halves_; }

    void setTuning(const AxisAssistantTuning &t) { tuning_ = t; }
    const AxisAssistantTuning &tuning() const { return tuning_; }

    // Fraction of the longest step elapsed (0..1) and seconds left until it ends.
    double progress(int64_t nowNs) const {
        if (!measuring()) return done(step_) ? 1.0 : 0.0;
        return std::min(1.0, std::max(0.0, double(nowNs - t0_) / double(tuning_.maxNs())));
    }
    double secondsLeft(int64_t nowNs) const {
        return measuring() ? std::max(0.0, double(tuning_.maxNs() - (nowNs - t0_)) / 1e9) : 0.0;
    }

    bool canStart(bool connected) const { return connected && !onResultPage() && !measuring(); }
    bool canAgain() const { return !onResultPage(); }
    bool canSkip() const { return !onResultPage() && !measuring() && wiz::roleOptional(step_); }
    bool canBack() const { return step_ > 0 && !measuring(); }
    bool canNext() const {
        return !onResultPage() && !measuring() && ((done(step_) && stepResult(step_).valid) || skipped(step_));
    }
    bool canApply() const { return onResultPage() && final_.ok; }

    // Lateral axes of the zoom detection: warns on the result page if the twist axis is among them.
    void setZoomLateral(const std::vector<Axis> &lat) { lateral_ = lat; }
    // Lift/press axis of the active convention (axis_convention.hpp, #186); the result page warns about it.
    void setLiftAxis(Axis a) { liftAxis_ = a; }
    void setConnected(bool c) { connected_ = c; }

    // Starts measuring at once (the "Start now" button; the normal start is by movement).
    bool start(int64_t nowNs, bool connected) {
        if (!canStart(connected)) return false;
        lastNow_ = nowNs;
        samples_.clear();
        note_.clear();
        notice_.clear();
        done_[size_t(step_)] = false;
        skipped_[size_t(step_)] = false;
        beginMeasuring(nowNs);
        return true;
    }
    // Throws the result of this step away and waits for a new movement.
    bool again(int64_t nowNs) {
        if (!canAgain()) return false;
        lastNow_ = nowNs;
        done_[size_t(step_)] = false;
        skipped_[size_t(step_)] = false;
        note_.clear();
        notice_.clear();
        enterWaiting();
        return true;
    }

    void onSample(const RawAxes &a, int64_t nowNs) {
        lastNow_ = nowNs;
        const bool active = maxAbs(a) > tuning_.start_threshold;
        if (phase_ == WizardPhase::Waiting) {
            if (active) {
                waitSince_ = nowNs;  // still moving (even if not armed yet): the automatic skip counts from the last movement
                if (above_ > 0 || armed(nowNs)) {
                    recent_.push_back(a);
                    if (++above_ >= wiz::MIN_RUN) {
                        samples_ = recent_;
                        note_.clear();
                        notice_.clear();
                        beginMeasuring(nowNs);
                    }
                } else {
                    quietSince_ = -1;  // still moving from before: wait for rest first
                }
            } else {
                above_ = 0;
                recent_.clear();
                if (quietSince_ < 0) quietSince_ = nowNs;
            }
        } else if (phase_ == WizardPhase::Measuring) {
            if (samples_.size() < kMaxSamples) samples_.push_back(a);
            if (active) lastActive_ = nowNs;
        }
    }

    // Advances the flow with the clock; true when something changed that the view must show.
    bool tick(int64_t nowNs) {
        lastNow_ = nowNs;
        switch (phase_) {
        case WizardPhase::Waiting: {
            if (!connected_) {
                waitSince_ = nowNs;  // the timers run only while data can arrive
                return false;
            }
            const int64_t idle = nowNs - waitSince_;
            if (!tuning_.manual_advance && wiz::roleOptional(step_) && !done_[size_t(step_)] && idle >= tuning_.autoSkipNs()) {
                const std::string title = wiz::tr(wiz::roleTitle(step_));
                skipped_[size_t(step_)] = true;
                goTo(step_ + 1);
                notice_ = wiz::wfmt(N_("\"%s\" skipped, no movement. You can do it later from the result page."),
                                    title.c_str());
                return true;
            }
            if (idle >= tuning_.maxNs() && !waitNoted_) {
                waitNoted_ = true;
                note_ = wiz::wfmt(N_("No movement seen. Move the cap as shown, or press \"Measure again\"."));
                return true;
            }
            return false;
        }
        case WizardPhase::Measuring: {
            const int64_t el = nowNs - t0_;
            if (el >= tuning_.maxNs()) {
                finishMeasuring(nowNs, true);
                return true;
            }
            if (el < kMinMeasureNs) return false;
            updateHalves();
            const int64_t rest = nowNs - lastActive_;
            if ((halves_ >= 2 && rest >= tuning_.restNs()) ||
                (halves_ >= 1 && rest >= std::max(kOneHalfRestNs, 4 * tuning_.restNs()))) {
                finishMeasuring(nowNs, false);
                return true;
            }
            return false;
        }
        case WizardPhase::Detected:
            if (!tuning_.manual_advance && nowNs - detectedAt_ >= kAdvanceNs) {
                goTo(step_ + 1);
                return true;
            }
            return false;
        default: return false;
        }
    }
    void connectionLost() {
        connected_ = false;
        if (!measuring()) return;
        samples_.clear();
        enterWaiting();
        note_ = wiz::wfmt(N_("Connection to the daemon lost - measurement aborted."));
    }
    bool skip() {
        if (!canSkip()) return false;
        skipped_[size_t(step_)] = true;
        done_[size_t(step_)] = false;
        goTo(step_ + 1);
        return true;
    }
    bool back() {
        if (!canBack()) return false;
        goTo(step_ - 1);
        return true;
    }
    bool next() {
        if (!canNext()) return false;
        goTo(step_ + 1);
        return true;
    }
    void reset() {
        const AxisAssistantTuning t = tuning_;
        *this = WizardController();
        tuning_ = t;
    }

    // Writes the detected mixes into [global] (section 0) and resets the direction switches. All or
    // nothing: on any failure the model is unchanged. Does not save; the caller saves and reloads.
    bool applyTo(SettingsModel &m, std::string *err) const {
        if (!final_.ok) {
            if (err) *err = "the mapping is incomplete";
            return false;
        }
        SettingsModel tmp = m;
        const std::pair<const char *, const std::string *> kv[] = {
            {"SCROLL_V_MIX", &final_.mix_v}, {"SCROLL_H_MIX", &final_.mix_h},
            {"ZOOM_MIX", &final_.mix_zoom}};
        for (const auto &p : kv)
            if (!tmp.setValue(0, p.first, *p.second, err)) return false;
        for (const char *k : {"SCROLL_V_SIGN", "SCROLL_H_SIGN", "ZOOM_SIGN"})
            if (!tmp.setValue(0, k, "1", err)) return false;
        // The measurement saw the raw axes (before INVERT_AXES), so an inverted axis would flip the detected direction again.
        if (!tmp.setValue(0, "INVERT_AXES", "none", err)) return false;
        // Zoom must dominate against the axes the two scroll channels use, never against its own
        // twist axis (the seeded x z rx rz would block zoom on a device whose twist is rz).
        std::string lat;
        {
            Mix mv, mh, mz;
            std::string perr;
            if (parseMix(final_.mix_v, &mv, &perr) && parseMix(final_.mix_h, &mh, &perr) &&
                parseMix(final_.mix_zoom, &mz, &perr)) {
                for (int a = 0; a < 6; ++a) {
                    auto uses = [a](const Mix &x) {
                        for (const Term &tm : x.terms)
                            if (int(tm.axis) == a) return true;
                        return false;
                    };
                    if ((uses(mv) || uses(mh)) && !uses(mz)) lat += (lat.empty() ? "" : " ") + std::string(axisName(Axis(a)));
                }
            }
        }
        if (!lat.empty() && !tmp.setValue(0, "ZOOM_LATERAL", lat, err)) return false;
        m = tmp;
        return true;
    }

private:
    static bool inRange(int s) { return s >= 0 && s < kSteps; }
    static int maxAbs(const RawAxes &a) {
        int m = 0;
        for (int v : a) m = std::max(m, std::abs(v));
        return m;
    }
    bool armed(int64_t nowNs) const { return quietSince_ >= 0 && nowNs - quietSince_ >= kArmRestNs; }
    void enterWaiting() {
        phase_ = WizardPhase::Waiting;
        waitSince_ = lastNow_;
        quietSince_ = -1;
        above_ = 0;
        waitNoted_ = false;
        halves_ = 0;
        recent_.clear();
    }
    void beginMeasuring(int64_t nowNs) {
        phase_ = WizardPhase::Measuring;
        t0_ = lastActive_ = nowNs;
        halves_ = 0;
        recent_.clear();
        above_ = 0;
        done_[size_t(step_)] = false;
    }
    void updateHalves() {
        const int axis = wiz::strongestAxis(wiz::smoothedPeaks(samples_));
        halves_ = wiz::countHalfMovements(samples_, axis, tuning_.start_threshold);
    }
    void finishMeasuring(int64_t nowNs, bool timedOut) {
        res_[size_t(step_)] = wiz::analyzeStep(samples_);
        done_[size_t(step_)] = true;
        skipped_[size_t(step_)] = false;
        if (res_[size_t(step_)].valid) {
            phase_ = WizardPhase::Detected;
            detectedAt_ = nowNs;
            return;
        }
        enterWaiting();  // stays on the step; the message of the result says what to change
        if (timedOut) note_ = wiz::wfmt(N_("The step took too long. Try again with one clear movement out and back."));
    }
    void goTo(int s) {
        note_.clear();
        notice_.clear();
        step_ = std::max(0, std::min(s, kSteps));
        if (step_ == kSteps) {
            phase_ = WizardPhase::ResultPage;
            evaluateNow();
        } else if ((done_[size_t(step_)] && res_[size_t(step_)].valid) || skipped_[size_t(step_)]) {
            phase_ = WizardPhase::Review;
        } else {
            enterWaiting();
        }
    }
    void evaluateNow() {
        final_ = wiz::evaluate(res_, skipped_, liftAxis_);
        if (!skipped_[wiz::ROLE_TWIST] && res_[wiz::ROLE_TWIST].valid)
            for (Axis l : lateral_)
                if (int(l) == res_[wiz::ROLE_TWIST].axis)
                    final_.warnings.push_back(wiz::wfmt(
                        N_("The twist axis %s is listed under \"Zoom must dominate against\" for "
                           "zoom - please deselect it there, otherwise zoom never engages."),
                        axisName(l)));
    }

    int step_ = 0;
    WizardPhase phase_ = WizardPhase::Waiting;
    AxisAssistantTuning tuning_;
    bool connected_ = true;
    int64_t lastNow_ = 0, t0_ = 0, waitSince_ = 0, quietSince_ = -1, lastActive_ = 0, detectedAt_ = 0;
    int above_ = 0, halves_ = 0;
    bool waitNoted_ = false;
    std::string note_, notice_;
    std::vector<RawAxes> samples_, recent_;
    std::array<wiz::StepResult, wiz::ROLE_COUNT> res_{};
    std::array<bool, wiz::ROLE_COUNT> done_{}, skipped_{};
    wiz::WizardResult final_;
    std::vector<Axis> lateral_;
    Axis liftAxis_ = AX_Y;
};

// The result page as a table by direction: what was detected for each motion, in the order of
// the steps. The axis name appears only here, next to the direction, because that is what is written.
struct WizardMappingRow {
    std::string direction;  // "Push forward/back" (translated)
    std::string detail;     // "y, positive" / "Skipped" / "Not detected" (translated)
};
inline std::vector<WizardMappingRow> wizardMappingRows(const WizardController &w) {
    std::vector<WizardMappingRow> rows;
    for (int r = 0; r < WizardController::kSteps; ++r) {
        WizardMappingRow row;
        row.direction = wiz::tr(wiz::roleTitle(r));
        const wiz::StepResult &s = w.stepResult(r);
        if (w.skipped(r)) row.detail = wiz::tr(N_("Skipped"));
        else if (w.done(r) && s.valid)
            row.detail = wiz::wfmt(s.sign < 0 ? N_("%s, negative") : N_("%s, positive"), axisName(Axis(s.axis)));
        else row.detail = wiz::tr(N_("Not detected"));
        rows.push_back(row);
    }
    return rows;
}

// The state of the step as a short text (state is never only colour or a bar).
inline std::string wizardStateText(const WizardController &w, bool connected) {
    if (w.onResultPage()) return std::string();
    if (!connected) return wiz::tr(N_("No connection to the daemon"));
    switch (w.phase()) {
    case WizardPhase::Waiting: return wiz::tr(N_("Waiting for you to move the cap..."));
    case WizardPhase::Measuring:
        return wiz::tr(w.halves() >= 2   ? N_("Measuring: out and back seen, hold still...")
                       : w.halves() == 1 ? N_("Measuring: first movement seen, now move back...")
                                         : N_("Measuring: keep moving..."));
    case WizardPhase::Detected: return wiz::tr(N_("Detected"));
    case WizardPhase::Review:
        return wiz::tr(w.skipped(w.step()) ? N_("Skipped") : N_("Detected"));
    default: return std::string();
    }
}

// The input path the convention is chosen by: the INPUT setting when it names one ("driver", "hid"), else "" (not
// known: the platform's usual path applies). The daemon's reported path would be better but the page has none.
inline std::string wizardInputPath(const Settings &g) { return g.input == "driver" || g.input == "hid" ? g.input : std::string(); }
// Lift/press axis when the configuration does not tell: the table of the platform and input path.
inline Axis wizardFallbackLiftAxis(const Settings &g, const std::string &osName) {
    return conventionLiftAxis(conventionFor(osName, wizardInputPath(g)));
}

// ---- what Apply changes, and Undo -----------------------------------------------------------------
//
// The keys of the Global profile that WizardController::applyTo writes. The result page lists them as "now ->
// new" before anything is written, and a snapshot taken just before Apply lets one click bring the old values back.
inline const std::vector<std::string> &wizardGlobalKeys() {
    static const std::vector<std::string> k = {"SCROLL_V_MIX",  "SCROLL_H_MIX",  "ZOOM_MIX",    "SCROLL_V_SIGN",
                                               "SCROLL_H_SIGN", "ZOOM_SIGN",     "ZOOM_LATERAL", "INVERT_AXES"};
    return k;
}

struct WizardChangeRow {
    const char *label;  // English source text, the view translates it
    std::string now, next;
    bool changed() const { return now != next; }
};
// The three axis mixes of a finished mapping against the values of the Global profile now (empty when the
// mapping is incomplete). The direction switches are reset to normal by Apply; that is a fourth row when any of
// them is not normal now.
inline std::vector<WizardChangeRow> wizardChangeRows(const SettingsModel &m, const WizardController &w) {
    std::vector<WizardChangeRow> rows;
    if (!w.final().ok) return rows;
    auto now = [&m](const char *key) {
        SettingsModel::Value v;
        return m.value(0, key, &v) ? v.text : std::string();
    };
    const wiz::WizardResult &f = w.final();
    rows.push_back({N_("Scroll V"), now("SCROLL_V_MIX"), f.mix_v});
    rows.push_back({N_("Scroll H"), now("SCROLL_H_MIX"), f.mix_h});
    rows.push_back({N_("Zoom"), now("ZOOM_MIX"), f.mix_zoom});
    std::string reversed;
    for (const char *k : {"SCROLL_V_SIGN", "SCROLL_H_SIGN", "ZOOM_SIGN"}) {
        const std::string v = now(k);
        if (!v.empty() && v != "1") reversed += (reversed.empty() ? "" : ", ") + std::string(k);
    }
    if (!reversed.empty()) rows.push_back({N_("Reversed directions"), reversed, "none"});
    const std::string inverted = now("INVERT_AXES");
    if (!inverted.empty() && inverted != "none") rows.push_back({N_("Inverted axes"), inverted, "none"});
    return rows;
}

// The device and program profiles that set their own direction values (INVERT_AXES or a *_SIGN key): Apply writes the
// Global profile only, so these keep what they say. Their names, in list order.
inline std::vector<std::string> wizardProfilesKeepingDirections(const SettingsModel &m) {
    std::vector<std::string> out;
    for (size_t i = 1; i < m.sectionCount(); ++i)
        for (const char *k : {"INVERT_AXES", "SCROLL_V_SIGN", "SCROLL_H_SIGN", "ZOOM_SIGN"})
            if (m.isOverridden(i, k)) {
                out.push_back(m.sections()[i].pattern);
                break;
            }
    return out;
}

struct GlobalSnapshot {
    std::vector<std::pair<std::string, std::string>> values;  // key, value of the Global profile
    bool valid() const { return !values.empty(); }
};
inline GlobalSnapshot snapshotGlobal(const SettingsModel &m) {
    GlobalSnapshot snap;
    for (const std::string &k : wizardGlobalKeys()) {
        SettingsModel::Value v;
        if (m.value(0, k, &v)) snap.values.emplace_back(k, v.text);
    }
    return snap;
}
// The offer of Undo after Apply: the values before and the values Apply wrote. It is on offer as long as the Global
// profile still has the values Apply wrote; a change by hand after Apply makes it obsolete (it would overwrite that
// change). Time plays no part.
struct AppliedUndo {
    GlobalSnapshot before, after;
    bool valid() const { return before.valid() && after.valid(); }
    bool stillApplies(const SettingsModel &m) const { return valid() && snapshotGlobal(m).values == after.values; }
};
// Puts the values back; all or nothing (on a failure the model is unchanged). Does not save.
inline bool restoreGlobal(SettingsModel &m, const GlobalSnapshot &snap, std::string *err) {
    if (!snap.valid()) {
        if (err) *err = "nothing to restore";
        return false;
    }
    SettingsModel tmp = m;
    for (const auto &kv : snap.values)
        if (!tmp.setValue(0, kv.first, kv.second, err)) return false;
    m = tmp;
    return true;
}

// ---- session: one client, all models ------------------------------------------------------------

class AxesSession {
public:
    explicit AxesSession(LiveFeedClient &client) : client_(client) {}

    // Call from the page timer (~30 Hz): reconnects with backoff (client), feeds every sample to the
    // feed model, the view and the running measurement, and finishes a measurement when due.
    // Returns true when a wizard measurement finished in this call.
    bool poll(int64_t nowNs) {
        client_.poll(nowNs, [&](const LiveSample &s) {
            feed_.update(s, nowNs);  // connection health: every sample counts
            lastSource_ = s.source;
            if (!shows(s)) return;
            view_.onSample(s, nowNs);
            wizard_.onSample(s.axes, nowNs);
        });
        feed_.syncDevices(client_);
        // The daemon sends only the device that moved last: a pinned device that rests would keep its last values.
        if (selected_ != kAllDevices) view_.settleIdle(nowNs, kPinnedIdleNs);
        if (selected_ != kAllDevices && feed_.hasDevices() && !findDevice(selected_)) selected_ = kAllDevices;  // unplugged
        const LiveDevice *sel = findDevice(selected_);
        const LiveDevice *src = (sel || feed_.devices().size() < 2) ? nullptr : findDevice(lastSource_);
        if (sel) view_.setDevice(sel->name, sel->factoryButtons);
        else if (src) view_.setDevice(src->name, feed_.factoryButtons());
        else view_.setDevice(feed_.deviceLabel(), feed_.factoryButtons());
        const bool up = client_.state() == LiveFeedClient::State::Connected;
        wizard_.setConnected(up);
        if (!up) wizard_.connectionLost();
        return wizard_.tick(nowNs);
    }
    ConnState connection(int64_t nowNs) const { return connectionState(client_.state(), feed_, nowNs); }
    bool connected(int64_t nowNs) const { return connection(nowNs) == ConnState::Connected; }

    LiveFeedModel &feed() { return feed_; }
    AxesViewModel &view() { return view_; }
    WizardController &wizard() { return wizard_; }

    // Device selector (issue #139). kAllDevices = every sample (the daemon sends the device that
    // moved last); otherwise only samples whose source is that device id reach the view and the
    // assistant (a sample of unknown source always passes). Falls back to kAllDevices when the
    // device disappears from the device list.
    static constexpr int kAllDevices = -1;
    static constexpr int64_t kPinnedIdleNs = 1000000000;  // quiet time after which a pinned device shows as idle
    int selectedDevice() const { return selected_; }
    // Returns false (and keeps the selection) for an id that is not in the device list. The assistant
    // is not interrupted by the page: the caller disables the selector while a step measures.
    bool selectDevice(int id) {
        if (id != kAllDevices && !findDevice(id)) return false;
        selected_ = id;
        return true;
    }
    // Selector choices: the devices of the feed, only when there is more than one to choose from.
    const std::vector<LiveDevice> &selectableDevices() const {
        static const std::vector<LiveDevice> none;
        return feed_.devices().size() > 1 ? feed_.devices() : none;
    }

private:
    bool shows(const LiveSample &s) const {
        return selected_ == kAllDevices || s.source == kLiveNoSource || s.source == selected_;
    }
    const LiveDevice *findDevice(int id) const {
        if (id < 0) return nullptr;
        for (const LiveDevice &d : feed_.devices())
            if (d.id == id) return &d;
        return nullptr;
    }
    int selected_ = kAllDevices;
    int lastSource_ = kLiveNoSource;
    LiveFeedClient &client_;
    LiveFeedModel feed_;
    AxesViewModel view_;
    WizardController wizard_;
};

}  // namespace spn::ui
