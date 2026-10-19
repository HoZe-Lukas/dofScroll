// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// OS-independent part of the raw-HID input backends (Windows, macOS): matches attached HID devices
// against the device descriptions (devices/*.conf), keeps one state per physical device and turns
// raw reports into InputEvents (translation and rotation may arrive as separate reports, buttons
// as a bitmask). The OS backends only enumerate devices, read reports and call into this class, so
// everything here is unit-tested on every platform (tests/test_hid_input_core.cpp).
//
// Values: the report values times the description's `scale` are passed on, clamped to
// +-kHidAxisLimit; the shipped descriptions give about +-350 at full deflection (the libspnav
// scale the core expects). Whether the axis directions and order of a given model match what
// spacenavd delivers is unverified without hardware; a wrong sign is fixed with a negative
// `scale` in the description file.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "../hid_descriptor.hpp"
#include "platform.hpp"

namespace spn {

constexpr int kHidAxisLimit = 32767;

class HidInputCore {
public:
    explicit HidInputCore(std::vector<HidDescriptor> descriptors) : descs_(std::move(descriptors)) {}

    // Index of the description for a vid:pid pair, -1 if the model is unknown.
    int match(unsigned vid, unsigned pid) const {
        for (size_t i = 0; i < descs_.size(); ++i)
            for (const auto &id : descs_[i].ids)
                if (id.first == vid && id.second == pid) return static_cast<int>(i);
        return -1;
    }
    bool known(unsigned vid, unsigned pid) const { return match(vid, pid) >= 0; }

    // Registers a physical device (one call per HID interface that delivers motion/buttons).
    // Returns its id (> 0) or 0 if the model is unknown. `productName` (from the OS) is used when
    // the description has no name. Appends a DevicesChanged event to *out (may be null).
    int addDevice(unsigned vid, unsigned pid, const std::string &productName, std::vector<InputEvent> *out) {
        const int di = match(vid, pid);
        if (di < 0) return 0;
        std::lock_guard<std::mutex> lk(mu_);
        Dev d;
        d.desc = di;
        d.info.id = next_id_++;
        d.info.name = descs_[static_cast<size_t>(di)].name.empty() ? productName : descs_[static_cast<size_t>(di)].name;
        d.info.vendor = vid;
        d.info.product = pid;
        const HidDescriptor &hd = descs_[static_cast<size_t>(di)];
        d.info.reportedButtons = hd.factoryButtons > 0 ? hd.factoryButtons : static_cast<int>(hd.buttonBits);
        d.info.axes = 6;
        const int id = d.info.id;
        devs_[id] = d;
        pushChanged(out);
        return id;
    }

    void removeDevice(int id, std::vector<InputEvent> *out) {
        std::lock_guard<std::mutex> lk(mu_);
        if (devs_.erase(id)) pushChanged(out);
    }

    // Decodes one raw report (byte 0 = report id) of device `id` and appends the resulting events:
    // one Motion event with the current value of all six axes when an axis changed, and one Button
    // event per changed button bit (lowest bit first). Unknown devices, foreign report ids and
    // short reports produce nothing. Returns the number of appended events.
    size_t onReport(int id, const uint8_t *data, size_t len, std::vector<InputEvent> *out) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = devs_.find(id);
        if (it == devs_.end() || !out) return 0;
        Dev &d = it->second;
        HidDecoded dec;
        if (!decodeHidReport(descs_[static_cast<size_t>(d.desc)], data, len, dec)) return 0;
        size_t n = 0;
        bool moved = false;
        for (int i = 0; i < 6; ++i) {
            if (!dec.axisSet[i]) continue;
            const int v = clampAxis(dec.axis[i]);
            if (v != d.axes[static_cast<size_t>(i)]) moved = true;
            d.axes[static_cast<size_t>(i)] = v;
        }
        if (moved) {
            InputEvent e;
            e.type = InputEventType::Motion;
            e.device = id;
            e.axes = d.axes;
            out->push_back(e);
            ++n;
        }
        if (dec.buttonsSet) {
            const uint32_t diff = dec.buttons ^ d.buttons;
            for (int b = 0; b < 32; ++b) {
                if (!(diff & (1u << b))) continue;
                InputEvent e;
                e.type = InputEventType::Button;
                e.device = id;
                e.button = b;
                e.down = (dec.buttons & (1u << b)) != 0;
                out->push_back(e);
                ++n;
            }
            d.buttons = dec.buttons;
        }
        return n;
    }

    // Zero motion and release all buttons of a device (sleep/wake, device removal in flight);
    // appends the events that bring the consumer to the neutral state.
    void reset(int id, std::vector<InputEvent> *out) {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = devs_.find(id);
        if (it == devs_.end() || !out) return;
        Dev &d = it->second;
        for (int b = 0; b < 32; ++b) {
            if (!(d.buttons & (1u << b))) continue;
            InputEvent e;
            e.type = InputEventType::Button;
            e.device = id;
            e.button = b;
            e.down = false;
            out->push_back(e);
        }
        d.buttons = 0;
        if (std::any_of(d.axes.begin(), d.axes.end(), [](int v) { return v != 0; })) {
            d.axes = {};
            InputEvent e;
            e.type = InputEventType::Motion;
            e.device = id;
            out->push_back(e);
        }
    }

    std::vector<DeviceInfo> devices() const {
        std::lock_guard<std::mutex> lk(mu_);
        std::vector<DeviceInfo> v;
        for (const auto &kv : devs_) v.push_back(kv.second.info);
        return v;
    }

    const std::vector<HidDescriptor> &descriptors() const { return descs_; }

private:
    struct Dev {
        int desc = -1;
        DeviceInfo info;
        std::array<int, 6> axes{};
        uint32_t buttons = 0;
    };

    static int clampAxis(double v) {
        if (!std::isfinite(v)) return 0;
        const double r = std::round(v);
        return static_cast<int>(std::max<double>(-kHidAxisLimit, std::min<double>(kHidAxisLimit, r)));
    }
    static void pushChanged(std::vector<InputEvent> *out) {
        if (!out) return;
        InputEvent e;
        e.type = InputEventType::DevicesChanged;
        out->push_back(e);
    }

    std::vector<HidDescriptor> descs_;
    mutable std::mutex mu_;
    std::map<int, Dev> devs_;
    int next_id_ = 1;
};

}  // namespace spn
