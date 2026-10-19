// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// dofscroll 2.0, Linux main: wires the platform-independent daemon core (daemon_core.hpp) to
// the Linux platform layer (src/platform/linux/): spacenavd/libspnav input, X11 or uinput
// output, X11 active window, SIGHUP reload, XDG paths and runtime files. The shared modules:
//   config.hpp      - settings/profiles ([global], [device:*], [app:*])
//   dsp.hpp         - channel signal processing, step accumulator, zoom detection
//   modes.hpp       - pluggable modes (loaded from the modes directory)
//   mode_runner.hpp - per-tick evaluation of the active mode (legacy and custom path)
//   daemon_core.hpp - reader thread, ticker, profile resolution, reload
#include <atomic>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <X11/Xlib.h>

// Xlib defines None as a macro, which clashes with ChannelActionType::None (modes.hpp).
#undef None

#include "common.hpp"
#include "config.hpp"
#include "daemon_core.hpp"
#include "device_count.hpp"
#include "modes.hpp"
#include "output.hpp"
#include "platform/linux/runtime_files.hpp"
#include "platform/platform.hpp"

using namespace spn;

namespace {

const char *const VERSION = "dofscroll 2.0";
const char *const LOG = "[dofscroll] ";

// Set by the quit signal handlers; the core's requestQuit() is a lock-free atomic store.
DaemonCore *g_core = nullptr;
void handleQuitSignal(int) {
    if (g_core) g_core->requestQuit();
}

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

    const PlatformPaths paths = platformPaths();

    // Also mirror log lines into the log file (deliberately never destroyed: the detached
    // reader thread may still log).
    std::cerr.rdbuf(makeTeeLogBuf(std::cerr.rdbuf(), paths.runtimeDir, debug));

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

    // Deliberately never destroyed: the detached reader thread accesses these until exit.
    std::string ilog;
    std::unique_ptr<InputSource> input = makeInputSource("auto", &ilog);
    if (!input) {
        std::cerr << LOG << "ERROR: no input source (" << ilog << ")" << std::endl;
        return 1;
    }
    InputSource *input_p = input.release();
    ActiveWindowProvider *win_p = makeActiveWindowProvider().release();
    ReloadListener *reload_p = makeReloadListener().release();
    StatusWriter *status = new StatusWriter(paths.runtimeDir);
    SuspendProbe *suspend = new SuspendProbe(paths.runtimeDir);

    DaemonDeps deps;
    deps.input = input_p;
    deps.activeWindow = win_p;
    deps.reload = reload_p;
    deps.paths = paths;
    deps.makeOutput = [](const std::string &b, std::string *l) { return makeOutput(b, l); };
    deps.configPath = [] { return defaultConfigPath(); };  // includes the legacy-path fallback
    deps.modesDir = [] { return modesDir(); };
    deps.suspendRequested = [suspend] { return (*suspend)(); };
    deps.writeStatus = [status](const std::string &b, int64_t n) { status->update(b, n); };
    deps.removeStatus = [status] { status->remove(); };
    // spacenavd reports only one device when several are attached: count them via /proc.
    deps.countDevices = [] { return countSpaceMice(); };
    deps.debug = debug;

    DaemonCore *core = new DaemonCore(std::move(deps));
    g_core = core;
    if (!core->initialize()) return 1;
    std::signal(SIGTERM, handleQuitSignal);
    std::signal(SIGINT, handleQuitSignal);
    core->run();
    return 0;
}
