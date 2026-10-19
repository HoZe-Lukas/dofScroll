// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS event output via Quartz Event Services (CGEvent). Plain C++ against the C APIs of
// ApplicationServices; the key, scroll and button tables are in mac_key_tables.hpp (unit-tested on
// every OS). Needs the Accessibility permission to have the events delivered; the permission is
// checked when the output is created and a missing grant is reported through the log string (the
// system prompt is requested at most once per process). Build: link -framework ApplicationServices
// -framework CoreFoundation (cmake/macos.cmake).
//
// Behavior that is UNVERIFIED without a Mac and hardware: that Ctrl+scroll zooms in a given app
// (macOS apps mostly use Cmd+scroll or pinch), the horizontal scroll direction, and the interplay
// with the "natural scrolling" system setting.
#ifndef __APPLE__
#error "mac_output.cpp is built on macOS only"
#endif

#include "mac_output.hpp"

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <atomic>
#include <mutex>

#include "mac_key_tables.hpp"

namespace spn {
namespace {

// The numeric constants of the pure header must equal the Apple definitions.
static_assert(mac::kFlagShift == kCGEventFlagMaskShift, "shift flag");
static_assert(mac::kFlagControl == kCGEventFlagMaskControl, "control flag");
static_assert(mac::kFlagAlt == kCGEventFlagMaskAlternate, "option flag");
static_assert(mac::kFlagCommand == kCGEventFlagMaskCommand, "command flag");
static_assert(mac::kCGMouseButtonLeft == kCGMouseButtonLeft, "left button");
static_assert(mac::kCGMouseButtonRight == kCGMouseButtonRight, "right button");
static_assert(mac::kCGMouseButtonCenter == kCGMouseButtonCenter, "center button");
static_assert(mac::kEvLeftMouseDown == kCGEventLeftMouseDown, "ev");
static_assert(mac::kEvLeftMouseUp == kCGEventLeftMouseUp, "ev");
static_assert(mac::kEvRightMouseDown == kCGEventRightMouseDown, "ev");
static_assert(mac::kEvRightMouseUp == kCGEventRightMouseUp, "ev");
static_assert(mac::kEvOtherMouseDown == kCGEventOtherMouseDown, "ev");
static_assert(mac::kEvOtherMouseUp == kCGEventOtherMouseUp, "ev");

std::atomic<bool> g_promptedForAccessibility{false};

// true if the process may post events. Asks the system to show its permission prompt only on the
// first call of the process.
bool accessibilityTrusted() {
    if (AXIsProcessTrusted()) return true;
    if (g_promptedForAccessibility.exchange(true)) return false;
    const void *keys[] = {kAXTrustedCheckOptionPrompt};
    const void *values[] = {kCFBooleanTrue};
    CFDictionaryRef opts = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    const bool ok = AXIsProcessTrustedWithOptions(opts);
    if (opts) CFRelease(opts);
    return ok;
}

class MacOutput : public Output {
public:
    MacOutput() { macReleaseGuard().add(this); }
    ~MacOutput() override {
        macReleaseGuard().remove(this);
        if (src_) {
            doReleaseAll();
            CFRelease(src_);
        }
    }

    const char *name() const override { return "cgevent"; }

    bool init(std::string *err) override {
        std::lock_guard<std::mutex> lk(m_);
        if (src_) return true;
        src_ = CGEventSourceCreate(kCGEventSourceStateHIDSystemState);
        if (!src_) {
            if (err) *err = "CGEventSourceCreate failed";
            return false;
        }
        return true;
    }

    // The public methods are serialized: releaseAll() is also called from the sleep watcher and the
    // exit handlers (other threads).
    void wheelV(int n) override {
        std::lock_guard<std::mutex> lk(m_);
        scroll(mac::scrollWheelValue(n), 0);
    }
    void wheelH(int n) override {
        std::lock_guard<std::mutex> lk(m_);
        scroll(0, mac::scrollWheelValue(n));
    }

    void setKey(Key k, bool down) override {
        std::lock_guard<std::mutex> lk(m_);
        if (!src_) return;
        const size_t i = static_cast<size_t>(k);
        if (down == keysDown_[i]) return;  // idempotent
        keysDown_[i] = down;
        // A modifier key event carries the flags as they are after the change, like hardware.
        recomputeHeldFlags();
        postKey(k, down);
    }

    void tapKey(Key k) override {
        std::lock_guard<std::mutex> lk(m_);
        if (!src_) return;
        // A key already held by a channel is released, tapped and pressed again (like the X11 and
        // uinput backends), so keysDown_ stays consistent with the ModeRunner's view.
        const bool wasDown = keysDown_[static_cast<size_t>(k)];
        if (wasDown) postKey(k, false);
        postKey(k, true);
        postKey(k, false);
        if (wasDown) postKey(k, true);
    }

    void mouseButton(int button, bool down) override {
        std::lock_guard<std::mutex> lk(m_);
        mac::MouseButtonSpec spec{};
        if (!src_ || !mac::mouseButtonSpec(button, &spec)) return;
        if (down == buttonsDown_[static_cast<size_t>(button)]) return;
        buttonsDown_[static_cast<size_t>(button)] = down;
        postMouse(spec, down);
    }

    void releaseAll() override {
        std::lock_guard<std::mutex> lk(m_);
        doReleaseAll();
    }

    void forceReleaseAll() override {
        std::lock_guard<std::mutex> lk(m_);
        if (!src_) return;
        // Independent of our own state: a crashed earlier process may have left keys down.
        heldFlags_ = 0;
        for (size_t i = 0; i < kKeyCount; ++i) postKeycode(mac::keyToKeycode(static_cast<Key>(i)), false, 0);
        for (uint16_t vk : mac::kExtraModifierKeycodes) postKeycode(vk, false, 0);
        for (int b = 1; b <= 3; ++b) {
            mac::MouseButtonSpec spec{};
            if (mac::mouseButtonSpec(b, &spec)) postMouse(spec, false);
        }
        doReleaseAll();
    }

    void flush() override {}

private:
    void doReleaseAll() {
        if (!src_) return;
        // Non-modifier keys first, modifiers last, so releases happen while the modifiers still
        // apply to the key-up events.
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i = 0; i < kKeyCount; ++i) {
                if (!keysDown_[i]) continue;
                const Key k = static_cast<Key>(i);
                const bool isMod = mac::keyModifierFlag(k) != 0;
                if (isMod != (pass == 1)) continue;
                keysDown_[i] = false;
                recomputeHeldFlags();
                postKey(k, false);
            }
        }
        for (int b = 1; b <= 3; ++b) {
            if (!buttonsDown_[static_cast<size_t>(b)]) continue;
            buttonsDown_[static_cast<size_t>(b)] = false;
            mac::MouseButtonSpec spec{};
            if (mac::mouseButtonSpec(b, &spec)) postMouse(spec, false);
        }
        heldFlags_ = 0;
    }

    void recomputeHeldFlags() {
        uint64_t f = 0;
        for (size_t i = 0; i < kKeyCount; ++i)
            if (keysDown_[i]) f |= mac::keyModifierFlag(static_cast<Key>(i));
        heldFlags_ = f;
    }

    // Flags every posted event carries: the held modifiers (synthetic events do not inherit the
    // physical modifier state) and, for the main-block plus, Shift.
    uint64_t flagsFor(Key k) const { return heldFlags_ | (mac::keyNeedsShift(k) ? mac::kFlagShift : 0); }

    void postKey(Key k, bool down) { postKeycode(mac::keyToKeycode(k), down, flagsFor(k)); }

    void postKeycode(uint16_t vk, bool down, uint64_t flags) {
        if (!src_ || vk == 0xFFFF) return;
        CGEventRef e = CGEventCreateKeyboardEvent(src_, static_cast<CGKeyCode>(vk), down);
        if (!e) return;
        CGEventSetFlags(e, static_cast<CGEventFlags>(flags));
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
    }

    static CGPoint cursorPosition() {
        CGPoint p = CGPointMake(0, 0);
        CGEventRef e = CGEventCreate(nullptr);
        if (e) {
            p = CGEventGetLocation(e);
            CFRelease(e);
        }
        return p;
    }

    void postMouse(const mac::MouseButtonSpec &spec, bool down) {
        CGEventRef e = CGEventCreateMouseEvent(src_, static_cast<CGEventType>(down ? spec.downType : spec.upType),
                                               cursorPosition(), static_cast<CGMouseButton>(spec.cgButton));
        if (!e) return;
        CGEventSetIntegerValueField(e, kCGMouseEventClickState, 1);
        CGEventSetFlags(e, static_cast<CGEventFlags>(heldFlags_));
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
    }

    void scroll(int32_t vertical, int32_t horizontal) {
        if (!src_ || (vertical == 0 && horizontal == 0)) return;
        CGEventRef e = CGEventCreateScrollWheelEvent(src_, kCGScrollEventUnitLine, 2, vertical, horizontal);
        if (!e) return;
        CGEventSetFlags(e, static_cast<CGEventFlags>(heldFlags_));
        CGEventPost(kCGHIDEventTap, e);
        CFRelease(e);
    }

    std::mutex m_;
    CGEventSourceRef src_ = nullptr;
    std::array<bool, kKeyCount> keysDown_{};
    std::array<bool, 4> buttonsDown_{};  // index 1..3
    uint64_t heldFlags_ = 0;
};

}  // namespace

mac::ReleaseGuard &macReleaseGuard() {
    static mac::ReleaseGuard *g = new mac::ReleaseGuard;  // never destroyed
    return *g;
}

std::unique_ptr<Output> makeMacOutput(std::string *log) {
    if (!accessibilityTrusted() && log)
        *log = "Accessibility permission missing: grant it in System Settings > Privacy & Security > "
               "Accessibility (events are not delivered until then)";
    return std::make_unique<MacOutput>();
}

}  // namespace spn
