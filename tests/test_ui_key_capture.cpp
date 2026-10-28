// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Press a key" (issue #174): the chord from the pressed key and the held modifiers.
#undef NDEBUG
#include <cassert>
#include <cstdio>

#include "modes.hpp"
#include "ui/model/key_capture_model.hpp"

using namespace spn;
using namespace spn::ui;

int main() {
    const IdentityStrings s;
    HeldMods none, ctrl, all;
    ctrl.ctrl = true;
    all.ctrl = all.shift = all.alt = all.super = true;
    // a plain key, a combination in the fixed modifier order
    CaptureResult r = captureChord("pageup", none, s);
    assert(r.kind == CaptureResult::Chord && r.chord == "pageup");
    r = captureChord("pageup", ctrl, s);
    assert(r.kind == CaptureResult::Chord && r.chord == "ctrl+pageup");
    r = captureChord("a", all, s);
    assert(r.kind == CaptureResult::Chord && r.chord == "ctrl+shift+alt+super+a");
    // a modifier alone waits for the key that goes with it
    for (const char *m : {"ctrl", "shift", "alt", "super"}) assert(captureChord(m, none, s).kind == CaptureResult::Waiting);
    // a key dofscroll cannot send says so and gives no chord
    r = captureChord("", none, s);
    assert(r.kind == CaptureResult::Unsupported && r.chord.empty() && !r.message.empty());
    r = captureChord("capslock", ctrl, s);
    assert(r.kind == CaptureResult::Unsupported);
    // every key of keys.hpp that is not a modifier gives a chord the mode loader reads
    for (size_t i = 0; i < kKeyCount; ++i) {
        const std::string name = keyName(static_cast<Key>(i));
        r = captureChord(name, ctrl, s);
        if (isModifierName(name)) {
            assert(r.kind == CaptureResult::Waiting);
            continue;
        }
        assert(r.kind == CaptureResult::Chord);
        ChannelAction a;
        std::string err;
        assert(parseChannelAction("step:" + r.chord + "," + r.chord, &a, &err));
    }
    std::puts("test_ui_key_capture OK");
    return 0;
}
