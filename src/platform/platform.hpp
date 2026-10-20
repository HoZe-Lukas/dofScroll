// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Platform seams: everything the shared core needs from the operating system, as interfaces.
// The core (config, DSP, modes, profiles) never includes OS headers; each OS provides the
// factories declared at the bottom in src/platform/<os>/. See docs/port-architecture.md.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "../output.hpp"  // Output: wheel, keys, mouse buttons (already the right shape)

namespace spn {

// ---- Input ---------------------------------------------------------------------------------

struct DeviceInfo {
    int id = 0;               // stable for the lifetime of the process; 0 when the source merges devices
    std::string name;         // e.g. "3Dconnexion SpaceMouse Pro" (matched by [device:*] profiles)
    unsigned vendor = 0;      // USB vendor id, 0 if unknown (spacenavd does not report it)
    unsigned product = 0;     // USB product id, 0 if unknown
    int reportedButtons = 0;  // what the source reports; the UI uses factoryButtonCount() instead
    int axes = 6;
};

enum class InputEventType { Motion, Button, DevicesChanged };

// Axis order: x, y, z, rx, ry, rz. Values use the libspnav scale (about +-350 at full deflection
// for the shipped devices); HID backends normalize raw report values to this scale.
struct InputEvent {
    InputEventType type = InputEventType::Motion;
    int device = 0;                        // DeviceInfo::id
    std::array<int, 6> axes{};             // Motion: current deflection of all six axes
    int button = 0;                        // Button: 0-based hardware index (bit number in the device's button
                                           // mask); the core maps it to BUTTON_<n> with n = button + 1
    bool down = false;                     // Button: pressed or released
};

enum class WaitResult { Event, Timeout, Closed };

// Which way the SpaceMouse is read on Windows and macOS (config key INPUT): through the interface of
// the installed official 3Dconnexion driver, directly as raw HID, or automatically (src/platform/
// input_select.hpp). Linux reads through spacenavd and ignores it.
enum class InputMode { Auto, Driver, Hid };

class InputSource {
public:
    virtual ~InputSource() = default;
    virtual const char *name() const = 0;               // "spacenavd", "hid", "iokit", ...
    virtual bool open(std::string *err) = 0;
    // Blocks up to timeoutMs for the next event (reader thread). Closed: source gone for good.
    virtual WaitResult wait(InputEvent *out, int timeoutMs) = 0;
    virtual std::vector<DeviceInfo> devices() const = 0;  // current snapshot
    virtual void close() = 0;
    // Optional (Windows and macOS hybrid input): applies the configured INPUT mode; may be called at any time
    // from the main thread (the reader thread picks it up). Other sources ignore it.
    virtual void setInputMode(InputMode) {}
    // Optional: id of the path the data comes from right now, "driver" or "hid"; empty when the source
    // has no such choice (Linux) or none is active yet. Thread-safe.
    virtual std::string pathLabel() const { return std::string(); }
    // True while a matching device exists but opening it was refused (another program, possibly the
    // vendor driver, holds it). Default: never. See platform/input_diag.hpp.
    virtual bool accessBlocked() const { return false; }
};

// ---- Active window ---------------------------------------------------------------------------

// Returns the string that [app:*] profiles are matched against (Linux: WM_CLASS, Windows: process
// executable name, macOS: bundle identifier or app name). Empty string = unknown.
class ActiveWindowProvider {
public:
    virtual ~ActiveWindowProvider() = default;
    virtual bool init() = 0;
    virtual std::string poll() = 0;
};

// ---- Paths ----------------------------------------------------------------------------------

struct PlatformPaths {
    std::string configDir;   // user config directory incl. the "dofscroll" folder (0700 / user only)
    std::string runtimeDir;  // status, suspend and log files (user only)
};
// configDir is never empty; runtimeDir may be empty when the OS offers no safe per-user runtime
// directory (Linux without XDG_RUNTIME_DIR): the daemon then runs without status/suspend/log files.
// Creates nothing.
PlatformPaths platformPaths();

// ---- Reload channel (replaces SIGHUP) --------------------------------------------------------
// Contract: the writer replaces the config atomically, then calls requestReload(); the daemon's
// listener reports it once, and the daemon re-reads the file on its main thread. Idempotent.

class ReloadListener {
public:
    virtual ~ReloadListener() = default;
    virtual bool init(std::string *err) = 0;
    virtual bool pollReload() = 0;  // true once per request since the last call, non-blocking
};
bool requestReload(std::string *err);  // tray -> daemon

// ---- Factories (defined per OS in src/platform/<os>/) -----------------------------------------

// backend: "auto" or an explicit name; `log` receives a one-line choice/failure note (may be null)
std::unique_ptr<InputSource> makeInputSource(const std::string &backend, std::string *log);
std::unique_ptr<ActiveWindowProvider> makeActiveWindowProvider();
std::unique_ptr<ReloadListener> makeReloadListener();

}  // namespace spn
