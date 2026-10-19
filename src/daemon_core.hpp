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
#include <string>

#include "common.hpp"
#include "config.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "output.hpp"
#include "platform/platform.hpp"

namespace spn {

int64_t steadyNowNs();

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
    // Optional: true while the tray pauses the output (default: never).
    std::function<bool()> suspendRequested;
    // Optional status file hooks (body = "key=value" lines without "updated", see common.hpp).
    std::function<void(const std::string &body, int64_t nowNs)> writeStatus;
    std::function<void()> removeStatus;
    // Optional: number of attached 3Dconnexion devices, -1 if unknown (default: the number of
    // devices the input source reports).
    std::function<int()> countDevices;
    // Optional monotonic clock in ns (default: steady_clock); tests inject a fake.
    std::function<int64_t()> clock;
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
    Settings settings_;
    std::string match_desc_, backend_cfg_, device_, wm_class_;
    std::string mode_override_;  // runtime mode switch by a button; cleared on reload
    bool mode_applied_ = false;  // selectMode activated a mode at least once
    std::string warned_mode_;  // unknown mode id already warned about
    bool warn_buttons_ = false;

    // Loop state (formerly locals of run())
    int64_t next_win_poll_ = 0, next_cfg_poll_ = 0, next_debug_ = 0, next_susp_poll_ = 0;
    int64_t next_devcount_ = 0;
    bool suspended_ = false, prev_enabled_ = true, prev_idle_ = false;
    uint64_t seen_generation_ = 0;
    int devices_seen_ = 0;
    std::string hint_key_;  // multi-device hint: once per change
    std::chrono::steady_clock::duration tick_period_;

    DeviceReader reader_;
    std::unique_ptr<Output> output_;
    std::unique_ptr<ModeRunner> runner_;

    static FileStamp statFile(const std::string &path);
    void loadConfig();
    void loadModeSet();
    bool selectMode(Settings &next, bool force);
    void applyProfile(bool modes_changed);
    void checkButtons();
    void switchMode(const std::string &target);
};

}  // namespace spn
