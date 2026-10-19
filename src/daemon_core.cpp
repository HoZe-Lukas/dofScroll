// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Platform-independent daemon core (see daemon_core.hpp).
#include "daemon_core.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <thread>

#include "log_sanitize.hpp"
#include "mode_request.hpp"
#include "platform/fs.hpp"
#include "profile_match.hpp"

namespace spn {

namespace {

const char *const LOG = "[dofscroll] ";

// Upper bound for dt: after a suspend/stall a huge accumulator would otherwise be
// discharged as one burst.
constexpr double MAX_DT = 0.05;
// "Fresh enough": well above the nominal ~8 ms HID poll period, but short enough that
// a connection loss during a deflection ends practically at once (instead of scrolling
// on forever in the last direction).
constexpr int64_t STALE_NS = 250'000'000;  // 250 ms
// Active-window query (~10x/s, not every tick).
constexpr int64_t ACTIVEWIN_POLL_NS = 100'000'000;
// mtime check of the config file and modes directory for auto-reload.
constexpr int64_t CONFIG_POLL_NS = 1'000'000'000;
// Check the tray's heartbeat (pause output) ~20x/s.
constexpr int64_t SUSPEND_POLL_NS = 50'000'000;
constexpr int64_t SESSION_POLL_NS = 1'000'000'000;  // session activity: about once per second
// --debug output ~10x/s.
constexpr int64_t DEBUG_PERIOD_NS = 100'000'000;
// Recount attached 3Dconnexion devices (additionally on every (re)connect).
constexpr int64_t DEVCOUNT_POLL_NS = 10'000'000'000;

// Reader backoff after a failed/lost connection.
constexpr int MIN_DELAY_MS = 500;
constexpr int MAX_DELAY_MS = 5000;
constexpr int64_t HEALTHY_NS = 10'000'000'000;  // 10 s
// Poll granularity of InputSource::wait() in the reader thread.
constexpr int WAIT_TIMEOUT_MS = 250;

std::string describeMatches(const ProfileSet &ps, const std::string &device,
                            const std::string &wm_class) {
    std::string d = "global";
    for (const Section &s : ps.sections) {
        if (s.kind == Section::DEVICE && patternMatches(s.pattern, device))
            d += " + device:" + s.pattern;
    }
    for (const Section &s : ps.sections) {
        if (s.kind == Section::APP && patternMatches(s.pattern, wm_class))
            d += " + app:" + s.pattern;
    }
    return d;
}

std::chrono::steady_clock::duration periodFor(int hz) {
    hz = std::clamp(hz, 30, 1000);
    return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(1.0 / hz));
}

}  // namespace

int64_t steadyNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
// DeviceReader
// ---------------------------------------------------------------------------

DeviceReader::DeviceReader(InputSource *src, std::function<int64_t()> clock, bool debug)
    : src_(src), clock_(std::move(clock)), debug_(debug) {
    for (auto &a : axes) a.store(0, std::memory_order_relaxed);
}

std::string DeviceReader::deviceName() const {
    std::lock_guard<std::mutex> lk(name_mutex_);
    return dev_name_;
}

void DeviceReader::start() {
    std::thread t(&DeviceReader::loop, this);
    t.detach();
}

// Logs the device(s) and passes the name on to the main thread (profile [device:*]). After
// hotplug the device can change -> on every (re)connect and device-list change. A merged
// source (spacenavd) reports one device; with several, the first one names the profile.
void DeviceReader::publishDeviceInfo() {
    const std::vector<DeviceInfo> devs = src_->devices();
    std::string name;
    int buttons = 0;
    for (const DeviceInfo &di : devs) {
        std::cerr << LOG << "Device: " << (di.name.empty() ? std::string("?") : sanitizeForLog(di.name)) << " ("
                  << di.reportedButtons << " buttons, " << di.axes << " axes)" << std::endl;
        buttons = std::max(buttons, di.reportedButtons);
    }
    if (!devs.empty()) name = devs.front().name;
    dev_buttons.store(buttons, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(name_mutex_);
        dev_name_ = name;
    }
    dev_generation.fetch_add(1, std::memory_order_release);
}

void DeviceReader::backoffSleep(const char *why) {
    std::cerr << LOG << why << ", next attempt in " << (delay_ms_ / 1000.0) << "s" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
    delay_ms_ = std::min(delay_ms_ * 2, MAX_DELAY_MS);
}

bool DeviceReader::connect() {
    std::string err;
    if (!src_->open(&err)) return false;
    std::cerr << LOG << "connected to " << src_->name() << std::endl;
    publishDeviceInfo();
    return true;
}

void DeviceReader::clearState() {
    for (auto &a : axes) a.store(0, std::memory_order_relaxed);
    button_mask.store(0, std::memory_order_relaxed);
}

void DeviceReader::disconnect() {
    src_->close();
    clearState();
}

// Only stores the latest state. Nothing is sent here, so irregular event timing does not
// affect the flow.
void DeviceReader::handle(const InputEvent &e) {
    switch (e.type) {
    case InputEventType::Motion:
        for (int i = 0; i < AX_COUNT; ++i) axes[i].store(e.axes[i], std::memory_order_relaxed);
        last_motion_ns.store(clock_(), std::memory_order_release);
        break;
    case InputEventType::Button:
        if (e.down && debug_) std::cerr << LOG << "Button " << e.button << " pressed" << std::endl;
        if (e.button >= 0 && e.button < 32) {
            uint32_t bit = 1u << e.button;
            if (e.down) button_mask.fetch_or(bit, std::memory_order_relaxed);
            else button_mask.fetch_and(~bit, std::memory_order_relaxed);
        }
        break;
    case InputEventType::DevicesChanged:
        // Device unplugged: release events never arrive then - held buttons/axes would
        // hang (a click would stay pressed). Deliberately no heuristic timeout: a held
        // button without motion is legitimate (click-and-hold).
        std::cerr << LOG << "Device list changed" << std::endl;
        clearState();
        publishDeviceInfo();
        break;
    }
}

// Connects with exponential backoff up to 5 s. This way the service starts cleanly even if
// the input source (spacenavd) is not (yet) running, and recovers by itself after a restart.
void DeviceReader::loop() {
    while (true) {
        while (!connect()) {
            std::string why = std::string(src_->name()) + " not reachable";
            backoffSleep(why.c_str());
        }

        const int64_t connected_ns = clock_();
        bool got_event = false;
        while (true) {
            InputEvent ev;
            WaitResult r = src_->wait(&ev, WAIT_TIMEOUT_MS);
            if (r == WaitResult::Closed) break;
            if (r == WaitResult::Timeout) continue;
            got_event = true;
            handle(ev);
        }

        disconnect();
        if (got_event || clock_() - connected_ns >= HEALTHY_NS) delay_ms_ = MIN_DELAY_MS;
        std::string why = "Connection to " + std::string(src_->name()) + " lost";
        backoffSleep(why.c_str());
    }
}

// ---------------------------------------------------------------------------
// DaemonCore
// ---------------------------------------------------------------------------

DaemonCore::DaemonCore(DaemonDeps deps)
    : d_(std::move(deps)),
      clock_(d_.clock ? d_.clock : std::function<int64_t()>(steadyNowNs)),
      tick_period_(periodFor(250)),
      reader_(d_.input, clock_, d_.debug) {}

DaemonCore::FileStamp DaemonCore::statFile(const std::string &path) {
    FileStamp f;
    fs::FileInfo fi;
    if (fs::statPath(path, &fi)) {
        f.exists = true;
        f.mtime_ns = fi.mtimeNs;
        f.size = fi.size;
        f.ino = fi.fileId;
    }
    return f;
}

void DaemonCore::loadConfig() {
    mode_override_.clear();  // a reload drops the button-selected mode
    // Take the stamp before reading: if the file changes while reading, it is
    // noticed at the next check.
    stamp_ = statFile(config_path_);
    ProfileSet ps;
    std::vector<std::string> warnings;
    if (!loadProfiles(config_path_, &ps, &warnings)) {
        std::cerr << LOG << "WARNING: config " << config_path_
                  << " not readable, keeping previous settings" << std::endl;
    } else {
        profiles_ = std::move(ps);
    }
    for (const std::string &w : warnings)
        std::cerr << LOG << "Config: " << sanitizeForLog(w, 512) << std::endl;
}

void DaemonCore::loadModeSet() {
    mode_override_.clear();
    modes_stamp_ = modesStamp(modes_dir_);  // before reading, like the config stamp
    std::vector<std::string> warnings;
    modes_ = loadModes(modes_dir_, &warnings);
    for (const std::string &w : warnings)
        std::cerr << LOG << "Modes: " << sanitizeForLog(w, 512) << std::endl;
}

bool DaemonCore::initialize() {
    config_path_ = d_.configPath ? d_.configPath() : d_.paths.configDir + "/dofscroll.conf";
    loadConfig();
    modes_dir_ = d_.modesDir ? d_.modesDir() : d_.paths.configDir + "/modes";
    loadModeSet();
    clearModeRequest(d_.paths.runtimeDir);  // a request left over from before this start is stale
    // First resolution without device/program, so BACKEND/TICK_HZ from [global] are
    // known at startup.
    settings_ = profiles_.resolve("", "");
    match_desc_ = describeMatches(profiles_, "", "");

    std::string log;
    if (d_.makeOutput) output_ = d_.makeOutput(settings_.backend, &log);
    if (!log.empty()) {
        std::cerr << LOG << log;
        if (log.back() != '\n') std::cerr << std::endl;
    }
    if (!output_) {
        std::cerr << LOG << "ERROR: no output possible (BACKEND=" << settings_.backend
                  << ")" << std::endl;
        return false;
    }
    backend_cfg_ = settings_.backend;
    std::cerr << LOG << "Output via " << output_->name() << std::endl;
    // Clean starting point: a previous process (crash, SIGKILL) may have left Ctrl
    // or the mouse button pressed via XTest.
    output_->forceReleaseAll();
    output_->flush();

    runner_ = std::make_unique<ModeRunner>(output_.get());
    selectMode(settings_, false);

    if (!d_.activeWindow || !d_.activeWindow->init())
        std::cerr << LOG << "Active window not available - [app:*] profiles inactive"
                  << std::endl;

    if (d_.reload) {
        std::string err;
        if (!d_.reload->init(&err))
            std::cerr << LOG << "WARNING: reload channel not available"
                      << (err.empty() ? std::string() : " (" + sanitizeForLog(err) + ")")
                      << " - changes are still picked up automatically" << std::endl;
    }
    tick_period_ = periodFor(settings_.tick_hz);
    return true;
}

// Picks the mode named by next.mode (unknown -> "normal", warned once), applies its
// setting overrides to next and activates it in the runner. force: also re-activate
// when the id is unchanged (the mode files were reloaded). Returns true if the
// runner got a (new) mode.
bool DaemonCore::selectMode(Settings &next, bool force) {
    const ModeDef *m = modes_.find(mode_override_.empty() ? next.mode : mode_override_);
    if (!m) {
        if (warned_mode_ != next.mode) {
            warned_mode_ = next.mode;
            std::cerr << LOG << "WARNING: unknown mode '" << sanitizeForLog(next.mode)
                      << "', using 'normal'" << std::endl;
        }
        m = &modes_.normal();
    }
    // Precedence global < device < mode overrides < app: resolve again with the overrides.
    for (const auto &kv : m->overrides) {
        Settings probe;
        std::string err;
        if (!applyKey(probe, kv.first, kv.second, &err))
            std::cerr << LOG << "Mode " << sanitizeForLog(m->id) << ": " << sanitizeForLog(err) << std::endl;
    }
    next = profiles_.resolve(device_, wm_class_, &m->overrides);
    // The very first activation also applies "normal" (a file mode may override the built-in).
    if (!force && mode_applied_ && runner_->mode().id == m->id) return false;
    mode_applied_ = true;
    runner_->setMode(*m);
    std::cerr << LOG << "Mode: " << sanitizeForLog(m->id)
              << (runner_->legacyPath() ? " (default channels)" : "") << std::endl;
    return true;
}

// Re-resolves the settings. If neither the matching sections nor the resolved values
// change (e.g. config only touched, identical content or load error), nothing happens
// - in particular held keys (click drag, Ctrl) are kept. Otherwise the filter state is
// reset and Ctrl is released; mouse buttons are not touched (the tick logic reconciles
// them with the new settings). A mode change releases everything held.
void DaemonCore::applyProfile(bool modes_changed) {
    std::string desc = describeMatches(profiles_, device_, wm_class_);
    Settings next = profiles_.resolve(device_, wm_class_);
    const bool mode_switched = selectMode(next, modes_changed);
    if (!mode_switched && desc == match_desc_ && allKeys(next) == allKeys(settings_)) return;

    settings_ = std::move(next);
    match_desc_ = desc;

    runner_->resetDsp();
    output_->flush();

    std::cerr << LOG << "Profile: " << sanitizeForLog(match_desc_) << " (device \""
              << (device_.empty() ? "?" : sanitizeForLog(device_)) << "\", program \""
              << (wm_class_.empty() ? "?" : sanitizeForLog(wm_class_)) << "\")" << std::endl;

    if (settings_.backend != backend_cfg_) {
        std::cerr << LOG << "Note: BACKEND=" << settings_.backend
                  << " only takes effect after a restart (active: " << output_->name() << ")"
                  << std::endl;
        backend_cfg_ = settings_.backend;
    }
    warn_buttons_ = true;
    checkButtons();
}

// Warns if a configured button does not exist on the device (only once per
// device/profile change).
void DaemonCore::checkButtons() {
    if (!warn_buttons_) return;
    warn_buttons_ = false;
    int buttons = reader_.dev_buttons.load(std::memory_order_relaxed);
    if (buttons <= 0) return;
    if (settings_.nav_button_enabled && settings_.nav_button >= buttons)
        std::cerr << LOG << "WARNING: NAV_BUTTON " << settings_.nav_button
                  << " does not exist on this device" << std::endl;
    if (settings_.click_button_enabled && settings_.click_button >= buttons)
        std::cerr << LOG << "WARNING: CLICK_BUTTON " << settings_.click_button
                  << " does not exist on this device" << std::endl;
}

// Button-triggered mode switch: "next" cycles through all modes in alphabetical id order.
void DaemonCore::switchMode(const std::string &target) {
    std::string id = target;
    if (target == "next") {
        std::vector<std::string> ids;
        for (const ModeDef &m : modes_.modes) ids.push_back(m.id);
        std::sort(ids.begin(), ids.end());
        auto it = std::upper_bound(ids.begin(), ids.end(), runner_->mode().id);
        id = it == ids.end() ? ids.front() : *it;
    }
    if (!modes_.find(id)) return;
    mode_override_ = id;
    std::cerr << LOG << "Mode switched to " << sanitizeForLog(id) << std::endl;
    applyProfile(true);  // force: releases everything held, applies the mode's overrides
    tick_period_ = periodFor(settings_.tick_hz);
}

// --- Profile inputs: config, modes, device, active program ------------------
void DaemonCore::updateProfile(int64_t now_ns) {
    bool need_resolve = false;
    bool modes_changed = false;
    if (d_.reload && d_.reload->pollReload()) {
        std::cerr << LOG << "Reload requested: reloading config and modes" << std::endl;
        loadConfig();
        loadModeSet();
        // Runtime mode request of the tray (src/mode_request.hpp): applied like a button switch,
        // after the reload cleared the previous override.
        const std::string req = takeModeRequest(d_.paths.runtimeDir);
        if (!req.empty()) {
            if (modes_.find(req)) {
                mode_override_ = req;
                std::cerr << LOG << "Mode requested: " << sanitizeForLog(req) << std::endl;
            } else {
                std::cerr << LOG << "WARNING: requested mode '" << sanitizeForLog(req)
                          << "' is not loaded" << std::endl;
            }
        }
        need_resolve = true;
        modes_changed = true;
        next_cfg_poll_ = now_ns + CONFIG_POLL_NS;
    } else if (now_ns >= next_cfg_poll_) {
        next_cfg_poll_ = now_ns + CONFIG_POLL_NS;
        // Re-evaluate the path: e.g. the tray has meanwhile migrated the file to
        // the new folder while we were still on the legacy path.
        const std::string new_path = d_.configPath ? d_.configPath() : config_path_;
        if (new_path != config_path_) {
            config_path_ = new_path;
            std::cerr << LOG << "Config path changed: " << sanitizeForLog(config_path_)
                      << std::endl;
            loadConfig();  // sets the stamp anew
            need_resolve = true;
        } else if (statFile(config_path_) != stamp_) {
            std::cerr << LOG << "Config file changed, reloading" << std::endl;
            loadConfig();
            need_resolve = true;
        }
        if (modesStamp(modes_dir_) != modes_stamp_) {
            std::cerr << LOG << "Modes changed, reloading" << std::endl;
            loadModeSet();
            need_resolve = true;
            modes_changed = true;
        }
    }

    uint64_t gen = reader_.dev_generation.load(std::memory_order_acquire);
    if (gen != seen_generation_) {
        seen_generation_ = gen;
        next_devcount_ = 0;  // recount right after a (re)connect
        std::string name = reader_.deviceName();
        warn_buttons_ = true;
        if (name != device_) {
            device_ = name;
            need_resolve = true;
        } else {
            checkButtons();
        }
    }

    if (d_.activeWindow && now_ns >= next_win_poll_) {
        next_win_poll_ = now_ns + ACTIVEWIN_POLL_NS;
        std::string wc = d_.activeWindow->poll();
        if (wc != wm_class_) {
            wm_class_ = wc;
            need_resolve = true;
        }
    }

    if (need_resolve) {
        applyProfile(modes_changed);
        tick_period_ = periodFor(settings_.tick_hz);
    }

    // Count attached 3Dconnexion devices (cheap: every ~10 s or after connect).
    // A merged source (spacenavd) reports only one if there are several -> one-time hint
    // per change.
    if (now_ns >= next_devcount_) {
        next_devcount_ = now_ns + DEVCOUNT_POLL_NS;
        int c = d_.countDevices ? d_.countDevices() : (int)d_.input->devices().size();
        devices_seen_ = c < 0 ? 0 : c;
        std::string key = std::to_string(devices_seen_) + "|" + device_;
        if (devices_seen_ >= 2 && key != hint_key_) {
            std::string dn = device_.empty() ? "?" : sanitizeForLog(device_);
            std::cerr << LOG << "Note: " << devices_seen_
                      << " SpaceMice attached, " << d_.input->name() << " reports only '" << dn
                      << "' - profile [device:" << dn
                      << "] applies to all; buttons of both devices share the indices"
                      << std::endl;
        }
        hint_key_ = devices_seen_ >= 2 ? key : std::string();
    }
}

// Safety net: see SAFETY_QUIET_NS. Runs after the runner's tick, so the activity it looks at
// already went through the stale watchdog, the suspend/ENABLED gating and the dead zones.
void DaemonCore::safetyNet(int64_t now_ns, uint32_t buttons) {
    if (buttons != 0 || runner_->axesActive()) {
        quiet_since_ = now_ns;
        safety_done_ = false;
        return;
    }
    if (safety_done_ || now_ns - quiet_since_ < SAFETY_QUIET_NS) return;
    safety_done_ = true;
    const bool held = runner_->holdsAnything();
    runner_->releaseAll();
    output_->releaseAll();  // also clears what the output holds if the runner lost track
    output_->flush();
    if (held && (!safety_logged_ || now_ns - last_safety_log_ >= SAFETY_LOG_INTERVAL_NS)) {
        safety_logged_ = true;
        last_safety_log_ = now_ns;
        std::cerr << LOG << "safety release: keys/buttons were still held without any input, released"
                  << std::endl;
    }
}

void DaemonCore::tickOnce(int64_t now_ns, double dt) {
    // --- Read inputs ------------------------------------------------------
    bool fresh = (clock_() - reader_.last_motion_ns.load(std::memory_order_acquire)) <= STALE_NS;
    RawAxes raw{};
    if (fresh) {
        for (int i = 0; i < AX_COUNT; ++i)
            raw[i] = reader_.axes[i].load(std::memory_order_relaxed);
    }
    uint32_t buttons = reader_.button_mask.load(std::memory_order_relaxed);
    const RawAxes live_raw = raw;  // the live feed shows the device even while output is idle
    const uint32_t live_buttons = buttons;
    const bool live_fresh = fresh;

    // The tray evaluates the mouse itself (tab "Axes"/wizard): treat inputs as
    // "no deflection, no button" - held keys (click, Ctrl) are then released cleanly.
    if (now_ns >= next_susp_poll_) {
        next_susp_poll_ = now_ns + SUSPEND_POLL_NS;
        bool want = d_.suspendRequested ? d_.suspendRequested() : false;
        if (want != suspended_) {
            suspended_ = want;
            std::cerr << LOG << (suspended_ ? "Output paused (tray axis display)"
                                            : "Output active again") << std::endl;
        }
    }
    // Several users on one PC: the daemon of a background session must not act on the shared device.
    if (d_.sessionMonitor && now_ns >= next_session_poll_) {
        next_session_poll_ = now_ns + SESSION_POLL_NS;
        std::string detail;
        const bool active = d_.sessionMonitor->isActive(&detail);
        if (active != session_active_) {
            session_active_ = active;
            std::cerr << LOG << (active ? "session active: output resumed" : "session inactive: output paused")
                      << std::endl;
            if (d_.debug) std::cerr << LOG << "session: " << sanitizeForLog(detail) << std::endl;
        }
    }
    // Profile with ENABLED=0 (e.g. [app:blender]): same treatment, no output.
    const bool enabled = settings_.enabled;
    if (enabled != prev_enabled_) {
        prev_enabled_ = enabled;
        if (enabled) std::cerr << LOG << "Control active again" << std::endl;
        else std::cerr << LOG << "Control disabled (profile " << sanitizeForLog(match_desc_)
                       << ")" << std::endl;
    }
    const bool idle = suspended_ || !enabled || !session_active_;
    if (idle) {
        fresh = false;
        raw = RawAxes{};
        buttons = 0;
        if (!prev_idle_) {  // release everything held on entering suspend/disabled
            runner_->releaseAll();
            output_->releaseAll();
            output_->flush();
        }
    }
    prev_idle_ = idle;

    if (d_.writeStatus)
        d_.writeStatus("profile=" + sanitizeForLog(match_desc_) + "\ndevice=" + sanitizeForLog(device_) +
                           "\napp=" + sanitizeForLog(wm_class_) + "\nmode=" + sanitizeForLog(runner_->mode().id) +
                           "\nenabled=" + (enabled ? "1" : "0") +
                           "\npaused=" + (suspended_ ? "1" : "0") +
                           "\ndevices_seen=" + std::to_string(devices_seen_) +
                           "\nsession=" + (session_active_ ? "active" : "inactive") + "\n",
                       now_ns);

    // --- Channels, buttons, output (see mode_runner.hpp) ------------------
    runner_->tick(settings_, raw, buttons, fresh, dt);
    safetyNet(now_ns, buttons);
    {
        const std::string req = runner_->takeSwitchRequest();
        if (!req.empty()) switchMode(req);
    }

    if (d_.liveFeed) {
        live_.active = settings_.live_feed;
        if (live_.active) {
            const ModeRunner::Info &i = runner_->info();
            live_.axes = live_raw;
            live_.buttons = live_buttons;
            live_.fresh = live_fresh;
            live_.v = static_cast<float>(i.rate_v);
            live_.h = static_cast<float>(i.rate_h);
            live_.zoom = static_cast<float>(i.twist);
            live_.zooming = i.zooming;
            live_.nav = i.nav;
            live_.enabled = enabled;
            live_.idle = idle;
            live_.mode = runner_->mode().id;
        }
        d_.liveFeed(live_, now_ns);
    }

    if (d_.debug && now_ns >= next_debug_ &&
        (raw[AX_X] || raw[AX_Y] || raw[AX_Z] || raw[AX_RX] || raw[AX_RY] || raw[AX_RZ])) {
        next_debug_ = now_ns + DEBUG_PERIOD_NS;
        const ModeRunner::Info &i = runner_->info();
        std::cerr << "h=" << i.rate_h << " v=" << i.rate_v << " twist=" << i.twist
                  << " lateral=" << i.lateral << " zoom=" << i.zooming
                  << (i.nav ? " nav" : "") << " mode=" << sanitizeForLog(runner_->mode().id)
                  << " profile=[" << sanitizeForLog(match_desc_) << "]" << std::endl;
    }
    output_->flush();
}

void DaemonCore::run() {
    reader_.start();

    auto next_tick = std::chrono::steady_clock::now();
    auto last_tick = next_tick;

    std::cerr << LOG << "Profile: " << sanitizeForLog(match_desc_) << " (tick rate " << settings_.tick_hz
              << " Hz, mode " << sanitizeForLog(runner_->mode().id) << ")" << std::endl;

    while (!quit_.load(std::memory_order_relaxed)) {
        next_tick += tick_period_;
        std::this_thread::sleep_until(next_tick);
        if (quit_.load(std::memory_order_relaxed)) break;

        const int64_t now_ns = clock_();
        updateProfile(now_ns);

        // --- Time base ----------------------------------------------------
        auto now = std::chrono::steady_clock::now();
        double dt = std::min(std::chrono::duration<double>(now - last_tick).count(), MAX_DT);
        // Drift guard: if we are far behind the plan (suspend, stall), do not catch
        // up on all missed ticks.
        if (now > next_tick + std::chrono::milliseconds(100)) next_tick = now;
        last_tick = now;

        tickOnce(now_ns, dt);
    }

    runner_->releaseAll();
    output_->releaseAll();
    output_->flush();
    if (d_.removeStatus) d_.removeStatus();
    std::cerr << LOG << "terminated" << std::endl;
}

}  // namespace spn
