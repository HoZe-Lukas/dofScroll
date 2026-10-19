// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll for macOS: wires the platform-independent daemon core (daemon_core.hpp) to the macOS
// platform layer (src/platform/mac/): IOHID input, CGEvent output, NSWorkspace active application,
// Unix-socket reload, ~/Library paths. Same structure as src/dofscroll.cpp (Linux).
#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hidsystem/IOHIDLib.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <streambuf>
#include <string>
#include <thread>

#include "../../common.hpp"
#include "../../config.hpp"
#include "../../daemon_core.hpp"
#include "../../log_sanitize.hpp"
#include "../../live_feed/live_feed.hpp"
#include "../../modes.hpp"
#include "../../output.hpp"
#include "../fs.hpp"
#include "../platform.hpp"
#include "../suspend_probe.hpp"
#include "mac_output.hpp"
#include "mac_paths.hpp"
#include "mac_power.hpp"

using namespace spn;

namespace {

const char *const VERSION = "dofscroll 2.0 (macOS)";
const char *const LOG = "[dofscroll] ";

DaemonCore *g_core = nullptr;
// SIGTERM/SIGINT/SIGHUP only set a flag: the main loop ends, run() releases the held keys through
// the normal path and the process exits (nothing unsafe happens in the handler itself).
void handleQuitSignal(int) {
    if (g_core) g_core->requestQuit();
}

// Last resort for std::terminate and exit(): release what this process pressed. Not reachable
// after SIGKILL or a hard crash: the next start releases everything (forceReleaseAll in
// DaemonCore::initialize).
void releaseHeldKeys() { macReleaseGuard().releaseAll(); }
[[noreturn]] void onTerminate() {
    releaseHeldKeys();
    std::abort();
}

void printHelp(const PlatformPaths &p) {
    std::cout <<
        "Usage: dofscroll [--debug] [--release-keys] [--help] [--version]\n"
        "\n"
        "Translates SpaceMouse motion (raw HID) into scroll, zoom, arrow-key and mouse-button events.\n"
        "Config: " << p.configDir << "/dofscroll.conf  Modes: " << p.configDir << "/modes\n"
        "(profiles [global], [device:NAME], [app:BUNDLE_ID_OR_APP_NAME], matched lowercase)\n"
        "\n"
        "  --debug         print the evaluated channels, mode and profile ~10x/s\n"
        "  --release-keys  release Ctrl, arrow keys and mouse buttons and exit\n"
        "  --help          this help\n"
        "  --version       print the version\n"
        "\n"
        "Needs two permissions (System Settings > Privacy & Security): Accessibility (to post\n"
        "events) and Input Monitoring (to read the device). SIGTERM/SIGINT exit cleanly.\n";
}

// ---- permissions -------------------------------------------------------------------------------

bool accessibilityGranted() { return AXIsProcessTrusted(); }

// IOHIDCheckAccess exists from macOS 10.15 on: looked up at run time (no __builtin_available(),
// which cppcheck cannot parse), like mac_hid_input.cpp does.
bool inputMonitoringGranted() {
    using CheckFn = uint32_t (*)(uint32_t);
    const CheckFn check = reinterpret_cast<CheckFn>(dlsym(RTLD_DEFAULT, "IOHIDCheckAccess"));
    if (!check) return true;  // older systems have no such permission
    return check(static_cast<uint32_t>(kIOHIDRequestTypeListenEvent)) == static_cast<uint32_t>(kIOHIDAccessTypeGranted);
}

// ---- runtime files -----------------------------------------------------------------------------

// Private runtime directory: created 0700, must belong to the current user and not be group or
// other writable, otherwise nothing is written there.
bool prepareRuntimeDir(const std::string &dir) {
    if (dir.empty()) return false;
    std::string err;
    if (!fs::mkdirsPrivate(dir, &err)) return false;
    struct stat st;
    return ::lstat(dir.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == ::geteuid() && (st.st_mode & 022) == 0;
}

// Mirrors everything written to std::cerr into <runtimeDir>/dofscroll.log (local-time stamp at the
// start of each line), rolling like the Linux log: when kLogMaxLines (debug: kLogMaxLinesDebug)
// lines or kLogMaxBytes are exceeded, only the newest kLogKeepLines remain (file replaced
// atomically). Errors only switch the mirroring off. Deliberately never destroyed: the detached
// reader thread may still log.
class TeeLogBuf : public std::streambuf {
public:
    TeeLogBuf(std::streambuf *orig, const std::string &dir, bool debug)
        : orig_(orig),
          maxLines_(debug ? kLogMaxLinesDebug : kLogMaxLines),
          keepLines_(debug ? kLogKeepLinesDebug : kLogKeepLines) {
        if (!prepareRuntimeDir(dir)) return;
        path_ = logFilePathIn(dir);
        if (!openAppend()) return;
        struct stat st;
        if (::fstat(fd_, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != ::geteuid() || (st.st_mode & 077) != 0) {
            disable();
            return;
        }
        size_ = static_cast<size_t>(st.st_size);
        std::string text;
        fs::ReadOptions opt;
        opt.refuseLinks = true;
        if (fs::readFileBounded(path_, kLogMaxBytes * 4, opt, &text).ok()) lines_ = countLines(text);
        else lines_ = 0;
        if (size_ > kLogMaxBytes || lines_ > maxLines_) rewrite();
    }

protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) return traits_type::not_eof(c);
        char ch = traits_type::to_char_type(c);
        return xsputn(&ch, 1) == 1 ? c : traits_type::eof();
    }
    std::streamsize xsputn(const char *s, std::streamsize n) override {
        const std::streamsize r = orig_->sputn(s, n);
        std::lock_guard<std::mutex> lk(m_);
        if (fd_ >= 0 && pending_.size() < 64 * 1024) {
            for (std::streamsize i = 0; i < n; ++i) {
                if (atLineStart_) {
                    char ts[32];
                    const std::time_t t = std::time(nullptr);
                    std::tm tmv{};
                    ::localtime_r(&t, &tmv);
                    if (std::strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S ", &tmv) > 0) pending_ += ts;
                    atLineStart_ = false;
                }
                pending_ += s[i];
                if (s[i] == '\n') atLineStart_ = true;
            }
        }
        return r;
    }
    int sync() override {
        const int r = orig_->pubsync();
        std::lock_guard<std::mutex> lk(m_);
        flushPending();
        return r;
    }

private:
    std::streambuf *orig_;
    size_t maxLines_, keepLines_;
    std::mutex m_;
    int fd_ = -1;
    size_t size_ = 0, lines_ = 0;
    std::string path_, pending_;
    bool atLineStart_ = true;

    bool openAppend() {
        fd_ = ::open(path_.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600);
        return fd_ >= 0;
    }
    void disable() {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
        pending_.clear();
    }
    void rewrite() {
        std::string text;
        fs::ReadOptions opt;
        opt.refuseLinks = true;
        if (!fs::readFileBounded(path_, kLogMaxBytes * 4, opt, &text).ok()) text.clear();
        const std::string kept = trimLogText(text, keepLines_, kLogMaxBytes / 2);
        std::string err;
        if (!fs::writeFileReplace(path_, kept, &err)) {
            disable();
            return;
        }
        ::close(fd_);
        if (!openAppend()) {
            fd_ = -1;
            return;
        }
        size_ = kept.size();
        lines_ = countLines(kept);
    }
    void flushPending() {
        if (fd_ < 0 || pending_.empty()) {
            pending_.clear();
            return;
        }
        const char *p = pending_.data();
        size_t left = pending_.size();
        while (left > 0) {
            const ssize_t w = ::write(fd_, p, left);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                disable();
                return;
            }
            p += w;
            left -= static_cast<size_t>(w);
            size_ += static_cast<size_t>(w);
        }
        lines_ += countLines(pending_);
        pending_.clear();
        if (lines_ > maxLines_ || size_ > kLogMaxBytes) rewrite();
    }
};

// Status file (format: common.hpp) replaced atomically; errors are never fatal.
class StatusWriter {
public:
    StatusWriter(const std::string &dir, std::string permissionsLine)
        : path_(prepareRuntimeDir(dir) ? statusFilePathIn(dir) : std::string()), perm_(std::move(permissionsLine)) {}

    // Called from the permission watcher thread: the next update() rewrites the file.
    void setPermissions(std::string line) {
        std::lock_guard<std::mutex> lk(m_);
        if (line != perm_) {
            perm_ = std::move(line);
            next_ = 0;
            last_.clear();
        }
    }

    void update(const std::string &body, int64_t nowNs) {
        std::lock_guard<std::mutex> lk(m_);
        if (path_.empty()) return;
        if (body == last_ && nowNs < next_) return;
        last_ = body;
        next_ = nowNs + 1'000'000'000;
        const std::string text = body + perm_ + "updated=" + std::to_string(static_cast<long long>(std::time(nullptr))) + "\n";
        if (text.size() > kStatusMaxBytes) return;
        std::string err;
        if (fs::writeFileReplace(path_, text, &err)) {
            wrote_ = true;
        } else if (!warned_) {
            warned_ = true;
            std::cerr << LOG << "WARNING: status file not writable (" << err << ") - tray display limited" << std::endl;
        }
    }
    void remove() {
        std::lock_guard<std::mutex> lk(m_);
        if (!path_.empty() && wrote_) fs::removeFile(path_);
    }

private:
    std::mutex m_;
    std::string path_, perm_, last_;
    int64_t next_ = 0;
    bool warned_ = false, wrote_ = false;
};

// --release-keys: open an own output, release everything, exit.
int releaseKeysAndExit(const PlatformPaths &paths) {
    ProfileSet ps;
    std::vector<std::string> warnings;
    loadProfiles(paths.configDir + "/dofscroll.conf", &ps, &warnings);
    std::string log;
    std::unique_ptr<Output> out = makeOutput(ps.resolve("", "").backend, &log);
    if (!out) {
        std::cerr << LOG << "--release-keys: no output possible (" << log << ")" << std::endl;
        return 1;
    }
    out->forceReleaseAll();
    out->flush();
    return 0;
}

// Re-checks the permissions every few seconds (they can be revoked or granted while the daemon
// runs), reports changes in the status file and, rate-limited, in the log.
void startPermissionWatcher(StatusWriter *status, PermissionTracker tracker) {
    std::thread([status, tracker]() mutable {
        for (;;) {
            const bool ax = accessibilityGranted();
            const bool im = inputMonitoringGranted();
            const PermissionTracker::Result r = tracker.update(ax, im, static_cast<long long>(std::time(nullptr)));
            if (r.changed) status->setPermissions(permissionsStatusLine(ax, im));
            if (r.log) {
                if (ax && im) std::cerr << LOG << "permissions OK" << std::endl;
                else std::cerr << LOG << "WARNING: " << permissionHint(ax, im) << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }).detach();
}

}  // namespace

int main(int argc, char **argv) {
    const PlatformPaths paths = platformPaths();
    bool debug = false;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--debug") debug = true;
        else if (a == "--help" || a == "-h") { printHelp(paths); return 0; }
        else if (a == "--version") { std::cout << VERSION << std::endl; return 0; }
        else if (a == "--release-keys") return releaseKeysAndExit(paths);
        else {
            std::cerr << "dofscroll: unknown option '" << a << "' (see --help)" << std::endl;
            return 2;
        }
    }

    // Deliberately never destroyed: the detached reader thread logs and reads until exit.
    std::cerr.rdbuf(new TeeLogBuf(std::cerr.rdbuf(), paths.runtimeDir, debug));

    const bool ax = accessibilityGranted();
    const bool im = inputMonitoringGranted();
    if (!ax || !im) std::cerr << LOG << "WARNING: " << permissionHint(ax, im) << std::endl;

    std::string ilog;
    std::unique_ptr<InputSource> input = makeInputSource("auto", &ilog);
    if (!input) {
        std::cerr << LOG << "ERROR: no input source (" << ilog << ")" << std::endl;
        return permanentErrorExitCode(std::getenv("XPC_SERVICE_NAME"));  // retrying cannot help
    }
    if (!ilog.empty()) std::cerr << LOG << ilog << std::endl;
    InputSource *input_p = input.release();
    ActiveWindowProvider *win_p = makeActiveWindowProvider().release();
    ReloadListener *reload_p = makeReloadListener().release();
    StatusWriter *status = new StatusWriter(paths.runtimeDir, permissionsStatusLine(ax, im));

    DaemonDeps deps;
    deps.input = input_p;
    deps.activeWindow = win_p;
    deps.reload = reload_p;
    deps.paths = paths;
    deps.makeOutput = [](const std::string &b, std::string *l) { return makeOutput(b, l); };
    deps.configPath = [paths] { return paths.configDir + "/dofscroll.conf"; };
    deps.modesDir = [paths] { return paths.configDir + "/modes"; };
    deps.writeStatus = [status](const std::string &b, int64_t n) { status->update(b, n); };
    deps.removeStatus = [status] { status->remove(); };
    deps.suspendRequested = FileSuspendProbe(paths.runtimeDir);  // UI axis assistant pauses the output
    deps.liveFeed = makeLiveFeedSink(paths.runtimeDir);  // UI data feed, see src/live_feed/
    deps.debug = debug;

    DaemonCore *core = new DaemonCore(std::move(deps));
    g_core = core;
    std::signal(SIGTERM, handleQuitSignal);
    std::signal(SIGINT, handleQuitSignal);
    std::signal(SIGHUP, handleQuitSignal);
    std::set_terminate(onTerminate);
    std::atexit(releaseHeldKeys);
    if (!core->initialize()) {
        releaseHeldKeys();
        return permanentErrorExitCode(std::getenv("XPC_SERVICE_NAME"));  // config/output problem: not transient
    }
    startSleepWatcher();
    startPermissionWatcher(status, PermissionTracker());
    core->run();
    releaseHeldKeys();  // run() already did through the clean path; this is the safety net
    return 0;
}
