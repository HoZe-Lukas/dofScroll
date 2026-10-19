// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Registry of the live outputs of the process so that every exit path (clean shutdown, atexit,
// std::terminate, system sleep) can release the keys and buttons this process pressed. Pure
// logic, no macOS headers: unit-tested on every platform (tests/test_mac_release.cpp).
#pragma once
#include <algorithm>
#include <mutex>
#include <vector>

#include "../../output.hpp"

namespace spn {
namespace mac {

class ReleaseGuard {
public:
    void add(Output *o) {
        std::lock_guard<std::mutex> lk(m_);
        if (o && std::find(outs_.begin(), outs_.end(), o) == outs_.end()) outs_.push_back(o);
    }
    void remove(Output *o) {
        std::lock_guard<std::mutex> lk(m_);
        outs_.erase(std::remove(outs_.begin(), outs_.end(), o), outs_.end());
    }
    size_t count() {
        std::lock_guard<std::mutex> lk(m_);
        return outs_.size();
    }
    // Releases everything every registered output holds (Output::releaseAll: only what this
    // process pressed). Returns false without doing anything if the registry is locked, so a
    // handler that runs while the lock is held (crash inside add/remove) cannot deadlock.
    bool releaseAll() {
        std::unique_lock<std::mutex> lk(m_, std::try_to_lock);
        if (!lk.owns_lock()) return false;
        for (Output *o : outs_) {
            o->releaseAll();
            o->flush();
        }
        return true;
    }

private:
    std::mutex m_;
    std::vector<Output *> outs_;
};

}  // namespace mac
}  // namespace spn
