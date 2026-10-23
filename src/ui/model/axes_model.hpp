// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Toolkit-free logic of the "Axes / assistant" page: bar scaling, peak hold, pause/freeze, the
// connection state line with the per-OS "start the daemon" hint, the axis assistant controller
// (steps, 3 s measurement, evaluation through axis_wizard.hpp, apply into [global]) and AxesSession,
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

#include "axis_wizard.hpp"
#include "live_feed/live_feed.hpp"
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
        hold_.update(s.axes, nowNs);
        for (int i = 0; i < AX_COUNT; i++) scale_ = std::max(scale_, axisFullScale(std::abs(s.axes[i])));
        chanScale_ = std::max({chanScale_, channelFullScale(std::fabs(double(s.v))),
                               channelFullScale(std::fabs(double(s.h))),
                               channelFullScale(std::fabs(double(s.zoom)))});
        has_ = true;
    }
    bool hasData() const { return has_; }
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

// ---- axis assistant -----------------------------------------------------------------------------

struct WizardStepText {
    const char *instruction;
    const char *meaning;
};

inline const WizardStepText &wizardStepText(int step) {
    static const WizardStepText t[wiz::ROLE_COUNT] = {
        {N_("Pull the cap toward you, then push it forward - level, without tilting. "
            "Toward you first!"),
         N_("Toward you = positive (scroll down).")},
        {N_("Tilt the cap toward you, then forward (tilt the top, without pushing). "
            "Toward you first!"),
         N_("Tilting toward you = positive (scroll down). Optional - can be skipped.")},
        {N_("Push the cap to the right and back - level, without tilting. Right first!"),
         N_("Right = positive (scroll right).")},
        {N_("Tilt the cap sideways: tilt to the right and back. Right first!"),
         N_("Tilting right = positive (scroll right). Optional - can be skipped.")},
        {N_("Twist the cap clockwise (seen from above) and back. Clockwise first!"),
         N_("Clockwise = positive (zoom in).")},
    };
    static const WizardStepText bad = {"", ""};
    return step >= 0 && step < wiz::ROLE_COUNT ? t[step] : bad;
}

class WizardController {
public:
    static constexpr int64_t kMeasureNs = 3000000000;
    static constexpr size_t kMaxSamples = 4096;  // bound for a measurement (3 s at <= 60 Hz needs ~200)
    static constexpr int kSteps = wiz::ROLE_COUNT;

    int step() const { return step_; }  // 0..kSteps-1 measuring steps, kSteps = result page
    bool onResultPage() const { return step_ >= kSteps; }
    bool measuring() const { return measuring_; }
    bool done(int s) const { return inRange(s) && done_[size_t(s)]; }
    bool skipped(int s) const { return inRange(s) && skipped_[size_t(s)]; }
    const wiz::StepResult &stepResult(int s) const { return res_[size_t(inRange(s) ? s : 0)]; }
    const wiz::WizardResult &final() const { return final_; }
    const std::string &note() const { return note_; }
    size_t sampleCount() const { return samples_.size(); }

    // Fraction of the measurement time elapsed (0..1) and seconds left.
    double progress(int64_t nowNs) const {
        if (!measuring_) return done(step_) ? 1.0 : 0.0;
        return std::min(1.0, std::max(0.0, double(nowNs - t0_) / double(kMeasureNs)));
    }
    double secondsLeft(int64_t nowNs) const {
        return measuring_ ? std::max(0.0, double(kMeasureNs - (nowNs - t0_)) / 1e9) : 0.0;
    }

    bool canStart(bool connected) const { return connected && !onResultPage() && !measuring_; }
    bool canSkip() const { return !onResultPage() && !measuring_ && wiz::roleOptional(step_); }
    bool canBack() const { return step_ > 0 && !measuring_; }
    bool canNext() const {
        return !onResultPage() && !measuring_ && ((done(step_) && stepResult(step_).valid) || skipped(step_));
    }
    bool canApply() const { return onResultPage() && final_.ok; }

    // Lateral axes of the zoom detection: warns on the result page if the twist axis is among them.
    void setZoomLateral(const std::vector<Axis> &lat) { lateral_ = lat; }

    bool start(int64_t nowNs, bool connected) {
        if (!canStart(connected)) return false;
        samples_.clear();
        note_.clear();
        done_[size_t(step_)] = false;
        measuring_ = true;
        t0_ = nowNs;
        return true;
    }
    void onSample(const RawAxes &a) {
        if (measuring_ && samples_.size() < kMaxSamples) samples_.push_back(a);
    }
    // Finishes the measurement after kMeasureNs; true once when the result of the step is ready.
    bool tick(int64_t nowNs) {
        if (!measuring_ || nowNs - t0_ < kMeasureNs) return false;
        measuring_ = false;
        res_[size_t(step_)] = wiz::analyzeStep(samples_);
        done_[size_t(step_)] = true;
        skipped_[size_t(step_)] = false;
        return true;
    }
    void connectionLost() {
        if (!measuring_) return;
        measuring_ = false;
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
    void reset() { *this = WizardController(); }

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
    void goTo(int s) {
        note_.clear();
        step_ = std::max(0, std::min(s, kSteps));
        if (step_ == kSteps) evaluateNow();
    }
    void evaluateNow() {
        final_ = wiz::evaluate(res_, skipped_);
        if (!skipped_[wiz::ROLE_TWIST] && res_[wiz::ROLE_TWIST].valid)
            for (Axis l : lateral_)
                if (int(l) == res_[wiz::ROLE_TWIST].axis)
                    final_.warnings.push_back(wiz::wfmt(
                        N_("The twist axis %s is listed under \"Zoom must dominate against\" for "
                           "zoom - please deselect it there, otherwise zoom never engages."),
                        axisName(l)));
    }

    int step_ = 0;
    bool measuring_ = false;
    int64_t t0_ = 0;
    std::string note_;
    std::vector<RawAxes> samples_;
    std::array<wiz::StepResult, wiz::ROLE_COUNT> res_{};
    std::array<bool, wiz::ROLE_COUNT> done_{}, skipped_{};
    wiz::WizardResult final_;
    std::vector<Axis> lateral_;
};

// ---- session: one client, all models ------------------------------------------------------------

class AxesSession {
public:
    explicit AxesSession(LiveFeedClient &client) : client_(client) {}

    // Call from the page timer (~30 Hz): reconnects with backoff (client), feeds every sample to the
    // feed model, the view and the running measurement, and finishes a measurement when due.
    // Returns true when a wizard measurement finished in this call.
    bool poll(int64_t nowNs) {
        client_.poll(nowNs, [&](const LiveSample &s) {
            feed_.update(s, nowNs);
            view_.onSample(s, nowNs);
            wizard_.onSample(s.axes);
        });
        feed_.syncDevices(client_);
        view_.setDevice(feed_.deviceLabel(), feed_.factoryButtons());
        if (client_.state() != LiveFeedClient::State::Connected) wizard_.connectionLost();
        return wizard_.tick(nowNs);
    }
    ConnState connection(int64_t nowNs) const { return connectionState(client_.state(), feed_, nowNs); }
    bool connected(int64_t nowNs) const { return connection(nowNs) == ConnState::Connected; }

    LiveFeedModel &feed() { return feed_; }
    AxesViewModel &view() { return view_; }
    WizardController &wizard() { return wizard_; }

private:
    LiveFeedClient &client_;
    LiveFeedModel feed_;
    AxesViewModel view_;
    WizardController wizard_;
};

}  // namespace spn::ui
