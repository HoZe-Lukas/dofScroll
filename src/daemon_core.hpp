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

#include "common.hpp"
#include "live_feed/live_feed_protocol.hpp"
#include "config.hpp"
#include "mode_runner.hpp"
#include "button_modes.hpp"
#include "modes.hpp"
#include "output.hpp"
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

    void publishDeviceInfo();
    void backoffSleep(const char *why);
    void clearState();
    void loop();
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
    const Settings &settings() const { return settings_; }
    const std::string &matchDesc() const { return match_desc_; }
    const std::string &deviceName() const { return device_; }
    const ModeRunner &runner() const { return *runner_; }

private:
    DaemonDeps d_;
    std::function<int64_t()> clock_;
    std::atomic<bool> quit_{false};
    std::string config_path_;
    struct FileStamp {
        bool exists = false;
        int64_t mtime_ns = 0, size = 0;
        uint64_t ino = 0;
        bool operator!=(const FileStamp &o) const {
            return exists != o.exists || mtime_ns != o.mtime_ns || size != o.size || ino != o.ino;
        }
    } stamp_;
    ProfileSet profiles_;
    ModeSet modes_;
    std::string modes_dir_, modes_stamp_;
    ButtonModeSet button_modes_;
    std::string button_modes_dir_, button_modes_stamp_;
    std::set<std::string> warned_button_modes_;  // unknown button mode ids already warned about
    Settings settings_;
    std::string match_desc_, backend_cfg_, device_, wm_class_;
    std::string toggled_mode_;   // the part of mode_override_ that came from toggle:mode:<id>
    std::string mode_override_;  // runtime mode switch by a button; cleared on reload
    bool hold_active_ = false;   // a hold:mode: button is held and its mode is the override
    std::string hold_id_, hold_prev_override_, hold_prev_toggled_;  // the held mode, the state to restore
    bool mode_applied_ = false;  // selectMode activated a mode at least once
    std::string warned_mode_;  // unknown mode id already warned about
    bool warn_buttons_ = false;

    // Loop state (formerly locals of run())
    int64_t next_win_poll_ = 0, next_cfg_poll_ = 0, next_debug_ = 0, next_susp_poll_ = 0;
    int64_t next_devcount_ = 0;
    int64_t next_session_poll_ = 0;
    bool session_active_ = true;
    bool suspended_ = false, prev_enabled_ = true, prev_idle_ = false;
    uint64_t seen_generation_ = 0;
    int devices_seen_ = 0;
    int64_t quiet_since_ = 0, last_safety_log_ = 0;
    bool safety_done_ = true;  // the net already ran for the current quiet period
    bool safety_logged_ = false;
    std::string hint_key_;  // multi-device hint: once per change
    std::chrono::steady_clock::duration tick_period_;

    LiveSample live_;
    DeviceReader reader_;
    std::unique_ptr<Output> output_;
    std::unique_ptr<ModeRunner> runner_;

    static FileStamp statFile(const std::string &path);
    void loadConfig();
    void loadModeSet();
    bool selectMode(Settings &next, bool force);
    void applyProfile(bool modes_changed);
    void loadButtonModeSet();
    void endHold(bool restore);
    void checkButtons();
    void switchMode(const std::string &target);
    // Status "toggle=": what button toggles hold at the moment, e.g. "nav" or "nav mode:timeline"; empty if none.
    std::string toggleState() const;
    void safetyNet(int64_t now_ns, uint32_t buttons);
};

}  // namespace spn
