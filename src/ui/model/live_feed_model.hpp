// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Toolkit-free UI model for the live feed: the latest sample plus the per-axis min/max/peak
// tracking that the axes view and the axis assistant use. The UI owns the LiveFeedClient, calls
// poll() from its timer (~30 Hz) and feeds the samples here (see pump()).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "live_feed/live_feed.hpp"

namespace spn {

class LiveFeedModel {
public:
    // How long without a sample until the data counts as stale (daemon paused or feed off).
    static constexpr int64_t kStaleNs = 1000000000;

    void update(const LiveSample &s, int64_t nowNs) {
        if (!seeded_) {
            seeded_ = true;
            min_ = s.axes;
            max_ = s.axes;
        }
        for (int i = 0; i < AX_COUNT; i++) {
            min_[i] = std::min(min_[i], s.axes[i]);
            max_[i] = std::max(max_[i], s.axes[i]);
            peak_[i] = std::max(peak_[i], std::abs(s.axes[i]));
        }
        everPressed_ |= s.buttons;
        latest_ = s;
        lastNs_ = nowNs;
        has_ = true;
        samples_++;
    }

    // Polls the client and feeds every received sample; returns how many arrived.
    int pump(LiveFeedClient &client, int64_t nowNs) {
        const int n = client.poll(nowNs, [&](const LiveSample &s) { update(s, nowNs); });
        if (client.infoCount() != infoSeen_) {  // new device list (or reconnect with a fresh one)
            infoSeen_ = client.infoCount();
            devices_ = client.devices();
            hasDevices_ = true;
        } else if (client.state() != LiveFeedClient::State::Connected) {
            devices_.clear();  // daemon gone: its device list is no longer known
            hasDevices_ = false;
        }
        return n;
    }

    // Device info frame of the daemon. hasDevices() is false until the first frame arrives and
    // while the feed is unavailable (consumers then fall back to the status file).
    bool hasDevices() const { return hasDevices_; }
    const std::vector<LiveDevice> &devices() const { return devices_; }
    uint32_t infoRevision() const { return infoSeen_; }
    // Name of the first device ("" if none), and the number of configurable buttons: the factory
    // count of the first device (0 if no device is known).
    std::string deviceName() const { return devices_.empty() ? std::string() : devices_.front().name; }
    int factoryButtons() const { return devices_.empty() ? 0 : devices_.front().factoryButtons; }

    bool hasSample() const { return has_; }
    const LiveSample &latest() const { return latest_; }
    bool stale(int64_t nowNs) const { return !has_ || nowNs - lastNs_ > kStaleNs; }
    uint64_t sampleCount() const { return samples_; }

    // Tracking since construction or the last resetTracking() (the assistant resets per step).
    int minOf(Axis a) const { return min_[a]; }
    int maxOf(Axis a) const { return max_[a]; }
    int peakOf(Axis a) const { return peak_[a]; }  // largest absolute deflection
    uint32_t buttonsEverPressed() const { return everPressed_; }
    bool pressed(int button) const { return button >= 0 && button < 32 && ((latest_.buttons >> button) & 1u); }

    void resetTracking() {
        peak_.fill(0);
        everPressed_ = 0;
        seeded_ = false;  // the next sample re-seeds min/max
    }

private:
    LiveSample latest_;
    RawAxes min_{}, max_{}, peak_{};
    uint32_t everPressed_ = 0;
    int64_t lastNs_ = 0;
    bool has_ = false, seeded_ = false;
    uint64_t samples_ = 0;
    std::vector<LiveDevice> devices_;
    uint32_t infoSeen_ = 0;
    bool hasDevices_ = false;
};

}  // namespace spn
