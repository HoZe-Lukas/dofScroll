// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Configuration system: Settings (all values), profiles (sections) and resolution by
// device + active program. Used by the daemon AND the tray (one source of truth for the file
// format, no second parser copy).
//
// File format (~/.config/dofscroll/dofscroll.conf), INI-like, '#' and ';' = comment:
//
//   # Without a section = [global]. Old flat files stay valid.
//   SCROLL_V_MIX = z + rx          # expression: sum of [+-][weight*]axis
//   SCROLL_V_SENS = 5              # 0..255
//   SCROLL_V_DEADZONE = 30
//   SCROLL_V_SIGN = 1              # 1 or -1 (direction)
//   SCROLL_V_CURVE = 1.5           # exponent  OR  points "0:0 50:0.1 200:1 350:3"
//   SCROLL_V_SMOOTH_MS = 20        # smoothing (time constant), 0 = off
//   (likewise SCROLL_H_*, ZOOM_*)
//   ZOOM_LATERAL = x z rx rz       # axes the zoom twist has to dominate
//   ZOOM_ENTER_MS = 40
//   ZOOM_EXIT_RATIO = 1.5
//   STEP_TRIGGER = 20              # level at which a step: channel action fires (steps/s)
//   BACKEND = auto                 # auto | x11 | uinput
//   TICK_HZ = 250
//   MODE = normal                  # mode id (see modes.hpp), e.g. timeline in [app:resolve]
//   ENABLED = 1                    # 0 = daemon sends nothing (e.g. [app:blender])
//   NAV_BUTTON / NAV_BUTTON_ENABLED / CLICK_BUTTON / CLICK_BUTTON_ENABLED
//   BUTTON_<n> = click             # n = 1..64; none | click | rightclick | middleclick | nav |
//                                  # key:<name> (which n exist depends on the device: UI)
//
//   [device:SpaceMouse Pro]        # applies if the device name (spnav_dev_name)
//   SCROLL_V_SENS = 8              # contains this text (case-insensitive)
//
//   [app:firefox]                  # applies if WM_CLASS contains this text
//   ZOOM_SENS = 12                 # (case-insensitive); app after device
//
// Sections contain only the keys they override. Resolution order:
// [global] -> matching [device:*] -> (active mode's overrides) -> matching [app:*]
// (each in file order, later ones win).
//
// Legacy keys (still readable, the tray writes only the new ones):
//   SCROLL_SENSITIVITY_Y/X -> SCROLL_V_SENS / SCROLL_H_SENS
//   ZOOM_SENSITIVITY       -> ZOOM_SENS
//   SCROLL_DEADZONE_Y/X    -> SCROLL_V_DEADZONE / SCROLL_H_DEADZONE
//   ZOOM_DEADZONE          -> ZOOM_DEADZONE
//   INVERT_Y / INVERT_X    -> SCROLL_V_SIGN / SCROLL_H_SIGN (value 1 / -1, the sign directly)
//   ZOOM_INVERT            -> ZOOM_SIGN (ZOOM_INVERT=1 means SIGN=-1, 0 means 1)
//   RESPONSE_CURVE         -> sets the exponent of all three channels
#pragma once
#include <cstddef>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "common.hpp"

namespace spn {

// Hard upper limits against resource exhaustion by manipulated files
// (docs/security.md). Valid configurations are far below them.
constexpr size_t kMaxConfigBytes = 1u << 20;   // file size (1 MiB)
constexpr size_t kMaxLineBytes = 8192;         // line length
constexpr size_t kMaxPatternBytes = 4096;      // [app:...]/[device:...] name, in bytes
constexpr size_t kMaxSections = 256;           // sections incl. [global]
constexpr size_t kMaxKeysPerSection = 512;     // unique keys per section
constexpr size_t kMaxMixTerms = 12;            // terms per MIX expression
constexpr size_t kMaxCurvePoints = 64;         // points per CURVE
constexpr size_t kMaxLateralAxes = 6;          // axes in ZOOM_LATERAL
constexpr double kMaxZoomExitRatio = 100.0;
constexpr double kMinStepTrigger = 0.5, kMaxStepTrigger = 10000.0;
constexpr long kMaxButtons = 32;               // highest accepted BUTTON_<n> (the daemon button mask is 32 bits)

struct Term {
    Axis axis;
    double weight;  // incl. sign
};

// Output value = sum of weight*axis. Empty = channel always yields 0.
struct Mix {
    std::vector<Term> terms;
};

// Parses "z + rx", "x - rz", "0.5*rx + z", "-ry". Invalid -> false, *err set.
bool parseMix(const std::string &text, Mix *out, std::string *err);
std::string mixToString(const Mix &m);  // round-trip safe: parseMix(mixToString(m)) == m

// Splitting a scroll channel's mix into a push part (translation terms x, y, z) and a tilt part
// (rotation terms rx, rz); weights and signs are kept ("x - rz" -> push "x", tilt "-rz"). Twist
// terms (ry) belong to neither part and are dropped. Either output may be null.
void splitMix(const Mix &m, Mix *push, Mix *tilt);

struct ChannelCfg {
    Mix mix;
    int sensitivity = 5;          // 0..255, 0 = channel off
    int deadzone = 30;            // raw sensor units, >= 0
    int sign = 1;                 // +1 or -1
    double curve_exp = 1.0;       // >= 0.1; used if curve_points is empty
    // Optional freely defined curve: (input after deadzone in raw sensor
    // units, factor on the linear speed), ascending by input, linear
    // interpolation, clamped outside. Overrides curve_exp.
    std::vector<std::pair<double, double>> curve_points;
    double smooth_ms = 0.0;       // low-pass time constant, 0 = off, <= 500
};

// Per-part tuning of a split scroll channel (modes.hpp: V_PUSH_ACTION and friends). A part starts
// as a copy of its parent channel (sens, dead zone, sign, curve, smoothing); the keys
// SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|SIGN|CURVE|SMOOTH_MS> replace single values with ABSOLUTE
// values (same ranges and syntax as the parent keys, not multipliers). Only used while the active
// mode splits the channel.
enum SplitPart { PART_V_PUSH = 0, PART_V_TILT, PART_H_PUSH, PART_H_TILT, PART_COUNT };
struct PartTuning {
    enum Field : unsigned { F_SENS = 1, F_DEADZONE = 2, F_SIGN = 4, F_CURVE = 8, F_SMOOTH = 16 };
    unsigned set = 0;   // bit mask of the fields below that were set
    ChannelCfg cfg;     // only the fields named in `set` are meaningful (mix is unused)
};
// The 20 per-part tuning key names (SCROLL_V_PUSH_SENS ... SCROLL_H_TILT_SMOOTH_MS), in a stable order.
std::vector<std::string> partTuningKeys();
// The sub-channel config of a part: `parent` with the part's mix and the tuned fields.
ChannelCfg partConfig(const ChannelCfg &parent, const PartTuning &t, const Mix &partMix);

struct Settings {
    ChannelCfg scroll_v;
    ChannelCfg scroll_h;
    ChannelCfg zoom;
    std::vector<Axis> zoom_lateral;   // default: x z rx rz
    int zoom_enter_ms = 40;
    double zoom_exit_ratio = 1.5;
    double step_trigger = 20.0;       // step: channel actions fire at this rate (steps/s after the channel processing)
    std::string backend = "auto";     // auto | x11 | uinput
    int tick_hz = 250;                // 30..1000
    int nav_button = 0;
    bool nav_button_enabled = false;
    int click_button = 1;
    bool click_button_enabled = false;
    // Control active at all? false (typically in an [app:*] profile, e.g. Blender with its
    // own 3D navigation): the daemon then produces no output at all.
    bool enabled = true;
    // Publish the live feed (axes, buttons, channel outputs) to the UI while a client is connected.
    bool live_feed = true;
    PartTuning parts[PART_COUNT];  // split scroll channels, see PartTuning
    std::string mode = "normal";  // mode id (see modes.hpp)
    // Button bindings from BUTTON_<n> keys (n = 1..kMaxButtons): canonical action text as
    // accepted by parseButtonAction (none | click | rightclick | middleclick | nav | key:<name> | mode:<id> | mode:next | toggle:nav |
    // toggle:mode:<id>).
    // Which n are valid for a given device is decided by the UI, not here.
    std::map<int, std::string> buttons;

    Settings();  // sets the defaults (see config.cpp): scroll_v "z + rx" sens 5
                 // dz 30 sign 1; scroll_h "x - rz" sens 3 dz 20 sign 1;
                 // zoom "ry" sens 5 dz 40 sign 1
};

using KV = std::pair<std::string, std::string>;

// Sets exactly one key (new and legacy names, key case-insensitive).
// false + *err on an unknown key or invalid value (Settings stays unchanged then).
bool applyKey(Settings &s, const std::string &key, const std::string &value, std::string *err);

// All keys in canonical order (new names), for the [global] section.
std::vector<KV> allKeys(const Settings &s);
// Only keys where s differs from base (for [device:*]/[app:*] sections).
std::vector<KV> diffKeys(const Settings &base, const Settings &s);

struct Section {
    enum Kind { GLOBAL, DEVICE, APP } kind = GLOBAL;
    std::string pattern;              // for DEVICE/APP; empty for GLOBAL
    std::vector<KV> kv;               // raw keys as in the file (order preserved)
};

struct ProfileSet {
    std::vector<Section> sections;    // sections[0] is always GLOBAL

    ProfileSet();                     // only an empty GLOBAL section
    // Resolution for device + program (both may be empty).
    Settings resolve(const std::string &device_name, const std::string &wm_class) const;
    // Same, with the active mode's settings overrides (ModeDef::overrides) applied after the
    // matching [device:*] sections and before the matching [app:*] sections.
    // Precedence: global < device < mode overrides < app. nullptr = no overrides.
    Settings resolve(const std::string &device_name, const std::string &wm_class,
                     const std::vector<KV> *mode_overrides) const;
    // Settings of section i only (global: defaults + global kv; otherwise: base + the section's kv).
    Settings sectionSettings(size_t i) const;
    // Writes s back into section i (global: allKeys; DEVICE: diffKeys against global;
    // APP: deviations from global PLUS every key that a [device:*] section sets to a
    // value different from s - so an app profile can also enforce the global value
    // against a device override). Signature unchanged.
    void setSectionFromSettings(size_t i, const Settings &s);
    static constexpr size_t npos = static_cast<size_t>(-1);
    // Pattern valid? (non-empty after trim, at most kMaxPatternBytes, no control
    // character or line break - otherwise it does not survive save+load; ']' is fine).
    // *out = trimmed pattern.
    static bool normalizePattern(const std::string &pattern, std::string *out);
    // Finds/creates a section, returns its index. Invalid pattern (see
    // normalizePattern) -> npos, the caller MUST check. The pattern is trimmed.
    size_t findOrAdd(Section::Kind kind, const std::string &pattern);
    // Like findOrAdd but creates nothing: npos if absent/invalid.
    size_t find(Section::Kind kind, const std::string &pattern) const;
    void remove(size_t i);            // GLOBAL (i == 0) cannot be removed

private:
    // Search index (kind + lower-case pattern -> index) so that findOrAdd is O(1)
    // instead of O(n). Rebuilt lazily when the size of `sections` changes or on a
    // hit mismatch; please avoid changes other than push_back/erase from outside
    // (or maintain sections via findOrAdd/remove).
    mutable std::unordered_map<std::string, size_t> idx_;
    mutable size_t idx_n_ = static_cast<size_t>(-1);
    void rebuildIndex() const;
};

// Oldest location (project was then named "spnav-scroll"): $XDG_CONFIG_HOME (only if absolute,
// XDG spec) or $HOME resp. the passwd home + /.config, each + /spnav-scroll.conf
std::string legacyConfigPath();
// Previous location: <configdir>/spnav-scroll/spnav-scroll.conf
std::string previousConfigPath();
// New location: <configdir>/dofscroll/dofscroll.conf. Fallback: if the new file does not exist
// (lstat), the previous path, otherwise the oldest one is returned, if present (until
// migration). At most 3 lstat calls, cheap enough for 1x/s.
std::string defaultConfigPath();
// Idempotent best-effort migration old -> new. true if the new file exists afterwards
// (already there or migrated); false if there was nothing to migrate or it failed (*log =
// one-line reason, may be nullptr; on success a hint that the old file can be removed).
// Symlinks are carried over as symlinks (absolute target), regular files are copied (mode
// preserved, never overwrites an existing new file, max. kMaxConfigBytes). The old file is
// always left untouched.
bool migrateLegacyConfig(std::string *log);
// Missing file -> empty ProfileSet (defaults), true. Non-regular file (e.g. directory, FIFO)
// or file > kMaxConfigBytes -> false. Lines > kMaxLineBytes, sections > kMaxSections and
// keys > kMaxKeysPerSection are skipped with a warning; duplicate keys of a section are
// merged (last value, position of the last one). A UTF-8 BOM at the start of the file is
// ignored. Invalid lines/values are skipped and described in *warnings (if != nullptr).
bool loadProfiles(const std::string &path, ProfileSet *out, std::vector<std::string> *warnings);
// Writes atomically (temp next to the real target + fsync + rename). Symlinks are
// resolved and preserved, the file mode is preserved (new: 0600).
// Invalid section patterns, key/value with a line break, empty key or key with a
// leading '[', '#', ';' or containing '=', pairs rejected by applyKey, symlink
// loops (> 16 hops, ELOOP) -> false, nothing is written. Mode without
// setuid/setgid/sticky. Temp file with a random name in the same directory handle.
// Comments of the original file are lost (known limitation, stated in the file header).
bool saveProfiles(const std::string &path, const ProfileSet &ps, std::string *err);

}  // namespace spn
