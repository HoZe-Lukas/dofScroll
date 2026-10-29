// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The step list of a macro as the interface edits it (stage 7a/7b of docs/plan-motion-functions.md), toolkit-free: the steps
// (key down, key up, pause), the editing operations (add, change, delete, reorder, at most 256 steps), the text of one side
// ("+ctrl +c wait:30 -c -ctrl") and the recorder that turns key presses into steps. The daemon's own grammar and limits are
// the ones of the action text (`macro:` of parseChannelAction / parseButtonAction); this file only builds and reads that text
// and every composed action is checked by those parsers before it is written (new_function_model.hpp).
//
//   MacroSteps s;  s.add(MacroStep::down("ctrl"));  s.add(MacroStep::wait(30));  s.text();   // "+ctrl wait:30"
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

namespace spn::ui {

constexpr size_t kMaxMacroSteps = 256;       // down, up and pause each count
constexpr int kMinMacroWaitMs = 1, kMaxMacroWaitMs = 60000;
constexpr int kDefaultMacroWaitMs = 30;      // between recorded steps when the times are not recorded

// The chord of a step: zero to four of ctrl, shift, alt, super, then a key (a lone modifier is the key itself).
inline bool validMacroChord(const std::string &chord) {
    if (chord.empty()) return false;
    std::vector<std::string> parts;
    size_t start = 0;
    for (size_t p; (p = chord.find('+', start)) != std::string::npos; start = p + 1) parts.push_back(chord.substr(start, p - start));
    parts.push_back(chord.substr(start));
    if (parts.size() > 5) return false;
    Key k;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (!parseKeyName(parts[i], &k)) return false;
        if (i + 1 < parts.size() && parts[i] != "ctrl" && parts[i] != "shift" && parts[i] != "alt" && parts[i] != "super") return false;
    }
    return true;
}

struct MacroStep {
    enum Type { Down, Up, Wait };
    Type type = Wait;
    std::string chord;  // Down, Up
    int ms = kDefaultMacroWaitMs;  // Wait
    static MacroStep down(const std::string &chord) { return MacroStep{Down, chord, 0}; }
    static MacroStep up(const std::string &chord) { return MacroStep{Up, chord, 0}; }
    static MacroStep wait(int ms) { return MacroStep{Wait, std::string(), ms}; }
    bool valid() const { return type == Wait ? (ms >= kMinMacroWaitMs && ms <= kMaxMacroWaitMs) : validMacroChord(chord); }
    // "+ctrl+c", "-c", "wait:30"
    std::string text() const { return type == Wait ? "wait:" + std::to_string(ms) : (type == Down ? "+" : "-") + chord; }
    bool operator==(const MacroStep &o) const { return type == o.type && chord == o.chord && ms == o.ms; }
};

// Reads one step ("+ctrl+c", "-c", "wait:30"); false for anything else.
inline bool parseMacroStep(const std::string &text, MacroStep *out) {
    if (text.size() > 5 && text.compare(0, 5, "wait:") == 0) {
        const std::string n = text.substr(5);
        if (n.size() > 5 || n.find_first_not_of("0123456789") != std::string::npos) return false;
        *out = MacroStep::wait(std::stoi(n));
        return out->valid();
    }
    if (text.size() > 1 && (text[0] == '+' || text[0] == '-')) {
        *out = text[0] == '+' ? MacroStep::down(text.substr(1)) : MacroStep::up(text.substr(1));
        return out->valid();
    }
    return false;
}

class MacroSteps {
public:
    const std::vector<MacroStep> &steps() const { return steps_; }
    size_t size() const { return steps_.size(); }
    bool empty() const { return steps_.empty(); }
    bool full() const { return steps_.size() >= kMaxMacroSteps; }
    const MacroStep &at(size_t i) const { return steps_[i]; }

    // false when the list is full (nothing added) or the step is not valid.
    bool add(const MacroStep &s) { return insert(steps_.size(), s); }
    bool insert(size_t at, const MacroStep &s) {
        if (full() || !s.valid() || at > steps_.size()) return false;
        steps_.insert(steps_.begin() + long(at), s);
        return true;
    }
    bool set(size_t i, const MacroStep &s) {
        if (i >= steps_.size() || !s.valid()) return false;
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

    // The side as action text: steps separated by a space; "" for an empty list.
    std::string text() const {
        std::string out;
        for (const MacroStep &s : steps_) out += (out.empty() ? "" : " ") + s.text();
        return out;
    }
    // Reads the text of one side. false + *err (English source text) for a step that is not valid or more than 256 steps.
    bool parse(const std::string &text, std::string *err) {
        std::vector<MacroStep> out;
        size_t i = 0;
        while (i < text.size()) {
            const size_t sp = text.find(' ', i);
            const std::string word = text.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            i = sp == std::string::npos ? text.size() : sp + 1;
            if (word.empty()) continue;
            MacroStep s;
            if (!parseMacroStep(word, &s)) {
                if (err) *err = "unknown macro step: " + word.substr(0, 40);
                return false;
            }
            if (out.size() >= kMaxMacroSteps) {
                if (err) *err = "a macro has at most 256 steps";
                return false;
            }
            out.push_back(s);
        }
        steps_ = out;
        return true;
    }
    bool operator==(const MacroSteps &o) const { return steps_ == o.steps_; }

private:
    std::vector<MacroStep> steps_;
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
    void start(const MacroSteps &existing = MacroSteps()) {
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
    const MacroSteps &steps() const { return steps_; }
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
        const Result r = append(MacroStep::down(key), nowMs);
        if (r == Recorded) held_.insert(key);
        return r;
    }
    Result release(const std::string &key, long long nowMs) {
        if (!recording_ || !held_.count(key)) return Ignored;
        const Result r = append(MacroStep::up(key), nowMs);
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
        std::vector<MacroStep> v = steps_.steps();
        int removed = 0;
        while (!v.empty() && removed < 2) {
            const MacroStep &s = v.back();
            if (s.type == MacroStep::Wait || s.chord == "esc") {
                if (s.type == MacroStep::Down) ++removed;
                v.pop_back();
            } else {
                break;
            }
        }
        if (!v.empty() && v.back().type == MacroStep::Wait && removed == 2) v.pop_back();  // the pause before the first one
        MacroSteps out;
        for (const MacroStep &s : v) out.add(s);
        steps_ = out;
    }

    bool times_ = true, recording_ = false, full_ = false, any_ = false, endedByEscape_ = false;
    long long lastMs_ = 0;
    int escRun_ = 0;
    MacroSteps steps_;
    std::set<std::string> held_;
};

}  // namespace spn::ui
