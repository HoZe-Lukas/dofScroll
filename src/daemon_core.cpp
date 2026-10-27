// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Platform-independent daemon core (see daemon_core.hpp).
#include "daemon_core.hpp"
#include "device_db.hpp"

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
    for (Slot &sl : slots_)
        for (auto &a : sl.axes) a.store(0, std::memory_order_relaxed);
}

std::string DeviceReader::deviceName() const {
    std::lock_guard<std::mutex> lk(name_mutex_);
    return dev_name_;
}

std::vector<DeviceInfo> DeviceReader::deviceList() const {
    std::lock_guard<std::mutex> lk(name_mutex_);
    return dev_list_;
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
        dev_list_ = devs;
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
    if (!src_->open(&err)) {
        if (!err.empty() && err != last_open_err_) {  // once per distinct reason, the retry loop repeats
            std::cerr << LOG << sanitizeForLog(err, 300) << std::endl;
            last_open_err_ = err;
        }
        return false;
    }
    last_open_err_.clear();
    events_since_connect.store(0, std::memory_order_relaxed);
    connected_ns.store(clock_(), std::memory_order_relaxed);
    std::cerr << LOG << "connected to " << src_->name() << std::endl;
    publishDeviceInfo();
    return true;
}

// The slot of a device; a free one is claimed on the first event (reader thread only).
DeviceReader::Slot *DeviceReader::claimSlot(int device_id) {
    for (Slot &s : slots_)
        if (s.id.load(std::memory_order_relaxed) == device_id) return &s;
    for (Slot &s : slots_)
        if (s.id.load(std::memory_order_relaxed) == -1) {
            for (auto &a : s.axes) a.store(0, std::memory_order_relaxed);
            s.button_mask.store(0, std::memory_order_relaxed);
            s.last_motion_ns.store(0, std::memory_order_relaxed);
            s.id.store(device_id, std::memory_order_release);
            return &s;
        }
    return nullptr;
}

// Clears everything: the aggregate and every slot.
void DeviceReader::clearState() {
    for (auto &a : axes) a.store(0, std::memory_order_relaxed);
    button_mask.store(0, std::memory_order_relaxed);
    for (Slot &s : slots_) {
        s.id.store(-1, std::memory_order_release);
        for (auto &a : s.axes) a.store(0, std::memory_order_relaxed);
        s.button_mask.store(0, std::memory_order_relaxed);
    }
}

void DeviceReader::disconnect() {
    src_->close();
    clearState();
}

// Only stores the latest state. Nothing is sent here, so irregular event timing does not
// affect the flow.
void DeviceReader::handle(const InputEvent &e) {
    if (e.type != InputEventType::DevicesChanged) events_since_connect.fetch_add(1, std::memory_order_relaxed);
    switch (e.type) {
    case InputEventType::Motion: {
        for (int i = 0; i < AX_COUNT; ++i) axes[i].store(e.axes[i], std::memory_order_relaxed);
        const int64_t now = clock_();
        last_motion_ns.store(now, std::memory_order_release);
        if (Slot *sl = claimSlot(e.device)) {
            for (int i = 0; i < AX_COUNT; ++i) sl->axes[i].store(e.axes[i], std::memory_order_relaxed);
            sl->last_motion_ns.store(now, std::memory_order_release);
        }
        break;
    }
    case InputEventType::Button:
        if (e.down && debug_) std::cerr << LOG << "Button " << e.button << " pressed" << std::endl;
        if (e.button >= 0 && e.button < 32) {
            uint32_t bit = 1u << e.button;
            if (e.down) button_mask.fetch_or(bit, std::memory_order_relaxed);
            else button_mask.fetch_and(~bit, std::memory_order_relaxed);
            if (Slot *sl = claimSlot(e.device)) {
                if (e.down) sl->button_mask.fetch_or(bit, std::memory_order_relaxed);
                else sl->button_mask.fetch_and(~bit, std::memory_order_relaxed);
            }
        }
        break;
    case InputEventType::DevicesChanged:
        // Device unplugged: release events never arrive then - held buttons/axes would
        // hang (a click would stay pressed). Deliberately no heuristic timeout: a held
        // button without motion is legitimate (click-and-hold).
        std::cerr << LOG << "Device list changed" << std::endl;
        {
            // Several devices (one id each): only what belonged to a removed device is cleared, the
            // others keep what they hold. A merged source cannot say which device went: clear all.
            const std::vector<DeviceInfo> devs = src_->devices();
            if (devs.size() < 2) {
                clearState();
            } else {
                for (Slot &sl : slots_) {
                    const int id = sl.id.load(std::memory_order_relaxed);
                    if (id == -1) continue;
                    bool stays = false;
                    for (const DeviceInfo &di : devs) stays = stays || di.id == id;
                    if (stays) continue;
                    sl.id.store(-1, std::memory_order_release);
                    for (auto &a : sl.axes) a.store(0, std::memory_order_relaxed);
                    sl.button_mask.store(0, std::memory_order_relaxed);
                }
            }
        }
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

        const int64_t t_connect_ns = clock_();
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
        if (got_event || clock_() - t_connect_ns >= HEALTHY_NS) delay_ms_ = MIN_DELAY_MS;
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

void DaemonCore::loadConfig() {
    for (auto &lp : lanes_) {
        lp->mode_override.clear();  // a reload drops the button-selected mode
        lp->toggled_mode.clear();
        lp->hold_active = false;
        if (lp->runner) lp->runner->clearToggles();  // ... and the toggled navigation mode
    }
    // Take the stamp before reading: if a file changes while reading, it is noticed at the next check.
    stamp_.reset(profileTreeStampAt(config_path_));
    ProfileSet ps;
    std::vector<FileDiag> diag;
    if (!loadProfileTreeAt(config_path_, &ps, &diag)) {
        std::cerr << LOG << "WARNING: config " << sanitizeForLog(config_path_, 512)
                  << " not readable, keeping previous settings" << std::endl;
    } else {
        profiles_ = std::move(ps);
    }
    profile_diag_ = std::move(diag);
    for (const FileDiag &d : profile_diag_)
        for (const std::string &w : d.warnings)
            std::cerr << LOG << "Config: " << sanitizeForLog(w, 512) << std::endl;
    // INPUT is a process-level key (the hybrid input of Windows and macOS; other sources ignore it).
    if (d_.input) {
        InputMode m = InputMode::Auto;
        const Settings g = profiles_.resolve("", "");
        if (g.input == "driver") m = InputMode::Driver;
        else if (g.input == "hid") m = InputMode::Hid;
        d_.input->setInputMode(m);
    }
}

void DaemonCore::loadModeSet() {
    for (auto &lp : lanes_) {
        lp->mode_override.clear();
        lp->toggled_mode.clear();
        lp->hold_active = false;
        if (lp->runner) lp->runner->clearToggles();
    }
    modes_stamp_ = modesStamp(modes_dir_);  // before reading, like the config stamp
    std::vector<std::string> warnings;
    modes_ = loadModes(modes_dir_, &warnings);
    warned_cycle_.clear();
    for (const std::string &w : warnings)
        std::cerr << LOG << "Modes: " << sanitizeForLog(w, 512) << std::endl;
}

void DaemonCore::loadButtonModeSet() {
    button_modes_stamp_ = buttonModesStamp(button_modes_dir_);  // before reading, like the other stamps
    std::vector<std::string> warnings;
    button_modes_ = loadButtonModes(button_modes_dir_, &warnings);
    warned_button_modes_.clear();
    warned_cycle_.clear();
    for (const std::string &w : warnings)
        std::cerr << LOG << "Button modes: " << sanitizeForLog(w, 512) << std::endl;
}

bool DaemonCore::initialize() {
    config_path_ = d_.configPath ? d_.configPath() : d_.paths.configDir + "/dofscroll.conf";
    loadConfig();
    modes_dir_ = d_.modesDir ? d_.modesDir() : d_.paths.configDir + "/modes";
    loadModeSet();
    button_modes_dir_ = d_.buttonModesDir ? d_.buttonModesDir() : d_.paths.configDir + "/buttons";
    loadButtonModeSet();
    clearModeRequest(d_.paths.runtimeDir);  // a request left over from before this start is stale
    // First resolution without device/program, so BACKEND/TICK_HZ from [global] are
    // known at startup.
    const Settings first = profiles_.resolve("", "");

    std::string log;
    if (d_.makeOutput) output_ = d_.makeOutput(first.backend, &log);
    if (!log.empty()) {
        std::cerr << LOG << log;
        if (log.back() != '\n') std::cerr << std::endl;
    }
    if (!output_) {
        std::cerr << LOG << "ERROR: no output possible (BACKEND=" << first.backend
                  << ")" << std::endl;
        return false;
    }
    backend_cfg_ = first.backend;
    std::cerr << LOG << "Output via " << output_->name() << std::endl;
    // Clean starting point: a previous process (crash, SIGKILL) may have left Ctrl
    // or the mouse button pressed via XTest.
    output_->forceReleaseAll();
    output_->flush();

    mux_ = std::make_unique<OutputMux>(output_.get());
    lanes_.push_back(makeLane(0));
    Lane &l0 = *lanes_.front();
    l0.settings = first;
    l0.match_desc = describeMatches(profiles_, "", "");
    selectMode(l0, l0.settings, false);

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
    updateTickPeriod();
    return true;
}

void DaemonCore::updateTickPeriod() {
    if (!lanes_.empty()) tick_period_ = periodFor(lanes_.front()->settings.tick_hz);
}

std::unique_ptr<Lane> DaemonCore::makeLane(int dev_id) {
    auto l = std::make_unique<Lane>();
    l->dev_id = dev_id;
    l->out = mux_->addView();
    if (!l->out) return nullptr;
    l->runner = std::make_unique<ModeRunner>(l->out);
    return l;
}

// The devices that get a lane: at most kMaxSlots, and one per model. A second device with the name of an
// earlier one (two SpaceMouse Compact) is ignored, because [device:*] could not tell them apart; it is logged
// once per device id.
std::vector<DeviceInfo> DaemonCore::usableDevices(const std::vector<DeviceInfo> &all) {
    std::vector<DeviceInfo> out;
    for (const DeviceInfo &di : all) {
        bool dup = false;
        for (const DeviceInfo &o : out) dup = dup || o.name == di.name;
        if (dup || out.size() >= static_cast<size_t>(DeviceReader::kMaxSlots)) {
            if (warned_ignored_.insert(di.id).second)
                std::cerr << LOG << "WARNING: device '" << sanitizeForLog(di.name) << "' ignored: "
                          << (dup ? "only one device per model is supported" : "too many devices") << std::endl;
            continue;
        }
        out.push_back(di);
    }
    return out;
}

// Releases everything one lane holds. Other lanes keep what they hold; with a single lane the whole
// output is cleared as before.
void DaemonCore::releaseLane(Lane &l, bool clear_toggles) {
    l.runner->releaseAll();
    if (clear_toggles) l.runner->clearToggles();
    if (lanes_.size() == 1) mux_->releaseEverything();
    else l.out->releaseAll();
}

// Binds the lanes to the attached devices. A lane keeps its state (mode, hold, toggle) as long as its
// device stays; a device that comes back under a new id (replug) takes over the lane of the same name.
void DaemonCore::syncLanes() {
    const std::vector<DeviceInfo> devs = usableDevices(reader_.deviceList());
    std::vector<bool> lane_used(lanes_.size(), false), dev_used(devs.size(), false);
    // 1. same id
    for (size_t d = 0; d < devs.size(); ++d)
        for (size_t i = 0; i < lanes_.size(); ++i)
            if (!lane_used[i] && lanes_[i]->bound && lanes_[i]->dev_id == devs[d].id) {
                lane_used[i] = dev_used[d] = true;
                break;
            }
    // 2. a lane without device (never bound, or its device went away) takes over a device without lane
    for (size_t d = 0; d < devs.size(); ++d) {
        if (dev_used[d]) continue;
        for (size_t i = 0; i < lanes_.size(); ++i) {
            if (lane_used[i]) continue;
            Lane &l = *lanes_[i];
            const bool same_model = l.bound && l.device == devs[d].name;
            if (l.bound && !same_model) continue;
            if (l.bound) releaseLane(l, true);  // the old device is gone: nothing of it may stay held
            l.dev_id = devs[d].id;
            l.bound = true;
            l.dirty = true;
            lane_used[i] = dev_used[d] = true;
            break;
        }
    }
    // 3. lanes of removed devices end (the first lane stays: it is the one that always exists)
    for (size_t i = lanes_.size(); i-- > 0;) {
        if (lane_used[i]) continue;
        if (lanes_.size() == 1) {  // the last lane always exists, it waits for a device
            Lane &l = *lanes_[0];
            if (l.bound) releaseLane(l, true);
            l.bound = false;
            continue;
        }
        std::cerr << LOG << "Device removed: lane for \"" << sanitizeForLog(lanes_[i]->device) << "\" ended" << std::endl;
        releaseLane(*lanes_[i], true);
        Output *view = lanes_[i]->out;
        lanes_.erase(lanes_.begin() + static_cast<std::ptrdiff_t>(i));  // the runner goes first, then its view
        mux_->removeView(view);
        lane_used.erase(lane_used.begin() + static_cast<std::ptrdiff_t>(i));
    }
    // 4. a device without lane: the lane that waits for a device takes it (the last lane never ends, so
    //    after a swap to another model there would otherwise be an empty lane next to the new one);
    //    otherwise a new lane
    for (size_t d = 0; d < devs.size(); ++d) {
        if (dev_used[d]) continue;
        bool taken = false;
        for (size_t i = 0; i < lanes_.size() && !taken; ++i) {
            Lane &l = *lanes_[i];
            if (lane_used[i] || l.bound) continue;
            l.mode_override.clear();  // state of the device that went away
            l.toggled_mode.clear();
            l.hold_active = false;
            l.runner->clearToggles();
            l.dev_id = devs[d].id;
            l.bound = true;
            l.dirty = true;
            lane_used[i] = dev_used[d] = taken = true;
        }
        if (taken) continue;
        std::unique_ptr<Lane> nl = makeLane(devs[d].id);
        if (!nl) {
            std::cerr << LOG << "WARNING: too many devices, '" << sanitizeForLog(devs[d].name) << "' is ignored" << std::endl;
            continue;
        }
        nl->bound = true;
        nl->dirty = true;
        nl->settings = profiles_.resolve("", "");
        nl->match_desc = describeMatches(profiles_, "", "");
        lanes_.push_back(std::move(nl));
        selectMode(*lanes_.back(), lanes_.back()->settings, false);
        lane_used.push_back(true);
        std::cerr << LOG << "New device: lane " << lanes_.size() << " for \"" << sanitizeForLog(devs[d].name) << "\"" << std::endl;
    }
    // names (the lane order does not follow the device order, so look the device up by id)
    for (auto &lp : lanes_) {
        Lane &l = *lp;
        if (!l.bound) {
            if (!l.device.empty()) { l.device.clear(); l.dirty = true; }
            continue;
        }
        for (size_t d = 0; d < devs.size(); ++d)
            if (devs[d].id == l.dev_id) {
                if (l.device != devs[d].name) {
                    l.device = devs[d].name;
                    l.dirty = true;
                }
            }
        l.warn_buttons = true;
    }
}

// Picks the mode named by next.mode (unknown -> "normal", warned once), applies its
// setting overrides to next and activates it in the runner. force: also re-activate
// when the id is unchanged (the mode files were reloaded). Returns true if the
// runner got a (new) mode.
bool DaemonCore::selectMode(Lane &l, Settings &next, bool force) {
    const ModeDef *m = modes_.find(l.mode_override.empty() ? next.mode : l.mode_override);
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
    next = profiles_.resolve(l.device, wm_class_, &m->overrides);
    // buttonmode:<id> references are replaced by the action of that button mode; at runtime only,
    // the config and the mode files keep the reference.
    std::set<std::string> missing;
    expandSettings(next, button_modes_, &missing);
    const ModeDef active = expandMode(*m, button_modes_, &missing);
    for (const std::string &id : missing)
        if (warned_button_modes_.insert(id).second)
            std::cerr << LOG << "WARNING: unknown button mode '" << sanitizeForLog(id)
                      << "', the button does nothing" << std::endl;
    // The very first activation also applies "normal" (a file mode may override the built-in).
    if (!force && l.mode_applied && l.runner->mode().id == m->id) return false;
    l.mode_applied = true;
    l.runner->setMode(active);
    std::cerr << LOG << "Mode: " << sanitizeForLog(m->id)
              << (l.runner->legacyPath() ? " (default channels)" : "") << std::endl;
    return true;
}

// Re-resolves the settings. If neither the matching sections nor the resolved values
// change (e.g. config only touched, identical content or load error), nothing happens
// - in particular held keys (click drag, Ctrl) are kept. Otherwise the filter state is
// reset and Ctrl is released; mouse buttons are not touched (the tick logic reconciles
// them with the new settings). A mode change releases everything held.
void DaemonCore::applyProfile(Lane &l, bool modes_changed) {
    std::string desc = describeMatches(profiles_, l.device, wm_class_);
    Settings next = profiles_.resolve(l.device, wm_class_);
    const bool mode_switched = selectMode(l, next, modes_changed);
    if (!mode_switched && desc == l.match_desc && sameSettings(next, l.settings)) return;

    l.settings = std::move(next);
    l.match_desc = desc;

    l.runner->resetDsp();
    output_->flush();

    std::cerr << LOG << "Profile: " << sanitizeForLog(l.match_desc) << " (device \""
              << (l.device.empty() ? "?" : sanitizeForLog(l.device)) << "\", program \""
              << (wm_class_.empty() ? "?" : sanitizeForLog(wm_class_)) << "\")" << std::endl;

    if (l.settings.backend != backend_cfg_) {
        std::cerr << LOG << "Note: BACKEND=" << l.settings.backend
                  << " only takes effect after a restart (active: " << output_->name() << ")"
                  << std::endl;
        backend_cfg_ = l.settings.backend;
    }
    l.warn_buttons = true;
    checkButtons(l);
}

// Warns if a configured button does not exist on the device (only once per
// device/profile change).
void DaemonCore::checkButtons(Lane &l) {
    if (!l.warn_buttons) return;
    l.warn_buttons = false;
    int buttons = 0;
    for (const DeviceInfo &di : reader_.deviceList())
        if (di.id == l.dev_id) buttons = di.reportedButtons;
    if (buttons <= 0) buttons = reader_.dev_buttons.load(std::memory_order_relaxed);
    if (buttons <= 0) return;
    if (l.settings.nav_button_enabled && l.settings.nav_button >= buttons)
        std::cerr << LOG << "WARNING: NAV_BUTTON " << l.settings.nav_button
                  << " does not exist on this device" << std::endl;
    if (l.settings.click_button_enabled && l.settings.click_button >= buttons)
        std::cerr << LOG << "WARNING: CLICK_BUTTON " << l.settings.click_button
                  << " does not exist on this device" << std::endl;
}

// Button-triggered mode switch: "next" / "prev" cycle through all modes in alphabetical id order, "next:<ids>" / "prev:<ids>" through the list.
void DaemonCore::switchMode(Lane &l, const std::string &target) {
    if (target == ModeRunner::kHoldReleaseRequest) {  // the button of a hold:mode: was released
        endHold(l, true);
        return;
    }
    if (target.compare(0, 5, "hold:") == 0) {
        // The mode is effective while the button is held. What was in effect before the press (a
        // runtime override, or none = the profile's mode) is restored on release.
        const std::string hid = target.substr(5);
        if (!modes_.find(hid)) return;
        l.hold_prev_override = l.mode_override;
        l.hold_prev_toggled = l.toggled_mode;
        l.hold_active = true;
        l.hold_id = hid;
        l.mode_override = hid;
        l.toggled_mode.clear();
        std::cerr << LOG << "Mode held: " << sanitizeForLog(hid) << std::endl;
        applyProfile(l, true);
        updateTickPeriod();
        return;
    }
    l.hold_active = false;  // any other switch ends a hold without restoring anything
    std::string id = target;
    bool fwd = true;
    std::vector<std::string> list;
    if (target.compare(0, 7, "toggle:") == 0) {
        // toggle:mode:<id>: the second press (the switch is in effect and <id> is the effective
        // mode) drops the switch and returns to the mode that profile resolution gives.
        id = target.substr(7);
        if (!modes_.find(id)) return;
        if (l.mode_override == id && l.runner->mode().id == id) {
            l.mode_override.clear();
            l.toggled_mode.clear();
            std::cerr << LOG << "Mode toggled off, back to the profile's mode" << std::endl;
            applyProfile(l, true);
            updateTickPeriod();
            return;
        }
    } else if (splitCycleTarget(target, &fwd, &list)) {
        const std::string &cur = l.runner->mode().id;
        if (list.empty()) {
            for (const ModeDef &m : modes_.modes) list.push_back(m.id);
            std::sort(list.begin(), list.end());
            if (fwd) {
                auto it = std::upper_bound(list.begin(), list.end(), cur);
                id = it == list.end() ? list.front() : *it;
            } else {
                // The id before the effective one; wraps from the first to the last.
                auto it = std::lower_bound(list.begin(), list.end(), cur);
                id = it == list.begin() ? list.back() : *(it - 1);
            }
        } else {
            // A user-defined cycle: unknown ids are skipped (warned once per load).
            std::vector<std::string> known;
            for (const std::string &e : list) {
                if (modes_.find(e)) {
                    known.push_back(e);
                } else if (warned_cycle_.insert(e).second) {
                    std::cerr << LOG << "WARNING: mode cycle: unknown mode '" << sanitizeForLog(e) << "' skipped" << std::endl;
                }
            }
            if (known.empty()) return;
            auto it = std::find(known.begin(), known.end(), cur);
            if (it == known.end()) id = fwd ? known.front() : known.back();
            else if (fwd) id = (it + 1 == known.end()) ? known.front() : *(it + 1);
            else id = it == known.begin() ? known.back() : *(it - 1);
        }
    }
    if (!modes_.find(id)) return;
    l.mode_override = id;
    l.toggled_mode = target.compare(0, 7, "toggle:") == 0 ? id : std::string();
    std::cerr << LOG << "Mode switched to " << sanitizeForLog(id) << std::endl;
    applyProfile(l, true);  // force: releases everything held, applies the mode's overrides
    updateTickPeriod();
}

// Ends a hold:mode:. restore: go back to the state from before the press (release of the button,
// suspend, ...). Does nothing when no hold is active or another switch already took over.
void DaemonCore::endHold(Lane &l, bool restore) {
    if (!l.hold_active) return;
    l.hold_active = false;
    if (!restore || l.mode_override != l.hold_id) return;
    l.mode_override = l.hold_prev_override;
    l.toggled_mode = l.hold_prev_toggled;
    std::cerr << LOG << "Mode hold released" << std::endl;
    applyProfile(l, true);
    updateTickPeriod();
}

std::string DaemonCore::toggleState(const Lane &l) const {
    std::string t;
    if (l.runner->navLatched()) t = "nav";
    if (!l.mode_override.empty() && l.mode_override == l.toggled_mode)
        t += (t.empty() ? "" : " ") + std::string("mode:") + l.mode_override;
    if (l.hold_active && l.mode_override == l.hold_id) t += (t.empty() ? "" : " ") + std::string("hold:") + l.hold_id;
    return t;
}

// --- Profile inputs: config, modes, device, active program ------------------
void DaemonCore::updateProfile(int64_t now_ns) {
    bool need_resolve = false;
    bool modes_changed = false;
    if (d_.reload && d_.reload->pollReload()) {
        std::cerr << LOG << "Reload requested: reloading config and modes" << std::endl;
        loadConfig();
        loadModeSet();
        loadButtonModeSet();
        // Runtime mode request of the tray (src/mode_request.hpp): applied like a button switch,
        // after the reload cleared the previous override.
        const std::string req = takeModeRequest(d_.paths.runtimeDir);
        if (!req.empty()) {
            if (modes_.find(req)) {
                for (auto &lp : lanes_) {
                    lp->mode_override = req;
                    lp->hold_active = false;
                }
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
        } else if (stamp_.settled(profileTreeStampAt(config_path_))) {
            std::cerr << LOG << "Configuration files changed, reloading" << std::endl;
            loadConfig();
            need_resolve = true;
        }
        if (modesStamp(modes_dir_) != modes_stamp_) {
            std::cerr << LOG << "Modes changed, reloading" << std::endl;
            loadModeSet();
            need_resolve = true;
            modes_changed = true;
        }
        if (buttonModesStamp(button_modes_dir_) != button_modes_stamp_) {
            std::cerr << LOG << "Button modes changed, reloading" << std::endl;
            loadButtonModeSet();
            need_resolve = true;
            modes_changed = true;
        }
    }

    uint64_t gen = reader_.dev_generation.load(std::memory_order_acquire);
    if (gen != seen_generation_) {
        seen_generation_ = gen;
        next_devcount_ = 0;  // recount right after a (re)connect
        if (d_.liveFeed) {
            live_.devices.clear();
            const std::vector<DeviceInfo> dl = usableDevices(reader_.deviceList());
            for (size_t di_i = 0; di_i < dl.size(); ++di_i) {
                const DeviceInfo &di = dl[di_i];
                if (live_.devices.size() >= kLiveMaxDevices) break;
                LiveDevice ld;
                ld.id = static_cast<uint8_t>(di.id);
                ld.vendor = static_cast<uint16_t>(di.vendor);
                ld.product = static_cast<uint16_t>(di.product);
                ld.reportedButtons = di.reportedButtons;
                ld.factoryButtons = factoryButtonCount(di.name, di.reportedButtons);
                ld.axes = di.axes;
                ld.name = di.name;
                live_.devices.push_back(std::move(ld));
            }
            live_.devicesRev++;
        }
        syncLanes();
        for (auto &lp : lanes_)
            if (!lp->dirty) checkButtons(*lp);
    }

    if (d_.activeWindow && now_ns >= next_win_poll_) {
        next_win_poll_ = now_ns + ACTIVEWIN_POLL_NS;
        std::string wc = d_.activeWindow->poll();
        if (wc != wm_class_) {
            wm_class_ = wc;
            need_resolve = true;
        }
    }

    for (size_t i = 0; i < lanes_.size(); ++i) {
        Lane &l = *lanes_[i];
        if (need_resolve || l.dirty) {
            l.dirty = false;
            applyProfile(l, modes_changed);
        }
    }
    if (need_resolve) updateTickPeriod();

    // Count attached 3Dconnexion devices (cheap: every ~10 s or after connect).
    // A merged source (spacenavd) reports only one if there are several -> one-time hint
    // per change.
    if (now_ns >= next_devcount_) {
        next_devcount_ = now_ns + DEVCOUNT_POLL_NS;
        int c = d_.countDevices ? d_.countDevices() : (int)d_.input->devices().size();
        devices_seen_ = c < 0 ? 0 : c;
        const std::string &dev0 = lanes_.front()->device;
        // Only a merged source can hide devices: with one lane per device nothing is hidden.
        std::string key = std::to_string(devices_seen_) + "|" + dev0;
        if (devices_seen_ >= 2 && lanes_.size() < 2 && key != hint_key_) {
            std::string dn = dev0.empty() ? "?" : sanitizeForLog(dev0);
            std::cerr << LOG << "Note: " << devices_seen_
                      << " SpaceMice attached, " << d_.input->name() << " reports only '" << dn
                      << "' - profile [device:" << dn
                      << "] applies to all; buttons of both devices share the indices (docs/multi-device.md)"
                      << std::endl;
        }
        hint_key_ = devices_seen_ >= 2 && lanes_.size() < 2 ? key : std::string();
    }

    // A device that is found but cannot be read (platform/input_diag.hpp): logged once per change,
    // shown through the status file (input_issue=) in the UI.
    {
        const int64_t since = now_ns - reader_.connected_ns.load(std::memory_order_relaxed);
        const InputIssue is = decideInputIssue(reader_.sourceName(), reader_.accessBlocked(), devices_seen_,
                                               reader_.events_since_connect.load(std::memory_order_relaxed), since);
        if (is != input_issue_) {
            input_issue_ = is;
            if (is != InputIssue::Clear) std::cerr << LOG << inputIssueText(is) << std::endl;
        }
    }
}

// Safety net: see SAFETY_QUIET_NS. Runs after the runner's tick, so the activity it looks at
// already went through the stale watchdog, the suspend/ENABLED gating and the dead zones.
void DaemonCore::safetyNet(Lane &l, int64_t now_ns, uint32_t buttons) {
    if (buttons != 0 || l.runner->axesActive()) {
        l.quiet_since = now_ns;
        l.safety_done = false;
        return;
    }
    if (l.safety_done || now_ns - l.quiet_since < SAFETY_QUIET_NS) return;
    l.safety_done = true;
    const bool held = l.runner->holdsAnything();
    l.runner->releaseAll();
    // also clears what the output holds if the runner lost track (one lane: the whole output)
    if (lanes_.size() == 1) mux_->releaseEverything();
    else l.out->releaseAll();
    output_->flush();
    if (held && (!l.safety_logged || now_ns - l.last_safety_log >= SAFETY_LOG_INTERVAL_NS)) {
        l.safety_logged = true;
        l.last_safety_log = now_ns;
        std::cerr << LOG << "safety release: keys/buttons were still held without any input, released"
                  << std::endl;
    }
}

void DaemonCore::tickOnce(int64_t now_ns, double dt) {
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
    const bool multi = lanes_.size() > 1;
    const int64_t clk = clock_();
    Lane *live_lane = lanes_.front().get();  // the live feed shows the device that moved last
    int64_t live_last = -1;
    RawAxes live_raw{};
    uint32_t live_buttons = 0;
    bool live_fresh = false, live_enabled = true, live_idle = false;
    std::string status_extra;

    for (size_t li = 0; li < lanes_.size(); ++li) {
        Lane &l = *lanes_[li];
        // --- Read inputs ----------------------------------------------------
        const DeviceReader::Slot *slot = reader_.slotFor(l.dev_id);
        const int64_t last_motion = slot ? slot->last_motion_ns.load(std::memory_order_acquire) : 0;
        bool fresh = slot && (clk - last_motion) <= STALE_NS;
        RawAxes raw{};
        if (fresh)
            for (int i = 0; i < AX_COUNT; ++i) raw[i] = slot->axes[i].load(std::memory_order_relaxed);
        uint32_t buttons = slot ? slot->button_mask.load(std::memory_order_relaxed) : 0;
        const RawAxes lane_live_raw = raw;  // the live feed shows the device even while output is idle
        const uint32_t lane_live_buttons = buttons;
        const bool lane_live_fresh = fresh;

        // Profile with ENABLED=0 (e.g. [app:blender]): same treatment, no output.
        const bool enabled = l.settings.enabled;
        if (enabled != l.prev_enabled) {
            l.prev_enabled = enabled;
            if (enabled) std::cerr << LOG << "Control active again" << std::endl;
            else std::cerr << LOG << "Control disabled (profile " << sanitizeForLog(l.match_desc) << ")" << std::endl;
        }
        const bool idle = suspended_ || !enabled || !session_active_;
        if (idle) {
            fresh = false;
            raw = RawAxes{};
            buttons = 0;
            if (!l.prev_idle) {  // release everything held on entering suspend/disabled
                endHold(l, true);  // a held mode must not stay in effect without the button
                releaseLane(l, true);
                output_->flush();
            }
        }
        l.prev_idle = idle;

        // --- Channels, buttons, output (see mode_runner.hpp) ----------------
        l.runner->tick(l.settings, raw, buttons, fresh, dt);
        safetyNet(l, now_ns, buttons);
        {
            const std::string req = l.runner->takeSwitchRequest();
            if (!req.empty()) switchMode(l, req);
        }

        if (last_motion > live_last || li == 0) {
            if (last_motion > live_last) live_last = last_motion;
            live_lane = &l;
            live_raw = lane_live_raw;
            live_buttons = lane_live_buttons;
            live_fresh = lane_live_fresh;
            live_enabled = enabled;
            live_idle = idle;
        }
        if (li > 0) {
            const std::string n = std::to_string(li + 1);
            status_extra += "device" + n + "=" + sanitizeForLog(l.device) + "\nprofile" + n + "=" + sanitizeForLog(l.match_desc) +
                            "\nmode" + n + "=" + sanitizeForLog(l.runner->mode().id) + "\ntoggle" + n + "=" + toggleState(l) +
                            "\nenabled" + n + "=" + (enabled ? "1" : "0") + "\n";
        }

        if (d_.debug && now_ns >= next_debug_ &&
            (raw[AX_X] || raw[AX_Y] || raw[AX_Z] || raw[AX_RX] || raw[AX_RY] || raw[AX_RZ])) {
            const ModeRunner::Info &i = l.runner->info();
            std::cerr << (multi ? "[" + sanitizeForLog(l.device) + "] " : std::string()) << "h=" << i.rate_h << " v=" << i.rate_v
                      << " twist=" << i.twist << " lateral=" << i.lateral << " zoom=" << i.zooming << (i.nav ? " nav" : "")
                      << (l.runner->mode().vSplit.active() ? " v_push=" + std::to_string(i.rate_part[PART_V_PUSH]) +
                                                                 " v_tilt=" + std::to_string(i.rate_part[PART_V_TILT])
                                                           : std::string())
                      << (l.runner->mode().hSplit.active() ? " h_push=" + std::to_string(i.rate_part[PART_H_PUSH]) +
                                                                 " h_tilt=" + std::to_string(i.rate_part[PART_H_TILT])
                                                           : std::string())
                      << " mode=" << sanitizeForLog(l.runner->mode().id) << " profile=[" << sanitizeForLog(l.match_desc) << "]"
                      << std::endl;
        }
    }
    if (d_.debug && now_ns >= next_debug_) next_debug_ = now_ns + DEBUG_PERIOD_NS;

    Lane &first = *lanes_.front();
    if (d_.writeStatus)
        d_.writeStatus("profile=" + sanitizeForLog(first.match_desc) + "\ndevice=" + sanitizeForLog(first.device) +
                           "\napp=" + sanitizeForLog(wm_class_) + "\nmode=" + sanitizeForLog(first.runner->mode().id) +
                           "\ntoggle=" + toggleState(first) +
                           "\nenabled=" + (first.settings.enabled ? "1" : "0") +
                           "\npaused=" + (suspended_ ? "1" : "0") +
                           "\ndevices_seen=" + std::to_string(devices_seen_) +
                           (multi ? "\nlanes=" + std::to_string(lanes_.size()) : std::string()) +
                           (multi ? "\nactive_device=" + sanitizeForLog(live_lane->device) : std::string()) +
                           (d_.input && !d_.input->pathLabel().empty() ? "\ninput=" + d_.input->pathLabel() : std::string()) +
                           (input_issue_ == InputIssue::Clear ? std::string()
                                                             : std::string("\ninput_issue=") + inputIssueKey(input_issue_)) +
                           "\nsession=" + (session_active_ ? "active" : "inactive") + "\n" + status_extra,
                       now_ns);

    if (d_.liveFeed) {
        live_.active = live_lane->settings.live_feed;
        if (live_.active) {
            const ModeRunner::Info &i = live_lane->runner->info();
            live_.axes = live_raw;
            live_.buttons = live_buttons;
            live_.fresh = live_fresh;
            live_.v = static_cast<float>(i.rate_v);
            live_.h = static_cast<float>(i.rate_h);
            live_.zoom = static_cast<float>(i.twist);
            live_.zooming = i.zooming;
            live_.nav = i.nav;
            live_.enabled = live_enabled;
            live_.idle = live_idle;
            live_.mode = live_lane->runner->mode().id;
        }
        d_.liveFeed(live_, now_ns);
    }

    output_->flush();
}

void DaemonCore::run() {
    reader_.start();

    auto next_tick = std::chrono::steady_clock::now();
    auto last_tick = next_tick;

    std::cerr << LOG << "Profile: " << sanitizeForLog(lanes_.front()->match_desc) << " (tick rate "
              << lanes_.front()->settings.tick_hz << " Hz, mode " << sanitizeForLog(lanes_.front()->runner->mode().id)
              << ")" << std::endl;

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

    for (auto &lp : lanes_) lp->runner->releaseAll();
    mux_->releaseEverything();
    output_->flush();
    if (d_.removeStatus) d_.removeStatus();
    std::cerr << LOG << "terminated" << std::endl;
}

}  // namespace spn
