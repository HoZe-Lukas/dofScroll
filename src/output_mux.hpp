// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Several SpaceMice at once: every device has its own ModeRunner, but there is one real Output.
// OutputMux hands each runner a view of that output and counts who holds what: a key or mouse
// button is pressed on the real output when the first view presses it and released when the last
// view lets go. So releasing everything of one device (unplug, mode change, safety net) never
// lets go of a key another device still holds, e.g. Ctrl for zoom on both devices.
// Wheel steps, taps and flush go straight through. Single-threaded (main thread only).
#pragma once
#include <array>
#include <cstdint>
#include <memory>

#include "output.hpp"

namespace spn {

class OutputMux {
public:
    static constexpr int kMaxViews = 8;
    static constexpr int kMouseButtons = 8;  // mouse buttons 1..kMouseButtons

    explicit OutputMux(Output *real) : real_(real) {}

    // A view for one device; owned by the mux. At most kMaxViews at a time (nullptr when all are in use).
    Output *addView() {
        for (size_t i = 0; i < views_.size(); ++i)
            if (!views_[i]) {
                views_[i] = std::make_unique<View>(this, static_cast<int>(i));
                return views_[i].get();
            }
        return nullptr;
    }
    // The device is gone: releases what its view holds and frees the view (the user must not use it anymore).
    void removeView(Output *view) {
        for (auto &v : views_)
            if (v && v.get() == view) {
                releaseSlot(v->slot());
                v.reset();
                return;
            }
    }
    // Releases every key and button of every view, also on the real output (safety net, shutdown).
    void releaseEverything() {
        keys_.fill(0);
        mouse_.fill(0);
        real_->releaseAll();
    }

    // Releases only what the given view holds (what other views hold stays pressed).
    void releaseView(Output *view) {
        for (auto &v : views_)
            if (v && v.get() == view) {
                releaseSlot(v->slot());
                return;
            }
    }
    bool viewHolds(const Output *view) const {
        for (const auto &v : views_)
            if (v && v.get() == view) {
                const uint32_t bit = 1u << v->slot();
                for (uint32_t m : keys_)
                    if (m & bit) return true;
                for (uint32_t m : mouse_)
                    if (m & bit) return true;
            }
        return false;
    }

private:
    class View : public Output {
    public:
        View(OutputMux *m, int slot) : m_(m), slot_(slot) {}
        int slot() const { return slot_; }
        const char *name() const override { return m_->real_->name(); }
        bool init(std::string *) override { return true; }
        void wheelV(int n) override { m_->real_->wheelV(n); }
        void wheelH(int n) override { m_->real_->wheelH(n); }
        void setKey(Key k, bool down) override { m_->setKey(slot_, k, down); }
        void tapKey(Key k) override { m_->real_->tapKey(k); }
        void mouseButton(int b, bool down) override { m_->mouseButton(slot_, b, down); }
        void releaseAll() override { m_->releaseSlot(slot_); }
        void flush() override { m_->real_->flush(); }

    private:
        OutputMux *m_;
        int slot_;
    };

    void setKey(int slot, Key k, bool down) {
        uint32_t &m = keys_[static_cast<size_t>(k)];
        const bool was = m != 0;
        if (down) m |= 1u << slot;
        else m &= ~(1u << slot);
        if (was != (m != 0)) real_->setKey(k, m != 0);
    }
    void mouseButton(int slot, int b, bool down) {
        if (b < 1 || b > kMouseButtons) {
            real_->mouseButton(b, down);
            return;
        }
        uint32_t &m = mouse_[static_cast<size_t>(b - 1)];
        const bool was = m != 0;
        if (down) m |= 1u << slot;
        else m &= ~(1u << slot);
        if (was != (m != 0)) real_->mouseButton(b, m != 0);
    }
    void releaseSlot(int slot) {
        const uint32_t bit = 1u << slot;
        for (size_t k = 0; k < keys_.size(); ++k)
            if (keys_[k] & bit) setKey(slot, static_cast<Key>(k), false);
        for (size_t b = 0; b < mouse_.size(); ++b)
            if (mouse_[b] & bit) mouseButton(slot, static_cast<int>(b) + 1, false);
    }

    Output *real_;
    std::array<uint32_t, kKeyCount> keys_{};
    std::array<uint32_t, kMouseButtons> mouse_{};
    std::array<std::unique_ptr<View>, kMaxViews> views_;
};

}  // namespace spn
