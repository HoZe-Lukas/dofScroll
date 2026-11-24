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
// The chord text of a Down, Up or Tap step ("ctrl+c"); "" for every other step.
inline std::string macroStepChord(const MacroStep &s) {
    switch (s.kind) {
        case MacroStep::Kind::Down:
        case MacroStep::Kind::Up: return macroStepText(s).substr(1);
        case MacroStep::Kind::Tap: return macroStepText(s).substr(4);  // "tap:"
        default: return std::string();
    }
}
// The steps that press or release keys (they have a chord).
inline bool macroStepHasChord(const MacroStep &s) {
    return s.kind == MacroStep::Kind::Down || s.kind == MacroStep::Kind::Up || s.kind == MacroStep::Kind::Tap;
}
// A step as the dialog may hold it: a pause in range, a text the core accepts for paste:, a repeat count in range, a real mouse button.
inline bool macroStepValid(const MacroStep &s) {
    switch (s.kind) {
        case MacroStep::Kind::Wait: return s.waitMs >= kMinMacroWaitMs && s.waitMs <= kMaxMacroWaitMs;
        case MacroStep::Kind::Paste: return macroPasteTextProblem(s.text).empty();
        case MacroStep::Kind::RepeatBegin: return s.count >= 0 && s.count <= kMaxMacroRepeat;
        case MacroStep::Kind::MouseDown:
        case MacroStep::Kind::MouseUp:
        case MacroStep::Kind::MouseTap: return s.mouse >= 1 && s.mouse <= 3;
        default: return true;
    }
}
// Down/up/tap step of a chord text, or an invalid pause (0 ms) when the chord is not valid: add() and set() reject it.
inline MacroStep macroDown(const std::string &chord) { MacroStep s = MacroStep::wait(0); makeMacroKeyStep(true, chord, &s); return s; }
inline MacroStep macroUp(const std::string &chord) { MacroStep s = MacroStep::wait(0); makeMacroKeyStep(false, chord, &s); return s; }
inline MacroStep macroTap(const std::string &chord) { MacroStep s = MacroStep::wait(0); parseMacroStep("tap:" + chord, &s, nullptr); return s; }
// A mouse step (kind MouseTap, MouseDown or MouseUp; button 1 left, 2 middle, 3 right), a repeat block start (count 0 = until the
// trigger is released) and a pasted text; an invalid pause (0 ms) when the text is not valid.
inline MacroStep macroMouse(MacroStep::Kind kind, int button) { return MacroStep::mouseStep(kind, button); }
inline MacroStep macroRepeat(int count) { return MacroStep::repeatBegin(count); }
inline MacroStep macroPaste(const std::string &text) { return macroPasteTextProblem(text).empty() ? MacroStep::paste(text) : MacroStep::wait(0); }

class MacroList {
public:
    const MacroSteps &steps() const { return steps_; }
    size_t size() const { return steps_.size(); }
    bool empty() const { return steps_.empty(); }
    bool full() const { return steps_.size() >= kMaxMacroSteps; }
    // "" if the list is a valid macro part (repeat blocks closed and not nested, at most 256 steps after they are expanded, texts and
    // pauses in range); else the core's message. allowInfinite: a "repeat" without a count (the press part of a while_held side).
    std::string problem(bool allowInfinite = false) const { return macroStepsProblem(steps_, allowInfinite); }
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
    bool parse(const std::string &text, std::string *err, bool allowInfinite = false) {
        MacroSteps out;
        std::string e;
        if (!parseMacroSteps(text, &out, err ? err : &e, allowInfinite)) return false;
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
    // A key pressed and let go again at once (within kTapMergeMs, or always without times) is recorded as one tap: step
    // instead of "+key wait -key". Off by default here (the editor turns it on).
    void setMergeTaps(bool on) { merge_ = on; }
    bool mergeTaps() const { return merge_; }
    static constexpr long long kTapMergeMs = 150;

    // Starts a recording. The earlier steps stay; the new ones go in at `insertAt` (a position 0..size, behind the step the
    // cursor is on), by default at the end. A position past the end means the end.
    void start(const MacroList &existing = MacroList(), size_t insertAt = static_cast<size_t>(-1)) {
        const size_t at = std::min(insertAt, existing.size());
        before_.clear();
        after_.clear();
        rec_.clear();
        for (size_t i = 0; i < existing.size(); ++i) (i < at ? before_ : after_).add(existing.at(i));
        compose();
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
    // All steps: the ones before the cursor, the recorded ones, the ones after.
    const MacroList &steps() const { return all_; }
    // The position of the last recorded step in steps() (where the list selects after a key), or -1 while nothing is recorded.
    long lastRecorded() const { return rec_.empty() ? -1L : long(before_.size() + rec_.size()) - 1L; }
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
        if (merge_ && !rec_.empty() && rec_.at(rec_.size() - 1) == macroDown(key) && (!times_ || nowMs - lastMs_ <= kTapMergeMs)) {
            rec_.set(rec_.size() - 1, macroTap(key));  // the press right before: one tap
            compose();
            lastMs_ = nowMs;
            held_.erase(key);
            return Recorded;
        }
        const Result r = append(macroUp(key), nowMs);
        held_.erase(key);
        return r;
    }
    void resetEndedByEscape() { endedByEscape_ = false; }

private:
    Result append(const MacroStep &s, long long nowMs) {
        // the pause before the step, then the step: both must fit
        const bool pause = any_;
        if (all_.size() + (pause ? 2 : 1) > kMaxMacroSteps) {
            full_ = true;
            recording_ = false;
            return Full;
        }
        if (pause) {
            long long gap = times_ ? nowMs - lastMs_ : kDefaultMacroWaitMs;
            if (gap > kMaxMacroWaitMs) gap = kMaxMacroWaitMs;
            if (gap >= kMinMacroWaitMs) rec_.add(MacroStep::wait(int(gap)));
        }
        rec_.add(s);
        compose();
        lastMs_ = nowMs;
        any_ = true;
        return Recorded;
    }
    void compose() {
        all_.clear();
        for (const MacroList *l : {&before_, &rec_, &after_})
            for (size_t i = 0; i < l->size(); ++i) all_.add(l->at(i));
    }
    // Takes the two earlier Escape presses (and the pauses around them) away again; the third was not added. Only the
    // recorded steps are touched, never what was there before.
    void dropTrailingEscapes() {
        spn::MacroSteps v = rec_.steps();
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
        rec_.clear();
        for (const MacroStep &s : v) rec_.add(s);
        compose();
    }

    bool merge_ = false, times_ = true, recording_ = false, full_ = false, any_ = false, endedByEscape_ = false;
    long long lastMs_ = 0;
    int escRun_ = 0;
    MacroList before_, rec_, after_, all_;
    std::set<std::string> held_;
};

}  // namespace spn::ui
