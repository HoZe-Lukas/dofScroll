// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Platform-independent daemon core: reader thread, ticker, profile resolution, config/mode
// reload and the ModeRunner. It talks to the OS only through the platform interfaces
// (InputSource, Output, ActiveWindowProvider, ReloadListener, PlatformPaths) and a few
// callbacks, so it runs unchanged on every OS and can be driven by fakes in unit tests.
//
// Threads: a reader thread blocks in InputSource::wait() and only stores the latest state
// (lock-free atomics); the main thread ticks at Settings.tick_hz, evaluates and sends evenly
// distributed steps (instead of bursts per event). Config/profiles belong to the main
// thread alone.
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "common.hpp"
#include "live_feed/live_feed_protocol.hpp"
#include "config.hpp"
#include "profile_files.hpp"
#include "mode_runner.hpp"
#include "button_modes.hpp"
#include "modes.hpp"
#include "output.hpp"
#include "output_mux.hpp"
#include "platform/input_diag.hpp"
#include "platform/platform.hpp"
#include "platform/session_monitor.hpp"

namespace spn {

int64_t steadyNowNs();

// Safety net against stuck keys (see docs/troubleshooting.md, "A key or scrolling gets stuck"):
// if no axis has been outside its dead zone and no button has been held for this long, the core
// releases everything the output holds (keys, mouse buttons) and clears the output's own
// bookkeeping. It never fires during a held button (click-and-hold, a key bound to a button)
// or while an axis is deflected, so normal behavior is unchanged. A held button is deliberately
// not bounded: there is no way to tell it from a legitimate hold; use "Release all keys" in the
// tray or `dofscroll --release-keys` for that case.
constexpr int64_t SAFETY_QUIET_NS = 250'000'000;  // 250 ms
// The "safety release" log line is written at most once per this interval.
constexpr int64_t SAFETY_LOG_INTERVAL_NS = 10'000'000'000;  // 10 s

// Everything the core needs from the outside. Pointers must outlive the core.
struct DaemonDeps {
    InputSource *input = nullptr;
    ActiveWindowProvider *activeWindow = nullptr;
    ReloadListener *reload = nullptr;
    PlatformPaths paths;
    // Creates the output for the configured BACKEND (e.g. spn::makeOutput on Linux).
    std::function<std::unique_ptr<Output>(const std::string &backend, std::string *log)> makeOutput;
    // Optional overrides. Defaults: <paths.configDir>/dofscroll.conf and <paths.configDir>/modes.
    // The config path is re-evaluated about once a second (legacy-location migration).
    std::function<std::string()> configPath;
    std::function<std::string()> modesDir;
    std::function<std::string()> buttonModesDir;  // default: <paths.configDir>/buttons
    // Optional: true while the tray pauses the output (default: never).
    std::function<bool()> suspendRequested;
    // Optional: tells whether this daemon's own login session is the active one (see
    // platform/session_monitor.hpp). Polled about once per second; while it reports "not active"
    // the core behaves like suspended. Default: always active. Must outlive the core.
    SessionMonitor *sessionMonitor = nullptr;
    // Optional status file hooks (body = "key=value" lines without "updated", see common.hpp).
    std::function<void(const std::string &body, int64_t nowNs)> writeStatus;
    std::function<void()> removeStatus;
    // Optional: number of attached 3Dconnexion devices, -1 if unknown (default: the number of
    // devices the input source reports).
    std::function<int()> countDevices;
    // Optional monotonic clock in ns (default: steady_clock); tests inject a fake.
    std::function<int64_t()> clock;
    // Optional live feed sink (see src/live_feed/): called once per tick with the raw axes, the
    // buttons and the channel outputs; the publisher rate-limits and never blocks.
    std::function<void(const LiveSample &, int64_t nowNs)> liveFeed;
    bool debug = false;
};

// Reader side: SpaceMouse state, lock-free towards the main thread.
class DeviceReader {
public:
    // State of one device (events carry InputEvent::device). A merged source (spacenavd) only ever
    // fills the slot of device 0; HID sources one slot per device. The reader thread writes, the main
    // thread reads; the slots are cleared when the device list changes.
    static constexpr int kMaxSlots = 4;
    struct Slot {
        std::atomic<int> id{-1};  // device id, -1 = free
        std::array<std::atomic<int>, AX_COUNT> axes{};
        std::atomic<uint32_t> button_mask{0};
        std::atomic<int64_t> last_motion_ns{0};
    };
    // The slot of a device, nullptr as long as it has not sent anything.
    const Slot *slotFor(int device_id) const {
        for (const Slot &s : slots_)
            if (s.id.load(std::memory_order_acquire) == device_id) return &s;
        return nullptr;
    }

    // Aggregate over all devices (the last event of any device): kept for the single-device view.
    std::array<std::atomic<int>, AX_COUNT> axes{};
    // Bitmask of pressed buttons (bit n = button index n, same numbering as ButtonBinding::button).
    std::atomic<uint32_t> button_mask{0};
    // Timestamp (clock, ns) of the last motion event -> stale watchdog.
    std::atomic<int64_t> last_motion_ns{0};
    // Button count of the current device (for warnings in the main thread).
    std::atomic<int> dev_buttons{0};
    // Incremented after every (re)connect or device change, after the name was set under the
    // mutex. The main thread only compares this number per tick and fetches the name on change.
    std::atomic<uint64_t> dev_generation{0};
    // Motion and button events received since the last (re)connect, and the clock (ns) of that
    // connect: the daemon core reports a device that never sent anything (platform/input_diag.hpp).
    std::atomic<uint64_t> events_since_connect{0};
    std::atomic<int64_t> connected_ns{0};
    // The source refuses to open a matching device (another program holds it).
    bool accessBlocked() const { return src_->accessBlocked(); }
    const char *sourceName() const { return src_->name(); }

    DeviceReader(InputSource *src, std::function<int64_t()> clock, bool debug);

    std::string deviceName() const;
    std::vector<DeviceInfo> deviceList() const;  // snapshot taken at the last publishDeviceInfo()

    // Starts the detached reader thread (connect with backoff, then event loop). Detached:
    // the process teardown cleans it up, the owner must never be destroyed before exit.
    void start();

    // Single steps, also used by the thread (and directly by unit tests, without a thread):
    bool connect();                    // one open() attempt; on success publishes the device info
    void handle(const InputEvent &e);  // applies one event to the shared state
    void disconnect();                 // close() and clear the state

private:
    InputSource *src_;
    std::function<int64_t()> clock_;
    bool debug_;
    mutable std::mutex name_mutex_;
    std::string dev_name_;
    std::vector<DeviceInfo> dev_list_;
    int delay_ms_ = 500;
    std::string last_open_err_;

    std::array<Slot, kMaxSlots> slots_;
    Slot *claimSlot(int device_id);
    void publishDeviceInfo();
    void backoffSleep(const char *why);
    void clearState();
    void loop();
};

// Everything that belongs to one physical device: its profile, joystick mode (button switches, hold,
// toggle), runner with the DSP state and the safety net. One lane per attached SpaceMouse; a merged
// source (spacenavd) gives one lane. The output is shared and counts what each lane holds (OutputMux).
struct Lane {
    int dev_id = 0;       // DeviceInfo::id this lane reads (InputEvent::device)
    bool bound = false;   // a device of the list is assigned (the first lane exists before any device)
    bool dirty = false;   // device name changed: resolve the profile again
    std::string device;   // name matched by [device:*] profiles
    Settings settings;
    std::string match_desc;
    std::string toggled_mode;   // the part of mode_override that came from toggle:mode:<id>
    std::string mode_override;  // runtime mode switch by a button; cleared on reload
    bool hold_active = false;   // a hold:mode: button is held and its mode is the override
    std::string hold_id, hold_prev_override, hold_prev_toggled;
    bool mode_applied = false;  // selectMode activated a mode at least once
    bool warn_buttons = false;
    bool prev_enabled = true, prev_idle = false;
    int64_t quiet_since = 0, last_safety_log = 0;
    bool safety_done = true;  // the net already ran for the current quiet period
    bool safety_logged = false;
    Output *out = nullptr;    // this lane's view of the shared output
    std::unique_ptr<ModeRunner> runner;
};

class DaemonCore {
public:
    explicit DaemonCore(DaemonDeps deps);

    // Loads config and modes, creates the output, activates the first mode, initializes the
    // active-window provider and the reload listener. false: no output possible.
    bool initialize();

    // Starts the reader thread and ticks until requestQuit().
    void run();
    // Async-signal-safe (lock-free atomic store).
    void requestQuit() { quit_.store(true, std::memory_order_relaxed); }

    // Single phases of the main loop (run() calls them; unit tests call them with their own
    // time base). updateProfile: reload, config/mode polling, device change, active window,
    // profile resolution. tickOnce: the rest of the tick (inputs, suspend, status, runner).
    void updateProfile(int64_t now_ns);
    void tickOnce(int64_t now_ns, double dt);

    DeviceReader &reader() { return reader_; }
    // The first lane: the only one with a merged source or one device.
    const Settings &settings() const { return lanes_.front()->settings; }
    const std::string &matchDesc() const { return lanes_.front()->match_desc; }
    const std::string &deviceName() const { return lanes_.front()->device; }
    const ModeRunner &runner() const { return *lanes_.front()->runner; }
    // One lane per attached device (at least one).
    // Per-file results of the last profile load (profile_files.hpp), for tests and diagnostics.
    const std::vector<FileDiag> &profileDiagnostics() const { return profile_diag_; }
    size_t laneCount() const { return lanes_.size(); }
    const Lane &lane(size_t i) const { return *lanes_[i]; }

private:
    DaemonDeps d_;
    std::function<int64_t()> clock_;
    std::atomic<bool> quit_{false};
    std::string config_path_;
    StampDebounce stamp_;  // profile tree (dofscroll.conf, devices/, apps/), debounced by one poll
    std::vector<FileDiag> profile_diag_;
    ProfileSet profiles_;
    ModeSet modes_;
    std::string modes_dir_, modes_stamp_;
    ButtonModeSet button_modes_;
    std::string button_modes_dir_, button_modes_stamp_;
    std::set<std::string> warned_button_modes_;  // unknown button mode ids already warned about
    std::string backend_cfg_, wm_class_;
    std::vector<std::unique_ptr<Lane>> lanes_;
    std::unique_ptr<OutputMux> mux_;
    std::set<std::string> warned_cycle_;  // unknown ids of a mode cycle list already warned about (per load)
    std::string warned_mode_;  // unknown mode id already warned about

    // Loop state (formerly locals of run())
    int64_t next_win_poll_ = 0, next_cfg_poll_ = 0, next_debug_ = 0, next_susp_poll_ = 0;
    int64_t next_devcount_ = 0;
    int64_t next_session_poll_ = 0;
    bool session_active_ = true;
    bool suspended_ = false;
    uint64_t seen_generation_ = 0;
    int devices_seen_ = 0;
    InputIssue input_issue_ = InputIssue::Clear;
    std::string hint_key_;  // multi-device hint: once per change
    std::chrono::steady_clock::duration tick_period_;

    LiveSample live_;
    DeviceReader reader_;
    std::unique_ptr<Output> output_;

    void loadConfig();
    void loadModeSet();
    bool selectMode(Lane &l, Settings &next, bool force);
    void applyProfile(Lane &l, bool modes_changed);
    void loadButtonModeSet();
    void endHold(Lane &l, bool restore);
    void checkButtons(Lane &l);
    void switchMode(Lane &l, const std::string &target);
    void updateTickPeriod();
    // Binds lanes to the devices of the list (new lane per new device, lanes of removed devices end).
    void syncLanes();
    std::unique_ptr<Lane> makeLane(int dev_id);
    // The devices that get a lane: one per model, at most DeviceReader::kMaxSlots.
    std::vector<DeviceInfo> usableDevices(const std::vector<DeviceInfo> &all);
    std::set<int> warned_ignored_;  // ids of ignored devices already logged
    void releaseLane(Lane &l, bool clearToggles);
    // Status "toggle=": what button toggles hold at the moment, e.g. "nav" or "nav mode:timeline"; empty if none.
    std::string toggleState(const Lane &l) const;
    void safetyNet(Lane &l, int64_t now_ns, uint32_t buttons);
};

}  // namespace spn
