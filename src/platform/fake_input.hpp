// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Scripted InputSource for unit tests and for developing a backend without hardware.
#pragma once

#include <deque>
#include <string>
#include <vector>

#include "platform.hpp"

namespace spn {

class FakeInputSource : public InputSource {
public:
    explicit FakeInputSource(std::vector<DeviceInfo> devs = {}) : devs_(std::move(devs)) {}
    void push(const InputEvent &e) { q_.push_back(e); }
    void pushMotion(int dev, int x, int y, int z, int rx, int ry, int rz) {
        InputEvent e;
        e.device = dev;
        e.axes = {x, y, z, rx, ry, rz};
        push(e);
    }
    void pushButton(int dev, int button, bool down) {
        InputEvent e;
        e.type = InputEventType::Button;
        e.device = dev;
        e.button = button;
        e.down = down;
        push(e);
    }
    const char *name() const override { return "fake"; }
    bool open(std::string *) override { open_ = true; return true; }
    WaitResult wait(InputEvent *out, int) override {
        if (!open_) return WaitResult::Closed;
        if (q_.empty()) return WaitResult::Timeout;
        *out = q_.front();
        q_.pop_front();
        return WaitResult::Event;
    }
    std::vector<DeviceInfo> devices() const override { return devs_; }
    void close() override { open_ = false; }

private:
    std::vector<DeviceInfo> devs_;
    std::deque<InputEvent> q_;
    bool open_ = false;
};

}  // namespace spn
