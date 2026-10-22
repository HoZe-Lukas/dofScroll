// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The first-run setup guide, without any toolkit. After installing, the first start is "open dofscroll
// from the menu"; this guide then shows a few steps with LIVE checks, so nobody has to type a command:
//
//   daemon      "dofscroll is running"   the daemon heartbeat (the interface starts the daemon by itself,
//                                        daemon_autostart.hpp)
//   device      "SpaceMouse found"       devices_seen of the status; on Linux, when the spacenavd service is
//                                        not active, the plain words and the command as a fallback only
//   autostart   "Start at login"         check box, on by default, applied through SetAutostart
//   macOS only  "Accessibility" and "Input Monitoring"   the permissions, a button opens the pane
//   tryit       "Try it"                 push the cap, the window under the pointer scrolls
//
// buildSetupGuide() turns the live inputs into the rows the view draws; every row says in words whether
// it is done (never by colour alone). The flow (shown at the first start, skipped, finished, shown
// again from the Overview) is SetupGuideFlow; what was done is kept in ui-progress.conf.
#pragma once
#include <string>
#include <vector>

#include "ui/model/action_runner.hpp"
#include "ui/model/restart_plan.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/strings.hpp"
#include "ui/model/ui_progress.hpp"

namespace spn::ui {

// macOS panes of System Settings (opened through ActionRunner::openSystemSettings).
enum class PermissionPane { Accessibility, InputMonitoring };

inline const char *permissionPaneUrl(PermissionPane p) {
    return p == PermissionPane::Accessibility
               ? "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility"
               : "x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent";
}

// Only these two addresses are ever handed to the system (no address is built from other text).
inline bool isSystemSettingsUrl(const std::string &url) {
    return url == permissionPaneUrl(PermissionPane::Accessibility) || url == permissionPaneUrl(PermissionPane::InputMonitoring);
}

// The one command shown when the SpaceMouse driver service of Linux is not running (fallback text).
inline const char *spacenavdFixCommand() { return "sudo systemctl enable --now spacenavd.service"; }

enum class StepState {
    Done,     // "Done"
    Todo,     // "To do"
    Problem,  // "Needs attention"
    Info      // plain information ("Try it")
};

enum class StepAction {
    None,
    StartDaemon,         // button "Start now"
    OpenAccessibility,   // button that opens the System Settings pane
    OpenInputMonitoring,
    Autostart            // the check box "Start at login"
};

struct SetupInputs {
    Os os = Os::Linux;
    bool daemonRunning = false;
    bool waitingForDaemon = false;   // inside the grace period: "not running yet" is expected
    bool daemonStartFailed = false;  // the interface tried to start it and there is still no heartbeat
    bool deviceSeen = false;
    std::string deviceName;          // from the status (may be empty)
    InputIssue inputIssue = InputIssue::Clear;
    ServiceState spacenavd = ServiceState::Unknown;  // Linux
    bool autostartKnown = false;     // the state of "Start at login" was read
    bool autostartOn = false;
    std::string autostartDetail;     // why it cannot be changed here (empty = it can)
    bool permissionsKnown = false;   // macOS: the daemon reported them
    bool missingAccessibility = false, missingInputMonitoring = false;
    std::string daemonPath;          // macOS: the file the permissions are given to
};

struct SetupStep {
    std::string id;       // "daemon", "device", "autostart", "accessibility", "input-monitoring", "tryit"
    std::string title;    // translated: "dofscroll is running"
    StepState state = StepState::Todo;
    std::string status;   // translated word(s) for the state: "Done", "To do", "Needs attention", ""
    std::string text;     // translated: what is the case now and what to do
    std::string command;  // copy-able command, only as a fallback; empty = none
    StepAction action = StepAction::None;
    std::string actionLabel;  // translated button text; empty = no button
};

inline std::string stateWord(StepState s, const StringProvider &t) {
    switch (s) {
        case StepState::Done: return t.tr("Done");
        case StepState::Todo: return t.tr("To do");
        case StepState::Problem: return t.tr("Needs attention");
        case StepState::Info: break;
    }
    return std::string();
}

inline SetupInputs setupInputsFrom(const StatusModel &status, Os os) {
    SetupInputs in;
    in.os = os;
    in.daemonRunning = status.daemonRunning();
    const DaemonStatus &d = status.status();
    in.deviceSeen = in.daemonRunning && (d.devices_seen > 0 || !d.device.empty());
    in.deviceName = d.device;
    in.inputIssue = in.daemonRunning ? d.input_issue : InputIssue::Clear;
    in.permissionsKnown = in.daemonRunning && d.permissions_known;
    in.missingAccessibility = d.missing_accessibility;
    in.missingInputMonitoring = d.missing_input_monitoring;
    return in;
}

inline std::vector<SetupStep> buildSetupGuide(const SetupInputs &in, const StringProvider &t) {
    std::vector<SetupStep> out;
    auto add = [&](SetupStep s) {
        s.status = stateWord(s.state, t);
        out.push_back(std::move(s));
    };

    // 1. The daemon.
    {
        SetupStep s;
        s.id = "daemon";
        s.title = t.tr("dofscroll is running");
        if (in.daemonRunning) {
            s.state = StepState::Done;
            s.text = t.tr("The background program (the daemon) is running.");
        } else if (in.waitingForDaemon) {
            s.state = StepState::Todo;
            s.text = t.tr("Starting the background program (the daemon)... This takes a few seconds.");
        } else {
            s.state = StepState::Problem;
            s.text = in.daemonStartFailed
                         ? t.tr("The background program (the daemon) could not be started. Try \"Start now\"; if it does not "
                                "help, see Troubleshooting in the documentation.")
                         : t.tr("The background program (the daemon) is not running.");
            s.action = StepAction::StartDaemon;
            s.actionLabel = t.tr("Start now");
        }
        add(std::move(s));
    }

    // 2. The SpaceMouse.
    {
        SetupStep s;
        s.id = "device";
        s.title = t.tr("SpaceMouse found");
        if (in.deviceSeen) {
            s.state = StepState::Done;
            s.text = in.deviceName.empty() ? t.tr("The SpaceMouse is connected.")
                                           : format(t.tr("Found: %1"), {in.deviceName});
            if (in.inputIssue == InputIssue::Blocked) {
                s.state = StepState::Problem;
                s.text += " " + t.tr("But another program, possibly the 3Dconnexion driver, holds it exclusively. See "
                                     "Troubleshooting in the documentation.");
            }
        } else if (!in.daemonRunning) {
            s.state = StepState::Todo;
            s.text = t.tr("Checked as soon as dofscroll is running.");
        } else if (in.os == Os::Linux && in.spacenavd == ServiceState::Inactive) {
            s.state = StepState::Problem;
            s.text = t.tr("The SpaceMouse driver service (spacenavd) is not running. It has to be switched on once, as "
                          "administrator. If your system does not offer a way to do it, run this command in a terminal:");
            s.command = spacenavdFixCommand();
        } else {
            s.state = StepState::Todo;
            s.text = in.os == Os::Linux
                         ? t.tr("No SpaceMouse found yet. Plug it in (or its wireless receiver) and move the cap.")
                         : t.tr("No SpaceMouse found yet. Plug it in and move the cap. If another program holds it, close "
                                "that program.");
        }
        add(std::move(s));
    }

    // 3. Start at login.
    {
        SetupStep s;
        s.id = "autostart";
        s.title = t.tr("Start at login");
        s.action = StepAction::Autostart;
        if (!in.autostartKnown) {
            s.state = StepState::Todo;
            s.text = t.tr("Checking...");
        } else if (!in.autostartDetail.empty() && !in.autostartOn) {
            s.state = StepState::Problem;
            s.text = format(t.tr("Cannot be switched on here: %1"), {in.autostartDetail});
        } else if (in.autostartOn) {
            s.state = StepState::Done;
            s.text = t.tr("dofscroll and its tray icon start when you log in. You can change this on the Settings page.");
        } else {
            s.state = StepState::Todo;
            s.text = t.tr("Start dofscroll when you log in, so the SpaceMouse works without opening anything.");
        }
        add(std::move(s));
    }

    // 4. macOS: the two permissions.
    if (in.os == Os::MacOS) {
        struct P {
            const char *id;
            bool missing;
            StepAction action;
        };
        const P perms[] = {{"input-monitoring", in.missingInputMonitoring, StepAction::OpenInputMonitoring},
                           {"accessibility", in.missingAccessibility, StepAction::OpenAccessibility}};
        for (const P &p : perms) {
            const bool input = std::string(p.id) == "input-monitoring";
            SetupStep s;
            s.id = p.id;
            s.title = input ? t.tr("Permission: Input Monitoring") : t.tr("Permission: Accessibility");
            s.action = p.action;
            s.actionLabel = input ? t.tr("Open Input Monitoring settings") : t.tr("Open Accessibility settings");
            if (!in.permissionsKnown) {
                s.state = StepState::Todo;
                s.text = in.daemonRunning
                             ? t.tr("Not reported by the daemon yet.")
                             : t.tr("Checked as soon as dofscroll is running.");
            } else if (!p.missing) {
                s.state = StepState::Done;
                s.text = t.tr("Granted.");
            } else {
                s.state = StepState::Problem;
                s.text = input ? t.tr("Missing: macOS does not let dofscroll read the SpaceMouse, so no events arrive. Switch "
                                      "on dofscroll in the list that opens.")
                               : t.tr("Missing: macOS does not accept the scroll, zoom and key events that dofscroll sends. "
                                      "Switch on dofscroll in the list that opens.");
                if (!in.daemonPath.empty())
                    s.text += " " + format(t.tr("If it is not in the list, add this file with the + button: %1"),
                                           {in.daemonPath});
            }
            add(std::move(s));
        }
    }

    // 5. Try it.
    {
        SetupStep s;
        s.id = "tryit";
        s.title = t.tr("Try it");
        s.state = StepState::Info;
        s.text = t.tr("Push the cap: the window under the mouse pointer scrolls. Twist it: the window zooms.");
        add(std::move(s));
    }
    return out;
}

// How many steps (without the information step) are done.
inline size_t setupStepsDone(const std::vector<SetupStep> &steps, size_t *total = nullptr) {
    size_t done = 0, all = 0;
    for (const SetupStep &s : steps) {
        if (s.state == StepState::Info) continue;
        ++all;
        done += s.state == StepState::Done;
    }
    if (total) *total = all;
    return done;
}

// The guide is shown by itself at the first start only: a new installation (no configuration file
// and nothing to migrate existed before the first start) that has not finished or skipped it.
inline bool setupGuideDue(const UiProgress &progress, bool freshInstall) { return freshInstall && !progress.setupGuideSeen; }

// Skip, finish and show again. `visible` is what the window shows now; the progress file keeps only
// "seen", so a skipped or finished guide does not come back by itself, and "Show setup guide" on the
// Overview opens it again at any time.
class SetupGuideFlow {
public:
    explicit SetupGuideFlow(std::string progressPath) : path_(std::move(progressPath)) {}

    // Called once at the start: whether the guide is shown now. A start that is not the first marks the
    // guide as seen (an existing installation never gets it by itself).
    bool start(bool freshInstall) {
        const UiProgress p = loadUiProgress(path_);
        visible_ = setupGuideDue(p, freshInstall);
        if (!freshInstall && !p.setupGuideSeen) markSetupGuideSeen(path_, nullptr);
        return visible_;
    }
    bool visible() const { return visible_; }
    void show() { visible_ = true; }  // "Show setup guide"
    bool skip(std::string *err) { return close(err); }
    bool finish(std::string *err) { return close(err); }
    // Closing the dialog with the window's own close button counts as skipping.
    bool closed(std::string *err) { return close(err); }

private:
    bool close(std::string *err) {
        visible_ = false;
        return markSetupGuideSeen(path_, err);
    }
    std::string path_;
    bool visible_ = false;
};

}  // namespace spn::ui
