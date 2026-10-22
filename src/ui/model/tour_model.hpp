// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The guided tour of the Motions and buttons tab (issue #143, toolkit-free): four steps, each waits for the real
// action when a device is attached and offers "Click it instead" when none is (or the daemon is not running):
//   1. move the cap, the matching motion is selected      2. pick an action for it
//   3. press a device button, it is selected              4. try the joystick mode
// It is shown the first time the tab opens and from "Show me how" at any time, can be skipped, and is not shown
// again after it ran to its end or was dismissed (UiProgress::motionTourSeen, ui_progress.hpp).
//
//   TourModel tour(deviceAvailable);
//   tour.on(TourEvent::MotionDetected);   // by moving the cap
//   tour.text(strings); tour.step();
#pragma once
#include <string>

#include "ui/model/strings.hpp"

namespace spn::ui {

enum class TourStep { Move, Pick, Press, Try, Done };
enum class TourEvent { MotionDetected, MotionClicked, ActionAssigned, ButtonDetected, ButtonClicked, Tried };

class TourModel {
public:
    explicit TourModel(bool deviceAvailable = false) : device_(deviceAvailable) {}

    TourStep step() const { return step_; }
    int number() const { return step_ == TourStep::Done ? 4 : int(step_) + 1; }  // 1..4
    static constexpr int kSteps = 4;
    bool finished() const { return step_ == TourStep::Done; }
    bool dismissed() const { return dismissed_; }
    bool active() const { return !finished() && !dismissed_; }

    // The daemon or device came or went while the tour runs: the step then waits for the movement or for a click.
    void setDevice(bool available) { device_ = available; }
    bool device() const { return device_; }
    // The step waits for the real movement or button now (a device is there and the user did not choose to click).
    bool waitsForDevice() const {
        return active() && device_ && !clickChosen_ && (step_ == TourStep::Move || step_ == TourStep::Press);
    }
    // "Click it instead" for the current step.
    void chooseClick() { clickChosen_ = true; }
    bool canClickInstead() const { return waitsForDevice(); }

    // true when the event moved the tour on.
    bool on(TourEvent e) {
        if (!active()) return false;
        const bool realOnly = waitsForDevice();
        bool hit = false;
        switch (step_) {
            case TourStep::Move: hit = e == TourEvent::MotionDetected || (!realOnly && e == TourEvent::MotionClicked); break;
            case TourStep::Pick: hit = e == TourEvent::ActionAssigned; break;
            case TourStep::Press: hit = e == TourEvent::ButtonDetected || (!realOnly && e == TourEvent::ButtonClicked); break;
            case TourStep::Try: hit = e == TourEvent::Tried; break;
            case TourStep::Done: break;
        }
        if (hit) advance();
        return hit;
    }
    // Skips the current step (the "Next" button when nothing can be done: no daemon to try the mode).
    void next() {
        if (active()) advance();
    }
    void dismiss() { dismissed_ = true; }
    void restart() {
        step_ = TourStep::Move;
        dismissed_ = false;
        clickChosen_ = false;
    }

    // The instruction of the current step.
    std::string text(const StringProvider &s) const {
        switch (step_) {
            case TourStep::Move:
                return waitsForDevice() ? s.tr("Move the cap: the motion you move is selected.")
                                        : s.tr("Click a motion in the picture to select it.");
            case TourStep::Pick: return s.tr("Pick what the motion does in the box on the right.");
            case TourStep::Press:
                return waitsForDevice() ? s.tr("Press a button on the device: it is selected.")
                                        : s.tr("Click a button in the picture to select it.");
            case TourStep::Try: return s.tr("Press \"Try this joystick mode\" and move the cap in a program.");
            case TourStep::Done: return s.tr("That is all. \"Show me how\" runs this tour again.");
        }
        return std::string();
    }

private:
    void advance() {
        clickChosen_ = false;
        switch (step_) {
            case TourStep::Move: step_ = TourStep::Pick; break;
            case TourStep::Pick: step_ = TourStep::Press; break;
            case TourStep::Press: step_ = TourStep::Try; break;
            case TourStep::Try: step_ = TourStep::Done; break;
            case TourStep::Done: break;
        }
    }
    TourStep step_ = TourStep::Move;
    bool device_ = false, dismissed_ = false, clickChosen_ = false;
};

}  // namespace spn::ui
