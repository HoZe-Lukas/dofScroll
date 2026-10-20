// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Autosave rule of the settings window, without any toolkit and without a real clock: every edit
// is written by itself, nothing has to be saved by hand and there is no "unsaved" state to ask
// about.
//
//   Autosave a(dirtyFn, saveFn, strings);
//   a.request(Autosave::Urgency::Immediate, now);  // switch, dropdown, slider, spin box (a committed value)
//   a.request(Autosave::Urgency::Debounced, now);  // text field: quiet for debounceMs first
//   a.tick(now);       // from a timer: saves when the deadline has passed
//   a.flush(now);      // page switch, profile switch, close, quit: saves now, synchronously
//
// Edits that arrive together are written together: an Immediate request is due at once but is
// only carried out by the next tick(), so a burst of changes of one gesture becomes one write
// (and one daemon reload). A Debounced request is due debounceMs after the last request, but at
// the latest maxWaitMs after the first one of the burst, so continuous typing cannot postpone the
// write forever. After a failed save nothing is retried by itself (no loop on a broken
// configuration); the next edit or flush() tries again and the failure stays in lastResult().
//
// The caller owns the clock (milliseconds, monotonic); tests pass any numbers.
#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>

#include "ui/model/strings.hpp"

namespace spn::ui {

class Autosave {
public:
    enum class Urgency { Immediate, Debounced };
    enum class State { None, Saved, SavedNotNotified, Failed };
    struct Outcome {
        bool saved = false;     // the file was written
        bool reloaded = true;   // the daemon was told (true when there is nothing to tell)
        std::string error;      // validation, write or reload error
    };
    using DirtyFn = std::function<bool()>;
    using SaveFn = std::function<Outcome()>;

    static constexpr int64_t kDebounceMs = 600;
    static constexpr int64_t kMaxWaitMs = 3000;
    static constexpr int64_t kStatusMs = 3000;  // how long "Saved" stays in the status line

    Autosave(DirtyFn dirty, SaveFn save) : dirty_(std::move(dirty)), save_(std::move(save)) {}

    void setDebounceMs(int64_t ms) { debounce_ = ms; }
    void setMaxWaitMs(int64_t ms) { maxWait_ = ms; }

    void request(Urgency u, int64_t now) {
        if (!pending_) {
            pending_ = true;
            firstAt_ = now;
            dueAt_ = u == Urgency::Immediate ? now : now + debounce_;
            urgent_ = u == Urgency::Immediate;
        } else if (u == Urgency::Immediate) {
            dueAt_ = std::min(dueAt_, now);
            urgent_ = true;
        } else if (!urgent_) {
            dueAt_ = std::min(now + debounce_, firstAt_ + maxWait_);
        }
    }

    bool pending() const { return pending_; }
    // Milliseconds until the pending save is due (0 = now); -1 without a pending save.
    int64_t msUntilDue(int64_t now) const { return pending_ ? std::max<int64_t>(0, dueAt_ - now) : -1; }

    // Carries out the pending save if it is due. true if a save was attempted.
    bool tick(int64_t now) {
        if (!pending_ || now < dueAt_) return false;
        return run(now);
    }
    // Saves now if anything is pending or the model still holds edits that were not written (for
    // example after a failed save). true if a save was attempted.
    bool flush(int64_t now) {
        if (!pending_ && !(dirty_ && dirty_())) return false;
        return run(now);
    }

    State state() const { return state_; }
    const std::string &lastError() const { return error_; }
    // The text for the footer status line: "" when there is nothing to say (never saved, or the
    // "Saved" message has expired). A failure stays until the next successful save.
    std::string statusText(int64_t now, const StringProvider &s) const {
        switch (state_) {
            case State::None: return std::string();
            case State::Saved: return now - savedAt_ < kStatusMs ? s.tr("Saved") : std::string();
            case State::SavedNotNotified:
                return now - savedAt_ < kStatusMs ? format(s.tr("Saved, but the daemon was not notified: %1"), {error_})
                                                  : std::string();
            case State::Failed: return format(s.tr("Could not save: %1"), {error_});
        }
        return std::string();
    }
    // Number of save attempts so far (tests, diagnostics).
    int attempts() const { return attempts_; }

private:
    bool run(int64_t now) {
        pending_ = false;
        if (dirty_ && !dirty_()) return false;  // nothing to write (already saved by someone else)
        ++attempts_;
        const Outcome o = save_ ? save_() : Outcome{false, true, "no way to save"};
        savedAt_ = now;
        error_ = o.error;
        state_ = !o.saved ? State::Failed : o.reloaded ? State::Saved : State::SavedNotNotified;
        if (state_ == State::Saved) error_.clear();
        return true;
    }

    DirtyFn dirty_;
    SaveFn save_;
    int64_t debounce_ = kDebounceMs;
    int64_t maxWait_ = kMaxWaitMs;
    bool pending_ = false, urgent_ = false;
    int64_t firstAt_ = 0, dueAt_ = 0, savedAt_ = 0;
    State state_ = State::None;
    std::string error_;
    int attempts_ = 0;
};

}  // namespace spn::ui
