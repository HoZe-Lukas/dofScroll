// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll 2.0: daemon that translates a 3Dconnexion SpaceMouse (via spacenavd/
// libspnav) into scroll, zoom, arrow-key and mouse-button events.
// It uses the shared modules:
//   config.hpp      - settings/profiles ([global], [device:*], [app:*])
//   dsp.hpp         - channel signal processing, step accumulator, zoom detection
//   modes.hpp       - pluggable modes (loaded from the modes directory)
//   mode_runner.hpp - per-tick evaluation of the active mode (legacy and custom path)
//   output.hpp      - output via X11/XTest or uinput
//   activewin.hpp   - active program (WM_CLASS) for [app:*] profiles
//
// Threads: a reader thread blocks on spnav_wait_event() and only stores the latest
// state (lock-free atomics); the main thread ticks at Settings.tick_hz, evaluates and
// sends evenly distributed steps (instead of bursts per event). Config/profiles belong
// to the main thread alone.
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <streambuf>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctime>
#include <X11/Xlib.h>
#include <spnav.h>

// Xlib defines None as a macro, which clashes with ChannelActionType::None (modes.hpp).
#undef None

#include "activewin.hpp"
#include "common.hpp"
#include "config.hpp"
#include "device_count.hpp"
#include "dsp.hpp"
#include "log_sanitize.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"
#include "output.hpp"
#include "profile_match.hpp"

using namespace spn;

namespace {

const char *const VERSION = "dofscroll 2.0";
const char *const LOG = "[dofscroll] ";

// --debug: only log button presses then (set by the daemon before the reader starts).
std::atomic<bool> g_debug{false};

// Set by signal handlers (hence sig_atomic_t instead of bool). The main loop checks
// the flags every tick and acts itself - the handlers stay minimal.
volatile std::sig_atomic_t g_reload_requested = 0;
volatile std::sig_atomic_t g_quit_requested = 0;

void handleReloadSignal(int) { g_reload_requested = 1; }
void handleQuitSignal(int) { g_quit_requested = 1; }

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

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
// Check the tray's heartbeat file (pause output) ~20x/s.
constexpr int64_t SUSPEND_POLL_NS = 50'000'000;
// --debug output ~10x/s.
constexpr int64_t DEBUG_PERIOD_NS = 100'000'000;
// Recount attached 3Dconnexion devices (additionally on every (re)connect).
constexpr int64_t DEVCOUNT_POLL_NS = 10'000'000'000;
// Status file: when unchanged, rewrite at most this often ("updated" timestamp).
constexpr int64_t STATUS_REFRESH_NS = 1'000'000'000;

// Is the tray pausing the output right now (fresh heartbeat file)?
// Trust rule: the file must belong to the effective user and must not be accessible
// to group/others; XDG_RUNTIME_DIR itself must belong to the effective user and must
// not be group/other-writable (checked once, cached) - otherwise the mechanism is
// ignored.
bool suspendRequested() {
    static const std::string path = suspendFilePath();
    if (path.empty()) return false;
    static const bool dir_ok = [] {
        const char *d = std::getenv("XDG_RUNTIME_DIR");
        struct stat ds;
        if (!d || stat(d, &ds) != 0 || !S_ISDIR(ds.st_mode)) return false;
        return ds.st_uid == geteuid() && (ds.st_mode & 022) == 0;
    }();
    if (!dir_ok) return false;
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
    if (st.st_uid != geteuid() || (st.st_mode & 077) != 0) return false;
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    long age_ms = (now.tv_sec - st.st_mtim.tv_sec) * 1000L +
                  (now.tv_nsec - st.st_mtim.tv_nsec) / 1000000L;
    return age_ms >= -SUSPEND_MAX_AGE_MS && age_ms <= SUSPEND_MAX_AGE_MS;
}

// Is XDG_RUNTIME_DIR trustworthy? (owned by the effective user, not group/other-
// writable) - prerequisite for the status and log files.
bool runtimeDirTrusted() {
    static const bool ok = [] {
        const char *d = std::getenv("XDG_RUNTIME_DIR");
        struct stat ds;
        if (!d || !*d || stat(d, &ds) != 0 || !S_ISDIR(ds.st_mode)) return false;
        return ds.st_uid == geteuid() && (ds.st_mode & 022) == 0;
    }();
    return ok;
}

// Mirrors everything that goes to std::cerr (stderr/journal stays unchanged) into the
// log file in the runtime dir as well (for display in the tray). Written on flush
// (std::endl) under a mutex, because the reader thread logs too. O_APPEND|O_NOFOLLOW,
// 0600; owner/type are checked. Rolling buffer: when max_lines (normal operation
// kLogMaxLines = 300, --debug kLogMaxLinesDebug) or kLogMaxBytes is exceeded, only the
// newest keep_lines lines remain (the file is rewritten atomically). Any error only
// disables the mirroring; the main thread never blocks (regular file on tmpfs,
// O_NONBLOCK).
class TeeLogBuf : public std::streambuf {
public:
    TeeLogBuf(std::streambuf *orig, size_t max_lines, size_t keep_lines)
        : orig_(orig), max_lines_(max_lines), keep_lines_(keep_lines) {
        const std::string path = logFilePath();
        if (path.empty() || !runtimeDirTrusted()) return;
        int fd = ::open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        if (fd < 0) return;
        struct stat st;
        if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
            (st.st_mode & 077) != 0) {
            ::close(fd);
            return;
        }
        fd_ = fd;
        size_ = (size_t)st.st_size;
        path_ = path;
        loadExisting();  // a restart (Restart=always) must not lose the log
    }

protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        char ch = traits_type::to_char_type(c);
        return xsputn(&ch, 1) == 1 ? c : traits_type::eof();
    }
    std::streamsize xsputn(const char *s, std::streamsize n) override {
        std::streamsize r = orig_->sputn(s, n);
        std::lock_guard<std::mutex> lk(m_);
        if (fd_ >= 0 && pending_.size() < 64 * 1024) {
            // Timestamp (local time) only in the file, at the start of each line.
            for (std::streamsize i = 0; i < n; ++i) {
                if (at_line_start_) {
                    char ts[32];
                    std::time_t t = std::time(nullptr);
                    std::tm tmv{};
                    localtime_r(&t, &tmv);
                    if (std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S ", &tmv) > 0) pending_ += ts;
                    at_line_start_ = false;
                }
                pending_ += s[i];
                if (s[i] == '\n') at_line_start_ = true;
            }
        }
        return r;
    }
    int sync() override {
        int r = orig_->pubsync();
        std::lock_guard<std::mutex> lk(m_);
        flushPending();
        return r;
    }

private:
    std::streambuf *orig_;
    size_t max_lines_, keep_lines_;
    std::mutex m_;
    int fd_ = -1;
    size_t size_ = 0;
    std::string pending_;
    std::string path_;
    std::deque<std::string> lines_;  // mirror of the file content (without \n)
    std::string partial_;            // line not yet terminated
    bool at_line_start_ = true;      // next character starts a line (timestamp)

    void loadExisting() {
        if (size_ == 0 || size_ > kLogMaxBytes) {
            if (size_ > kLogMaxBytes) rewrite();  // too long (old version): clear
            return;
        }
        int rfd = ::open(path_.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (rfd < 0) return;
        std::string data(size_, '\0');
        ssize_t n = ::pread(rfd, &data[0], data.size(), 0);
        ::close(rfd);
        if (n <= 0) return;
        data.resize((size_t)n);
        size_t pos = 0;
        while (pos < data.size()) {
            size_t nl = data.find('\n', pos);
            if (nl == std::string::npos) break;
            lines_.push_back(data.substr(pos, nl - pos));
            pos = nl + 1;
        }
        if (lines_.size() > max_lines_) {
            while (lines_.size() > keep_lines_) lines_.pop_front();
            rewrite();
        }
    }

    // Rewrites lines_ atomically (temp + rename) and reopens the append descriptor.
    void rewrite() {
        const std::string tmp = path_ + ".tmp";
        ::unlink(tmp.c_str());
        int tfd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (tfd < 0) { disable(); return; }
        std::string out;
        for (const std::string &l : lines_) { out += l; out += '\n'; }
        const char *p = out.data();
        size_t left = out.size();
        bool ok = true;
        while (left > 0) {
            ssize_t w = ::write(tfd, p, left);
            if (w <= 0) { if (w < 0 && errno == EINTR) continue; ok = false; break; }
            p += w;
            left -= (size_t)w;
        }
        ::close(tfd);
        if (!ok || ::rename(tmp.c_str(), path_.c_str()) != 0) {
            ::unlink(tmp.c_str());
            disable();
            return;
        }
        if (fd_ >= 0) ::close(fd_);
        fd_ = ::open(path_.c_str(), O_WRONLY | O_APPEND | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
        if (fd_ < 0) { disable(); return; }
        size_ = out.size();
    }

    void flushPending() {
        if (fd_ < 0 || pending_.empty()) {
            pending_.clear();
            return;
        }
        const char *p = pending_.data();
        size_t left = pending_.size();
        while (left > 0) {
            ssize_t w = ::write(fd_, p, left);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                disable();
                return;
            }
            p += w;
            left -= (size_t)w;
            size_ += (size_t)w;
        }
        // Count lines; on overflow trim to the newest keep_lines_.
        partial_ += pending_;
        pending_.clear();
        size_t pos = 0, nl;
        while ((nl = partial_.find('\n', pos)) != std::string::npos) {
            lines_.push_back(partial_.substr(pos, nl - pos));
            pos = nl + 1;
        }
        partial_.erase(0, pos);
        if (partial_.size() > 8192) partial_.clear();  // guard, never an endless line
        if (lines_.size() > max_lines_ || size_ > kLogMaxBytes) {
            while (lines_.size() > keep_lines_) lines_.pop_front();
            rewrite();
        }
    }
    void disable() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        pending_.clear();
    }
};

// Writes the status file (format: see common.hpp) atomically: temp file in the same
// directory (O_EXCL|O_NOFOLLOW, 0600) + rename. Errors are never fatal, logged once.
// Without XDG_RUNTIME_DIR nothing happens.
class StatusWriter {
public:
    StatusWriter() : path_(statusFilePath()) {}

    // body = lines without "updated"; written on change or after STATUS_REFRESH_NS.
    void update(const std::string &body, int64_t now_ns) {
        if (path_.empty() || !runtimeDirTrusted()) return;
        if (body == last_body_ && now_ns < next_refresh_ns_) return;
        last_body_ = body;
        next_refresh_ns_ = now_ns + STATUS_REFRESH_NS;
        std::string text = body + "updated=" + std::to_string((long long)std::time(nullptr)) + "\n";
        if (text.size() > kStatusMaxBytes) return;
        if (!write(text) && !warned_) {
            warned_ = true;
            std::cerr << LOG << "WARNING: status file not writable (" << std::strerror(errno)
                      << ") - tray display limited" << std::endl;
        }
    }

    void remove() {
        if (!path_.empty() && wrote_) ::unlink(path_.c_str());
    }

private:
    std::string path_, last_body_;
    int64_t next_refresh_ns_ = 0;
    bool warned_ = false, wrote_ = false;

    bool write(const std::string &text) {
        const std::string tmp = path_ + ".tmp." + std::to_string((long long)getpid());
        int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno == EEXIST) {  // leftover of an earlier run with the same PID
            ::unlink(tmp.c_str());
            fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        }
        if (fd < 0) return false;
        const char *p = text.data();
        size_t left = text.size();
        bool ok = true;
        while (left > 0) {
            ssize_t w = ::write(fd, p, left);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                ok = false;
                break;
            }
            p += w;
            left -= (size_t)w;
        }
        int e = errno;
        if (::close(fd) != 0 && ok) { ok = false; e = errno; }
        if (ok && ::rename(tmp.c_str(), path_.c_str()) != 0) { ok = false; e = errno; }
        if (!ok) {
            ::unlink(tmp.c_str());
            errno = e;
            return false;
        }
        wrote_ = true;
        return true;
    }
};

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

// Identity of the config file for auto-reload detection. The tray writes atomically
// (temp + rename) -> compare inode/size too.
struct FileStamp {
    bool exists = false;
    int64_t mtime_ns = 0;
    int64_t size = 0;
    uint64_t ino = 0;
    bool operator!=(const FileStamp &o) const {
        return exists != o.exists || mtime_ns != o.mtime_ns || size != o.size || ino != o.ino;
    }
};

FileStamp statFile(const std::string &path) {
    FileStamp f;
    struct stat st;
    if (::stat(path.c_str(), &st) == 0) {
        f.exists = true;
        f.mtime_ns = (int64_t)st.st_mtim.tv_sec * 1'000'000'000 + st.st_mtim.tv_nsec;
        f.size = (int64_t)st.st_size;
        f.ino = (uint64_t)st.st_ino;
    }
    return f;
}

// ---------------------------------------------------------------------------
// Reader thread: SpaceMouse state, lock-free towards the main thread.
// ---------------------------------------------------------------------------
class DeviceReader {
public:
    std::array<std::atomic<int>, AX_COUNT> axes{};
    // Bitmask of pressed buttons (bit n = spnav_event_button.bnum n).
    std::atomic<uint32_t> button_mask{0};
    // Timestamp (steady_clock, ns) of the last motion event -> stale watchdog.
    std::atomic<int64_t> last_motion_ns{0};
    // Button count of the current device (for warnings in the main thread).
    std::atomic<int> dev_buttons{0};
    // Incremented after every (re)connect, after the name was set under the mutex.
    // The main thread only compares this number per tick (cheap) and fetches the
    // name under the mutex only on change.
    std::atomic<uint64_t> dev_generation{0};

    DeviceReader() {
        for (auto &a : axes) a.store(0, std::memory_order_relaxed);
    }

    std::string deviceName() const {
        std::lock_guard<std::mutex> lk(name_mutex_);
        return dev_name_;
    }

    void start() {
        std::thread t(&DeviceReader::loop, this);
        // Detached: spnav_wait_event() blocks without a way to abort; on exit the
        // process teardown cleans the thread up.
        t.detach();
    }

private:
    mutable std::mutex name_mutex_;
    std::string dev_name_;

    // Logs device name/buttons/axes and passes the name on to the main thread
    // (profile [device:*]). After hotplug the device can change -> on every
    // (re)connect.
    void publishDeviceInfo() {
        char name[256] = "";
        if (spnav_dev_name(name, sizeof(name)) < 0) name[0] = '\0';
        name[sizeof(name) - 1] = '\0';
        int buttons = spnav_dev_buttons();
        std::cerr << LOG << "Device: " << (name[0] ? sanitizeForLog(name) : std::string("?")) << " (" << buttons
                  << " buttons, " << spnav_dev_axes() << " axes)" << std::endl;
        dev_buttons.store(buttons, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(name_mutex_);
            dev_name_ = name;
        }
        dev_generation.fetch_add(1, std::memory_order_release);
    }

    // Backoff delay, kept across calls: only reset after a connection that delivered
    // events or stood for a long time.
    static constexpr int MIN_DELAY_MS = 500;
    static constexpr int MAX_DELAY_MS = 5000;
    static constexpr int64_t HEALTHY_NS = 10'000'000'000;  // 10 s
    int delay_ms_ = MIN_DELAY_MS;

    void backoffSleep(const char *why) {
        std::cerr << LOG << why << ", next attempt in " << (delay_ms_ / 1000.0) << "s"
                  << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        delay_ms_ = std::min(delay_ms_ * 2, MAX_DELAY_MS);
    }

    // Connects to spacenavd, exponential backoff up to 5 s. This way the service
    // starts cleanly even if spacenavd is not (yet) running, and recovers by itself
    // after a spacenavd restart.
    void connectWithRetry() {
        while (spnav_open() == -1) backoffSleep("spacenavd not reachable");
        std::cerr << LOG << "connected to spacenavd" << std::endl;
        publishDeviceInfo();
    }

    void clearState() {
        for (auto &a : axes) a.store(0, std::memory_order_relaxed);
        button_mask.store(0, std::memory_order_relaxed);
    }

    // Blocks on spnav events and only stores the latest state. Nothing is sent
    // here, so irregular event timing does not affect the flow.
    void loop() {
        while (true) {
            connectWithRetry();

            const int64_t connected_ns = nowNs();
            bool got_event = false;
            spnav_event sev;
            while (spnav_wait_event(&sev)) {
                got_event = true;
                if (sev.type == SPNAV_EVENT_MOTION) {
                    axes[AX_X].store(sev.motion.x, std::memory_order_relaxed);
                    axes[AX_Y].store(sev.motion.y, std::memory_order_relaxed);
                    axes[AX_Z].store(sev.motion.z, std::memory_order_relaxed);
                    axes[AX_RX].store(sev.motion.rx, std::memory_order_relaxed);
                    axes[AX_RY].store(sev.motion.ry, std::memory_order_relaxed);
                    axes[AX_RZ].store(sev.motion.rz, std::memory_order_relaxed);
                    last_motion_ns.store(nowNs(), std::memory_order_release);
                } else if (sev.type == SPNAV_EVENT_BUTTON) {
                    if (sev.button.press && g_debug.load(std::memory_order_relaxed))
                        std::cerr << LOG << "Button " << sev.button.bnum << " pressed" << std::endl;
                    if (sev.button.bnum >= 0 && sev.button.bnum < 32) {
                        uint32_t bit = 1u << sev.button.bnum;
                        if (sev.button.press) button_mask.fetch_or(bit, std::memory_order_relaxed);
                        else button_mask.fetch_and(~bit, std::memory_order_relaxed);
                    }
                } else if (sev.type == SPNAV_EVENT_DEV && sev.dev.op == SPNAV_DEV_RM) {
                    // Device unplugged: release events never arrive then - held
                    // buttons/axes would hang (a click would stay pressed).
                    // Deliberately no heuristic timeout: a held button without
                    // motion is legitimate (click-and-hold).
                    std::cerr << LOG << "Device removed" << std::endl;
                    clearState();
                }
            }

            spnav_close();
            clearState();
            if (got_event || nowNs() - connected_ns >= HEALTHY_NS) delay_ms_ = MIN_DELAY_MS;
            backoffSleep("Connection to spacenavd lost");
        }
    }
};

// ---------------------------------------------------------------------------
// Main thread: profiles, evaluation, output.
// ---------------------------------------------------------------------------
class Daemon {
public:
    explicit Daemon(bool debug) : debug_(debug) { g_debug.store(debug, std::memory_order_relaxed); }

    bool initialize() {
        {
            // Best effort: adopt the legacy config into the own folder (may fail in the
            // daemon's read-only sandbox - then it does not matter).
            std::string mlog;
            migrateLegacyConfig(&mlog);
            if (!mlog.empty()) {
                std::cerr << LOG << mlog;
                if (mlog.back() != '\n') std::cerr << std::endl;
            }
        }
        config_path_ = defaultConfigPath();
        loadConfig();
        modes_dir_ = modesDir();
        loadModeSet();
        // First resolution without device/program, so BACKEND/TICK_HZ from [global] are
        // known at startup.
        settings_ = profiles_.resolve("", "");
        match_desc_ = describeMatches(profiles_, "", "");

        std::string log;
        output_ = makeOutput(settings_.backend, &log);
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

        if (!activewin_.init())
            std::cerr << LOG << "Active window not available - [app:*] profiles inactive"
                      << std::endl;

        std::signal(SIGHUP, handleReloadSignal);
        std::signal(SIGTERM, handleQuitSignal);
        std::signal(SIGINT, handleQuitSignal);
        return true;
    }

    void run() {
        reader_.start();

        auto tick_period = periodFor(settings_.tick_hz);
        auto next_tick = std::chrono::steady_clock::now();
        auto last_tick = next_tick;
        int64_t next_win_poll = 0, next_cfg_poll = 0, next_debug = 0, next_susp_poll = 0;
        bool suspended = false;
        uint64_t seen_generation = 0;
        bool prev_enabled = true;  // for the one-time log per ENABLED change
        bool prev_idle = false;    // suspended or disabled in the previous tick
        int64_t next_devcount = 0;
        int devices_seen = 0;
        std::string hint_key;      // multi-device hint: once per change

        std::cerr << LOG << "Profile: " << sanitizeForLog(match_desc_) << " (tick rate " << settings_.tick_hz
                  << " Hz, mode " << sanitizeForLog(runner_->mode().id) << ")" << std::endl;

        while (!g_quit_requested) {
            next_tick += tick_period;
            std::this_thread::sleep_until(next_tick);
            if (g_quit_requested) break;

            const int64_t now_ns = nowNs();

            // --- Profile inputs: config, modes, device, active program ------
            bool need_resolve = false;
            bool modes_changed = false;
            if (g_reload_requested) {
                g_reload_requested = 0;
                std::cerr << LOG << "SIGHUP: reloading config and modes" << std::endl;
                loadConfig();
                loadModeSet();
                need_resolve = true;
                modes_changed = true;
                next_cfg_poll = now_ns + CONFIG_POLL_NS;
            } else if (now_ns >= next_cfg_poll) {
                next_cfg_poll = now_ns + CONFIG_POLL_NS;
                // Re-evaluate the path: e.g. the tray has meanwhile migrated the file to
                // the new folder while we were still on the legacy path.
                const std::string new_path = defaultConfigPath();
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
            if (gen != seen_generation) {
                seen_generation = gen;
                next_devcount = 0;  // recount right after a (re)connect
                std::string name = reader_.deviceName();
                warn_buttons_ = true;
                if (name != device_) {
                    device_ = name;
                    need_resolve = true;
                } else {
                    checkButtons();
                }
            }

            if (now_ns >= next_win_poll) {
                next_win_poll = now_ns + ACTIVEWIN_POLL_NS;
                std::string wc = activewin_.poll();
                if (wc != wm_class_) {
                    wm_class_ = wc;
                    need_resolve = true;
                }
            }

            if (need_resolve) {
                applyProfile(modes_changed);
                tick_period = periodFor(settings_.tick_hz);
            }

            // Count attached 3Dconnexion devices (cheap: every ~10 s or after connect).
            // spacenavd reports only one if there are several -> one-time hint per change.
            if (now_ns >= next_devcount) {
                next_devcount = now_ns + DEVCOUNT_POLL_NS;
                int c = countSpaceMice();
                devices_seen = c < 0 ? 0 : c;
                std::string key = std::to_string(devices_seen) + "|" + device_;
                if (devices_seen >= 2 && key != hint_key) {
                    std::string dn = device_.empty() ? "?" : sanitizeForLog(device_);
                    std::cerr << LOG << "Note: " << devices_seen
                              << " SpaceMice attached, spacenavd reports only '" << dn
                              << "' - profile [device:" << dn
                              << "] applies to all; buttons of both devices share the indices"
                              << std::endl;
                }
                hint_key = devices_seen >= 2 ? key : std::string();
            }

            // --- Time base --------------------------------------------------
            auto now = std::chrono::steady_clock::now();
            double dt = std::min(std::chrono::duration<double>(now - last_tick).count(), MAX_DT);
            // Drift guard: if we are far behind the plan (suspend, stall), do not catch
            // up on all missed ticks.
            if (now > next_tick + std::chrono::milliseconds(100)) next_tick = now;
            last_tick = now;

            // --- Read inputs ------------------------------------------------
            bool fresh = (nowNs() - reader_.last_motion_ns.load(std::memory_order_acquire)) <= STALE_NS;
            RawAxes raw{};
            if (fresh) {
                for (int i = 0; i < AX_COUNT; ++i)
                    raw[i] = reader_.axes[i].load(std::memory_order_relaxed);
            }
            uint32_t buttons = reader_.button_mask.load(std::memory_order_relaxed);

            // The tray evaluates the mouse itself (tab "Axes"/wizard): treat inputs as
            // "no deflection, no button" - held keys (click, Ctrl) are then released cleanly.
            if (now_ns >= next_susp_poll) {
                next_susp_poll = now_ns + SUSPEND_POLL_NS;
                bool want = suspendRequested();
                if (want != suspended) {
                    suspended = want;
                    std::cerr << LOG << (suspended ? "Output paused (tray axis display)"
                                                   : "Output active again") << std::endl;
                }
            }
            // Profile with ENABLED=0 (e.g. [app:blender]): same treatment, no output.
            const bool enabled = settings_.enabled;
            if (enabled != prev_enabled) {
                prev_enabled = enabled;
                if (enabled) std::cerr << LOG << "Control active again" << std::endl;
                else std::cerr << LOG << "Control disabled (profile " << sanitizeForLog(match_desc_)
                               << ")" << std::endl;
            }
            const bool idle = suspended || !enabled;
            if (idle) {
                fresh = false;
                raw = RawAxes{};
                buttons = 0;
                if (!prev_idle) {  // release everything held on entering suspend/disabled
                    runner_->releaseAll();
                    output_->flush();
                }
            }
            prev_idle = idle;

            status_.update("profile=" + sanitizeForLog(match_desc_) + "\ndevice=" + sanitizeForLog(device_) +
                               "\napp=" + sanitizeForLog(wm_class_) + "\nmode=" + sanitizeForLog(runner_->mode().id) +
                               "\nenabled=" + (enabled ? "1" : "0") +
                               "\npaused=" + (suspended ? "1" : "0") +
                               "\ndevices_seen=" + std::to_string(devices_seen) + "\n",
                           now_ns);

            // --- Channels, buttons, output (see mode_runner.hpp) ------------
            runner_->tick(settings_, raw, buttons, fresh, dt);

            if (debug_ && now_ns >= next_debug &&
                (raw[AX_X] || raw[AX_Y] || raw[AX_Z] || raw[AX_RX] || raw[AX_RY] || raw[AX_RZ])) {
                next_debug = now_ns + DEBUG_PERIOD_NS;
                const ModeRunner::Info &i = runner_->info();
                std::cerr << "h=" << i.rate_h << " v=" << i.rate_v << " twist=" << i.twist
                          << " lateral=" << i.lateral << " zoom=" << i.zooming
                          << (i.nav ? " nav" : "") << " mode=" << sanitizeForLog(runner_->mode().id)
                          << " profile=[" << sanitizeForLog(match_desc_) << "]" << std::endl;
            }
            output_->flush();
        }

        runner_->releaseAll();
        output_->releaseAll();
        output_->flush();
        status_.remove();
        std::cerr << LOG << "terminated" << std::endl;
    }

private:
    bool debug_;
    std::string config_path_;
    FileStamp stamp_;
    ProfileSet profiles_;
    ModeSet modes_;
    std::string modes_dir_, modes_stamp_;
    Settings settings_;
    std::string match_desc_;
    std::string backend_cfg_;
    std::string device_;
    std::string wm_class_;
    std::string warned_mode_;  // unknown mode id already warned about
    bool warn_buttons_ = false;

    DeviceReader reader_;
    ActiveWindow activewin_;
    StatusWriter status_;
    std::unique_ptr<Output> output_;
    std::unique_ptr<ModeRunner> runner_;

    static std::chrono::steady_clock::duration periodFor(int hz) {
        hz = std::clamp(hz, 30, 1000);
        return std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / hz));
    }

    void loadConfig() {
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
            std::cerr << LOG << "Config: " << w << std::endl;
    }

    void loadModeSet() {
        modes_stamp_ = modesStamp(modes_dir_);  // before reading, like the config stamp
        std::vector<std::string> warnings;
        modes_ = loadModes(modes_dir_, &warnings);
        for (const std::string &w : warnings)
            std::cerr << LOG << "Modes: " << sanitizeForLog(w) << std::endl;
    }

    // Picks the mode named by next.mode (unknown -> "normal", warned once), applies its
    // setting overrides to next and activates it in the runner. force: also re-activate
    // when the id is unchanged (the mode files were reloaded). Returns true if the
    // runner got a (new) mode.
    bool selectMode(Settings &next, bool force) {
        const ModeDef *m = modes_.find(next.mode);
        if (!m) {
            if (warned_mode_ != next.mode) {
                warned_mode_ = next.mode;
                std::cerr << LOG << "WARNING: unknown mode '" << sanitizeForLog(next.mode)
                          << "', using 'normal'" << std::endl;
            }
            m = &modes_.normal();
        }
        for (const auto &kv : m->overrides) {
            std::string err;
            if (!applyKey(next, kv.first, kv.second, &err))
                std::cerr << LOG << "Mode " << sanitizeForLog(m->id) << ": " << sanitizeForLog(err) << std::endl;
        }
        if (!force && runner_->mode().id == m->id) return false;
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
    void applyProfile(bool modes_changed) {
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
    void checkButtons() {
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
};

void printHelp() {
    std::cout <<
        "Usage: dofscroll [--debug] [--release-keys] [--help] [--version]\n"
        "\n"
        "Translates SpaceMouse motion (spacenavd/libspnav) into scroll, zoom,\n"
        "arrow-key and mouse-button events. Config: " << defaultConfigPath() << "\n"
        "(previous path: " << previousConfigPath() << ", adopted automatically)\n"
        "(profiles [global], [device:NAME], [app:WM_CLASS]). Modes: " << modesDir() << "\n"
        "\n"
        "  --debug    prints the evaluated channels, the zoom decision, the mode\n"
        "             and the active profile ~10x/s (stop the service first,\n"
        "             otherwise events are sent twice)\n"
        "  --release-keys  releases Ctrl, arrow keys and the left mouse button and\n"
        "             exits (ExecStopPost of the systemd unit: safeguard after a\n"
        "             crash/SIGKILL)\n"
        "  --help     this help\n"
        "  --version  print the version\n"
        "\n"
        "Signals: SIGHUP = reload config and modes (also automatic when the files\n"
        "change), SIGTERM/SIGINT = exit cleanly (held keys released).\n";
}

}  // namespace

// --release-keys: open an own output, release everything, exit.
static int releaseKeysAndExit() {
    ProfileSet ps;
    std::vector<std::string> warnings;
    loadProfiles(defaultConfigPath(), &ps, &warnings);
    std::string log;
    std::unique_ptr<Output> out = makeOutput(ps.resolve("", "").backend, &log);
    if (!out) {
        std::cerr << LOG << "--release-keys: no output possible" << std::endl;
        return 1;
    }
    out->forceReleaseAll();
    out->flush();
    return 0;
}

int main(int argc, char **argv) {
    // The window query runs in its own thread (own display), the output in the main
    // thread: Xlib must be made thread-safe before the first call.
    XInitThreads();
    bool debug = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--debug") debug = true;
        else if (a == "--help" || a == "-h") { printHelp(); return 0; }
        else if (a == "--version") { std::cout << VERSION << std::endl; return 0; }
        else if (a == "--release-keys") return releaseKeysAndExit();
        else {
            std::cerr << "dofscroll: unknown option '" << a << "' (see --help)" << std::endl;
            return 2;
        }
    }

    // Deliberately never destroyed: the detached reader thread accesses the DeviceReader
    // inside the daemon until process exit.
    // Also mirror log lines into the log file (deliberately never destroyed: detached
    // reader thread).
    if (!debug)
        std::cerr.rdbuf(new TeeLogBuf(std::cerr.rdbuf(), kLogMaxLines, kLogKeepLines));
    else
        std::cerr.rdbuf(new TeeLogBuf(std::cerr.rdbuf(), kLogMaxLinesDebug, kLogKeepLinesDebug));
    Daemon *daemon = new Daemon(debug);
    if (!daemon->initialize()) return 1;
    daemon->run();
    return 0;
}
