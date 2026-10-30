// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The step list of a macro as the interface edits it (stage 7a/7b of docs/plan-motion-functions.md), toolkit-free: the steps
// (key down, key up, pause), the editing operations (add, change, delete, reorder, at most 256 steps), the text of one side
// ("+ctrl +c wait:30 -c -ctrl") and the recorder that turns key presses into steps. The steps, the grammar, the text and the
// limits are the ones of the macro core (src/macro.hpp: spn::MacroStep, parseMacroSteps, macroStepsText, kMaxMacroSteps), so the
// dialog and the daemon's loader agree; this file only adds the list the dialog edits and the recorder. Every composed action
// is checked by the daemon's parsers before it is written (new_function_model.hpp).
//
//   MacroList s;  s.add(macroDown("ctrl"));  s.add(MacroStep::wait(30));  s.text();   // "+ctrl wait:30"
//   MacroRecorder r(true);  r.start();  r.press("a", 0);  r.release("a", 80);                 // +a wait:80 -a
//
// Recording is a function of the dialog only: the recorder gets the key events the dialog hands it (while the dialog has the
// focus) and keeps nothing outside the steps; nothing here reads the keyboard by itself.
#pragma once
#include <algorithm>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

#include "keys.hpp"
#include "macro.hpp"

namespace spn::ui {

// A step from a chord text ("ctrl+c", "a"): false + *err (the core's message) if it is not valid.
inline bool makeMacroKeyStep(bool down, const std::string &chord, MacroStep *out, std::string *err = nullptr) {
    std::string e;
    return parseMacroStep((down ? "+" : "-") + chord, out, err ? err : &e);
}
// The chord text of a Down or Up step ("ctrl+c"); "" for a pause.
inline std::string macroStepChord(const MacroStep &s) { return s.kind == MacroStep::Kind::Wait ? std::string() : macroStepText(s).substr(1); }
inline bool macroStepValid(const MacroStep &s) {
    return s.kind == MacroStep::Kind::Wait ? (s.waitMs >= kMinMacroWaitMs && s.waitMs <= kMaxMacroWaitMs) : true;
}
// Down/up step of a chord text, or an invalid pause (0 ms) when the chord is not valid: add() and set() reject it.
inline MacroStep macroDown(const std::string &chord) { MacroStep s = MacroStep::wait(0); makeMacroKeyStep(true, chord, &s); return s; }
inline MacroStep macroUp(const std::string &chord) { MacroStep s = MacroStep::wait(0); makeMacroKeyStep(false, chord, &s); return s; }

class MacroList {
public:
    const MacroSteps &steps() const { return steps_; }
    size_t size() const { return steps_.size(); }
    bool empty() const { return steps_.empty(); }
    bool full() const { return steps_.size() >= kMaxMacroSteps; }
    const MacroStep &at(size_t i) const { return steps_[i]; }

    // false when the list is full (nothing added) or the step is not valid (a pause outside 1..60000 ms).
    bool add(const MacroStep &s) { return insert(steps_.size(), s); }
    bool insert(size_t at, const MacroStep &s) {
        if (full() || !macroStepValid(s) || at > steps_.size()) return false;
        steps_.insert(steps_.begin() + long(at), s);
        return true;
    }
    bool set(size_t i, const MacroStep &s) {
        if (i >= steps_.size() || !macroStepValid(s)) return false;
        steps_[i] = s;
        return true;
    }
    bool remove(size_t i) {
        if (i >= steps_.size()) return false;
        steps_.erase(steps_.begin() + long(i));
        return true;
    }
    bool moveUp(size_t i) {
        if (i == 0 || i >= steps_.size()) return false;
        std::swap(steps_[i], steps_[i - 1]);
        return true;
    }
    bool moveDown(size_t i) {
        if (i + 1 >= steps_.size()) return false;
        std::swap(steps_[i], steps_[i + 1]);
        return true;
    }
    void clear() { steps_.clear(); }

    // The side as action text (macroStepsText of the core); "" for an empty list (the core writes "none").
    std::string text() const { return steps_.empty() ? std::string() : macroStepsText(steps_); }
    // Reads the text of one side with the core's parser. false + *err for a step that is not valid or more than 256 steps.
    bool parse(const std::string &text, std::string *err) {
        MacroSteps out;
        std::string e;
        if (!parseMacroSteps(text, &out, err ? err : &e)) return false;
        steps_ = out;
        return true;
    }
    bool operator==(const MacroList &o) const { return steps_ == o.steps_; }

private:
    MacroSteps steps_;
};

// Records key events into steps. The times are the ones the dialog measured (milliseconds on any clock). With `recordTimes`
// the pause between two events is the measured gap (at least 1 ms, at most 60 s); without, the default pause of 30 ms is put
// between the steps. A key that is already down (the keyboard repeat) and a release of a key that was not pressed while
// recording are ignored. The 256th step ends the recording (full()).
class MacroRecorder {
public:
    explicit MacroRecorder(bool recordTimes = true) : times_(recordTimes) {}
    void setRecordTimes(bool on) { times_ = on; }
    bool recordTimes() const { return times_; }

    // Starts a recording that appends to the steps (the earlier ones stay).
    void start(const MacroList &existing = MacroList()) {
        steps_ = existing;
        held_.clear();
        recording_ = true;
        full_ = false;
        any_ = false;
        escRun_ = 0;
    }
    void stop() {
        recording_ = false;
        held_.clear();
    }
    bool recording() const { return recording_; }
    bool full() const { return full_; }
    const MacroList &steps() const { return steps_; }
    // Escape pressed three times in a row ends the recording (the way out for the keyboard, which the recording would
    // otherwise swallow): those three presses are not part of the macro.
    bool endedByEscape() const { return endedByEscape_; }

    // Result of an event: Recorded, Ignored (repeat, release without press, not recording) or Full (the steps are full).
    enum Result { Recorded, Ignored, Full };
    Result press(const std::string &key, long long nowMs) {
        if (!recording_) return Ignored;
        if (held_.count(key)) return Ignored;
        if (key == "esc") {
            if (++escRun_ >= 3) {
                dropTrailingEscapes();
                stop();
                endedByEscape_ = true;
                return Ignored;
            }
        } else {
            escRun_ = 0;
        }
        const Result r = append(macroDown(key), nowMs);
        if (r == Recorded) held_.insert(key);
        return r;
    }
    Result release(const std::string &key, long long nowMs) {
        if (!recording_ || !held_.count(key)) return Ignored;
        const Result r = append(macroUp(key), nowMs);
        held_.erase(key);
        return r;
    }
    void resetEndedByEscape() { endedByEscape_ = false; }

private:
    Result append(const MacroStep &s, long long nowMs) {
        // the pause before the step, then the step: both must fit
        const bool pause = any_;
        if (steps_.size() + (pause ? 2 : 1) > kMaxMacroSteps) {
            full_ = true;
            recording_ = false;
            return Full;
        }
        if (pause) {
            long long gap = times_ ? nowMs - lastMs_ : kDefaultMacroWaitMs;
            if (gap > kMaxMacroWaitMs) gap = kMaxMacroWaitMs;
            if (gap >= kMinMacroWaitMs) steps_.add(MacroStep::wait(int(gap)));
        }
        steps_.add(s);
        lastMs_ = nowMs;
        any_ = true;
        return Recorded;
    }
    // Takes the two earlier Escape presses (and the pauses around them) away again; the third was not added.
    void dropTrailingEscapes() {
        spn::MacroSteps v = steps_.steps();
        int removed = 0;
        while (!v.empty() && removed < 2) {
            const MacroStep &s = v.back();
            if (s.kind == MacroStep::Kind::Wait || macroStepChord(s) == "esc") {
                if (s.kind == MacroStep::Kind::Down) ++removed;
                v.pop_back();
            } else {
                break;
            }
        }
        if (!v.empty() && v.back().kind == MacroStep::Kind::Wait && removed == 2) v.pop_back();  // the pause before the first one
        MacroList out;
        for (const MacroStep &s : v) out.add(s);
        steps_ = out;
    }

    bool times_ = true, recording_ = false, full_ = false, any_ = false, endedByEscape_ = false;
    long long lastMs_ = 0;
    int escRun_ = 0;
    MacroList steps_;
    std::set<std::string> held_;
};

}  // namespace spn::ui
