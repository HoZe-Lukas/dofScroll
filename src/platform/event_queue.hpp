// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Thread-safe event queue between the OS callback/reader threads of an HID backend and
// InputSource::wait() (called by the daemon's reader thread). Bounded: when the consumer stalls,
// the oldest motion events are dropped (the newest motion state is what matters); button and
// devices-changed events are never dropped.
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>

#include "platform.hpp"

namespace spn {

class EventQueue {
public:
    explicit EventQueue(size_t maxEvents = 4096) : max_(maxEvents) {}

    void push(const InputEvent &e) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (closed_) return;
            if (q_.size() >= max_) dropOneMotion();
            q_.push_back(e);
        }
        cv_.notify_one();
    }
    void push(const std::vector<InputEvent> &v) {
        for (const InputEvent &e : v) push(e);
    }

    // Blocks up to timeoutMs. Event: *out set. Timeout: nothing arrived. Closed: close() was
    // called and the queue is drained.
    WaitResult wait(InputEvent *out, int timeoutMs) {
        std::unique_lock<std::mutex> lk(mu_);
        if (!cv_.wait_for(lk, std::chrono::milliseconds(timeoutMs < 0 ? 0 : timeoutMs),
                          [&] { return !q_.empty() || closed_; }))
            return WaitResult::Timeout;
        if (q_.empty()) return WaitResult::Closed;
        *out = q_.front();
        q_.pop_front();
        return WaitResult::Event;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            closed_ = true;
        }
        cv_.notify_all();
    }
    // Reopen after close() (InputSource::open() may be called again after a failure).
    void reopen() {
        std::lock_guard<std::mutex> lk(mu_);
        closed_ = false;
        q_.clear();
    }
    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return q_.size();
    }

private:
    // Drops the oldest Motion event; if there is none, the queue grows past max_ (those are
    // rare button/device events).
    void dropOneMotion() {
        for (auto it = q_.begin(); it != q_.end(); ++it)
            if (it->type == InputEventType::Motion) {
                q_.erase(it);
                return;
            }
    }

    size_t max_;
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<InputEvent> q_;
    bool closed_ = false;
};

}  // namespace spn
