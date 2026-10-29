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
//   INVERT_AXES = z ry             # axes whose direction is flipped before anything else (all modes); none = off
//   ZOOM_LATERAL = x z rx rz       # axes the zoom twist has to dominate
//   ZOOM_MODIFIER = ctrl           # key held while zooming (with the mouse wheel): ctrl, shift, alt, super, a
//                                  # combination such as ctrl+shift, or none (Cmd on macOS is super)
//   ZOOM_ENTER_MS = 40
//   ZOOM_EXIT_RATIO = 1.5
//   STEP_TRIGGER = 20              # level at which a step: channel action fires (steps/s)
//   BACKEND = auto                 # auto | x11 | uinput
//   INPUT = auto                   # auto | driver | hid; Windows and macOS only (ignored on Linux)
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
#include "modes.hpp"

namespace spn {

// Hard upper limits against resource exhaustion by manipulated files
// (docs/security.md). Valid configurations are far below them.
constexpr size_t kMaxConfigBytes = 1u << 20;   // file size (1 MiB)
// Action text (the value of a *_ACTION or BUTTON_<n> key, parseChannelAction / parseButtonAction): 32 KiB (was 256 bytes in the
// plan, 8 KiB by the line limit). A line must hold the longest action plus its key and a trailing comment.
constexpr size_t kMaxActionTextBytes = 32 * 1024;
constexpr size_t kMaxLineBytes = kMaxActionTextBytes + 1024;  // line length (also the line limit of mode files)
constexpr size_t kMaxPatternBytes = 4096;      // [app:...]/[device:...] name, in bytes
constexpr size_t kMaxSections = 256;           // sections incl. [global]
constexpr size_t kMaxKeysPerSection = 10000;   // unique keys per section (was 512)
constexpr size_t kMaxOwnFunctionsPerSection = 1000;  // FUNC_<name> own functions per section (stage 7e of docs/plan-motion-functions.md; was 32)
static_assert(kMaxOwnFunctionsPerSection <= kMaxKeysPerSection, "own functions are keys of their section");
// True when a section that already holds `count` unique keys cannot take another one.
inline bool sectionKeysFull(size_t count) { return count >= kMaxKeysPerSection; }
constexpr size_t kMaxMixTerms = 12;            // terms per MIX expression
constexpr size_t kMaxCurvePoints = 64;         // points per CURVE
constexpr size_t kMaxLateralAxes = 6;          // axes in ZOOM_LATERAL
constexpr double kMaxZoomExitRatio = 100.0;
constexpr double kMinStepTrigger = 0.5, kMaxStepTrigger = 10000.0;
// Key engine (stage 3 of docs/plan-motion-functions.md): at most this many key presses per tick and function, and per tick in total.
constexpr int kMaxPressesPerFunction = 4, kMaxPressesPerTick = 8;
// Largest accepted KEY_RATE_* value (presses per second); the hard cap min(100, tick_hz / 2) applies below it.
constexpr double kMaxKeyRate = 1000.0;
constexpr long kMinPriority = -1000, kMaxPriority = 1000;  // PRIORITY of a [device:]/[app:] profile
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
// (every rotation term, whatever the driver calls the axis: rx, ry or rz); weights and signs are kept
// ("x - rz" -> push "x", tilt "-rz"). A twist belongs to the zoom channel, not to a scroll mix. Either output
// may be null.
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

// The six motions that can drive a key or a chord, in the order of the key engine slots (the first four are SplitPart).
enum KeyMotion { KM_V_PUSH = 0, KM_V_TILT, KM_H_PUSH, KM_H_TILT, KM_TWIST, KM_LIFT, KM_COUNT };
// Per-motion tuning of the key functions `keys:` (analog) and `step:` (digital): <P>KEY_RATE_MIN, <P>KEY_RATE_MAX and
// <P>TRIGGER with P in SCROLL_V_PUSH_, SCROLL_V_TILT_, SCROLL_H_PUSH_, SCROLL_H_TILT_, ZOOM_ (twist), LIFT_. A value that is
// not set keeps today's behavior: no lower limit, no upper limit below the hard cap, STEP_TRIGGER. A channel that is not split
// uses the push values.
struct KeyTuning {
    enum Field : unsigned { F_RATE_MIN = 1, F_RATE_MAX = 2, F_TRIGGER = 4 };
    unsigned set = 0;        // bit mask of the fields below that were set
    double rate_min = 0.0;   // analog: lowest repeat rate in presses/s while the motion is deflected (0 = none)
    double rate_max = 0.0;   // analog: highest repeat rate (0 = unlimited, the hard cap min(100, tick_hz / 2) still applies)
    double trigger = 0.0;    // level (steps/s) of one press (digital) or of the first press (analog); 0 = STEP_TRIGGER / not used
};
// The 18 key tuning key names (SCROLL_V_PUSH_KEY_RATE_MIN ... LIFT_TRIGGER), in a stable order.
std::vector<std::string> keyTuningKeys();
// The key prefix of a motion ("SCROLL_V_PUSH_", ..., "ZOOM_", "LIFT_").
const char *keyTuningPrefix(int keyMotion);

// Keys held while a zoom action sends the mouse wheel (ZOOM_MODIFIER), as a bit mask. The bit values are the
// ones of the chord modifiers in modes.hpp.
constexpr unsigned kZoomModCtrl = 1, kZoomModShift = 2, kZoomModAlt = 4, kZoomModSuper = 8;
// "ctrl+shift" -> mask; "none" or an empty text -> 0. false + *err on an unknown name.
bool parseZoomModifier(const std::string &text, unsigned *out, std::string *err);
// The canonical text of a mask ("ctrl", "ctrl+shift", "none"), the inverse of parseZoomModifier.
std::string zoomModifierToString(unsigned mask);

// "z, ry" -> bit mask (bit = Axis); "none" or an empty text -> 0. false + *err on an unknown axis.
bool parseInvertAxes(const std::string &text, unsigned *out, std::string *err);
// "z ry" (axis order), "none" for 0.
std::string invertAxesToString(unsigned mask);

// The layers of the profile actions (Settings::actions), lowest first: [global], [device:*], [app:*]. The joystick mode
// sits between the device and the program layer (profile_actions.hpp).
constexpr int kLayerGlobal = 0, kLayerDevice = 1, kLayerApp = 2, kLayerCount = 3;

// The channel actions a PROFILE can set (V_ACTION, H_ACTION, TWIST_ACTION, LIFT_ACTION and the split keys V_PUSH_ACTION,
// V_TILT_ACTION, H_PUSH_ACTION, H_TILT_ACTION): the same keys, grammar and meaning as in a joystick mode file
// (modes.hpp). A key that is not set leaves the action to the joystick mode (profile_actions.hpp merges them).
struct ProfileActions {
    enum Chan { V = 0, H = 1, TWIST = 2, LIFT = 3 };
    static constexpr int kChanCount = 4;
    ChannelAction chan[kChanCount];
    bool chanSet[kChanCount] = {false, false, false, false};
    ChannelAction part[PART_COUNT];  // V push, V tilt, H push, H tilt
    bool partSet[PART_COUNT] = {false, false, false, false};
    bool any() const {
        for (bool b : chanSet) if (b) return true;
        for (bool b : partSet) if (b) return true;
        return false;
    }
};

struct Settings {
    ChannelCfg scroll_v;
    ChannelCfg scroll_h;
    ChannelCfg zoom;
    ChannelCfg lift;                  // LIFT_*: the lift / press motion (default mix "y", dead zone 80; LIFT_ACTION defaults to none)
    unsigned invert_axes = 0;         // INVERT_AXES: bit mask (bit = Axis) of the axes whose direction is flipped
    std::vector<Axis> zoom_lateral;   // default: x z rx rz
    unsigned zoom_modifier = kZoomModCtrl;  // ZOOM_MODIFIER: keys held while zooming
    int zoom_enter_ms = 40;
    double zoom_exit_ratio = 1.5;
    double step_trigger = 20.0;       // step: channel actions fire at this rate (steps/s after the channel processing)
    std::string backend = "auto";     // auto | x11 | uinput
    std::string input = "auto";       // auto | driver | hid (Windows/macOS input path; ignored on Linux)
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
    KeyTuning key_tuning[KM_COUNT];  // rate limits and trigger of the key functions per motion, see KeyTuning
    std::string mode = "normal";  // mode id (see modes.hpp)
    // Button bindings from BUTTON_<n> keys (n = 1..kMaxButtons): canonical action text as
    // accepted by parseButtonAction (none | click | rightclick | middleclick | nav | key:<name> | mode:<id> | mode:next | toggle:nav |
    // toggle:mode:<id>).
    // Which n are valid for a given device is decided by the UI, not here.
    std::map<int, std::string> buttons;
    // The channel actions set by profiles, per layer (kLayerGlobal, kLayerDevice below the joystick mode; kLayerApp
    // above it: the program has the last word, precedence global < device < mode < program).
    ProfileActions actions[kLayerCount];
    // BUTTON_<n> set by [app:*] sections (also in `buttons`): they win over the binding of the joystick mode.
    std::map<int, std::string> app_buttons;
    // The layer applyKey writes the action keys and BUTTON_<n> to (kLayerGlobal ... kLayerApp, see applyKeyLayer).
    int action_layer = kLayerGlobal;

    Settings();  // sets the defaults (see config.cpp): scroll_v "z + rx" sens 5
                 // dz 30 sign 1; scroll_h "x - rz" sens 3 dz 20 sign 1;
                 // zoom "ry" sens 5 dz 40 sign 1
};

using KV = std::pair<std::string, std::string>;

// Sets exactly one key (new and legacy names, key case-insensitive).
// false + *err on an unknown key or invalid value (Settings stays unchanged then).
bool applyKey(Settings &s, const std::string &key, const std::string &value, std::string *err);

// Like applyKey, but writes the channel actions and BUTTON_<n> to the given layer (kLayerGlobal, kLayerDevice: below
// the joystick mode; kLayerApp: above it). The single place that knows the layers; Settings::action_layer
// is only the transport of this parameter into applyKey and is kLayerGlobal outside these calls.
bool applyKeyLayer(Settings &s, const std::string &key, const std::string &value, int layer, std::string *err);
// Applies a key list at the given layer; invalid entries are skipped.
void applyKvList(Settings &s, const std::vector<KV> &kv, int layer);

// Complete equality of two resolved Settings: allKeys plus what allKeys reports only in collapsed form
// (the action layers and the program buttons). Used to decide that a profile change changes nothing.
bool sameSettings(const Settings &a, const Settings &b);

// All keys in canonical order (new names), for the [global] section.
std::vector<KV> allKeys(const Settings &s);
// Only keys where s differs from base (for [device:*]/[app:*] sections).
std::vector<KV> diffKeys(const Settings &base, const Settings &s);

struct Section {
    enum Kind { GLOBAL, DEVICE, APP } kind = GLOBAL;
    std::string pattern;              // for DEVICE/APP; empty for GLOBAL
    std::vector<KV> kv;               // raw keys as in the file (order preserved)
};
// The layer of the keys of a section kind.
int layerOfSection(Section::Kind kind);

// PRIORITY of a profile section (integer key, default 0, kMinPriority..kMaxPriority, no effect in [global]).
// Profiles of one kind are applied in ascending PRIORITY, later wins; see profile_files.hpp for the order.
int sectionPriority(const Section &s);

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
    void setSectionFromSettingsKeys(size_t i, const Settings &s);  // the part of setSectionFromSettings that rewrites kv
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

// The text of loadProfiles() for text already in memory (the profile files of profile_files.hpp are read
// through the bounded directory reader and parsed here). `path` is only the label in the warnings.
// Always true; *out is only set when non-null.
bool parseProfilesText(const std::string &data, const std::string &path, ProfileSet *out,
                       std::vector<std::string> *warnings);
// The text of one profile file: "[device:NAME]" or "[app:NAME]" and the key lines, as saveProfiles() writes
// a section. Same validation as saveProfiles(); a GLOBAL section is refused. false + *err when invalid.
bool renderProfileSection(const Section &sec, std::string *out, std::string *err);

}  // namespace spn
