// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// SettingsModel: read, edit and save the user's configuration for the settings window, on top of
// the config library (ProfileSet with [global], [device:*] and [app:*] sections). No toolkit.
//
//   SettingsModel m;
//   m.setReloadCallback(...);                  // e.g. ActionRunner::requestReload
//   m.load(path, &warnings, &err);
//   m.setDeviceInfo(deviceName, reportedButtons);  // limits the button rows (factoryButtonCount)
//   for (auto &s : m.sections()) ...           // index 0 is [global]
//   m.value(i, "SCROLL_V_SENS", &v);           // v.text, v.overridden, v.inherited
//   m.setValue(i, "SCROLL_V_SENS", "7", &err); // validated by applyKey; section keeps only overrides
//   m.clearOverride(i, "SCROLL_V_SENS");       // back to the inherited value (not for [global])
//   m.setButton(i, 3, "key:space", &err);      // BUTTON_3, 1 <= n <= buttonLimit()
//   m.setValue(i, "MODE", "timeline", &err);   // the MODE key like any other (also in [global])
//   m.validate(strings) -> error strings; m.save() -> atomic write, then the reload callback.
//
// Mode rules (the settings page is mode-aware):
//  * MODE can be chosen in every profile, [global] included, as a dropdown: modeOptions(i) is the
//    ordered option list (for [device:*] and [app:*] entry 0 "Default mode" = MODE not set,
//    inherited; for [global] there is no such entry because the global mode is the base; then all
//    loaded modes in load order, built-ins first, with display name and the id as secondary text;
//    an id that is set but not loaded is appended), selectedModeOption(i) the current index and
//    setMode(i, optionIndex) writes MODE, or clears it for entry 0 (never for [global]). Exactly
//    one mode is active.
//  * The effective mode of a section is its own MODE, else the inherited one (global < device <
//    mode overrides < app as in the daemon; which device/app combination is active at runtime is
//    unknown here, so an app section only looks at itself and [global]). The mode of [global] is
//    the base for every profile that does not choose its own; the overrides of that mode apply on
//    top of the [global] values.
//  * Channel settings (mix, sensitivity, deadzone, curve, smoothing, sign, zoom detection) are the
//    same in every mode: globalChannelGroups() lists them as an always-available group set. They
//    belong to a profile: every profile, [global] included, can hold them (index i). Start at login,
//    language and the axes assistant are not part of profile sections.
//  * visibleSettings(i) returns only the mode-dependent parts of the page of a profile: the
//    "mode" group for every profile and the "general" (ENABLED) group for [device:*]/[app:*], the "buttons" group (the BUTTON_<n> rows) while all three channel actions of the mode are "default"
//    (the normal path), and for a non-default mode its channel actions and button bindings as
//    read-only ModeInfo ("Edit mode file" is editModeFile() through the ActionRunner).
//    Per-profile overrides of channel values stay possible: Visible::overrides lists the
//    overridden channel keys (empty by default; the view shows it collapsed), addableOverrides
//    the keys relevant for the mode that can be added with addOverride (starts with the inherited
//    value) and removed with clearOverride. Candidates: channels whose mode action is not "none";
//    zoom detection only while the twist channel is "default".
//  * setValue/setMode/clearOverride report through layoutChanged whether the effective mode of
//    the section changed, so the view rebuilds the page (no signals).
//
// Keys are the canonical names of config.hpp (SCROLL_V_/SCROLL_H_/ZOOM_ MIX, SENS, DEADZONE, SIGN,
// CURVE, SMOOTH_MS; ZOOM_MODIFIER, ZOOM_LATERAL, ZOOM_ENTER_MS, ZOOM_EXIT_RATIO, BACKEND, INPUT, TICK_HZ, ENABLED,
// MODE); BUTTON_<n> goes through setButton. NAV_BUTTON[_ENABLED] and CLICK_BUTTON[_ENABLED] are
// deprecated: the model does not offer them any more (visibleSettings, the overrides list) and
// migrateLegacyButtons() turns a file that still uses them into BUTTON_<n> entries.
// Values are stored in canonical form (what allKeys would write). An "override" is a key present
// in the section; the inherited value is the one of [global].
#pragma once
#include <algorithm>
#include <cctype>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "utf8.hpp"
#include "device_db.hpp"
#include "mode_list.hpp"
#include "modes.hpp"
#include "ui/model/action_runner.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

class SettingsModel {
public:
    struct SectionInfo {
        size_t index;
        Section::Kind kind;
        std::string pattern;  // empty for [global]
        std::string title;    // "[global]", "[device:X]", "[app:X]" (not translated)
    };
    struct Value {
        std::string text;       // effective value in this section
        bool overridden = false;  // the section sets the key itself ([global]: key present in the file)
        std::string inherited;    // value without the override (defaults for [global])
    };
    struct ButtonRow {
        int number;           // 1-based
        std::string action;   // "none", "click", ..., "key:space"
        bool overridden;      // set in this section
        bool beyondLimit;     // bound but above buttonLimit() (kept, not editable via setButton)
    };
    struct SaveResult {
        bool saved = false;     // file written
        bool reloaded = false;  // reload callback called and succeeded (true also without callback)
        std::string error;      // validation errors (joined), write error or reload error
    };
    struct Group {
        std::string id;     // "general", "scroll_v", "scroll_h", "zoom", "zoom_detect", "buttons", "mode"
        std::string title;  // English source string for the view's translation
        std::vector<std::string> keys;  // editable setting keys, in display order
        bool buttonRows = false;        // group "buttons": the BUTTON_<n> rows (buttonRows()) belong here
    };
    // Read-only description of a non-default mode.
    struct ModeInfo {
        std::string id, label, description;
        std::string v, h, twist;                           // channel actions as in mode files
        std::vector<std::pair<int, std::string>> buttons;  // number -> action text
        bool hasFile = false;                              // editModeFile possible
    };
    struct Visible {
        std::string modeId;          // effective mode of the section
        bool isDefaultMode = true;   // "normal" (always for [global])
        bool modeLoaded = true;      // false: the mode id names no loaded mode (treated like normal)
        bool canChooseMode = false;  // [device:*] / [app:*]
        std::vector<Group> groups;
        ModeInfo mode;               // filled when !isDefaultMode && modeLoaded
        Group overrides;             // keys this section overrides (id "overrides"; empty for [global])
        std::vector<std::string> addableOverrides;  // channel keys that can be added with addOverride
    };
    struct ModeOption {
        std::string id;         // "" = "Default mode" (MODE not set)
        std::string label;      // display name; entry 0: English source "Default mode" (translate in the view)
        std::string secondary;  // the id, "" for entry 0
        bool known = true;      // false: id set in the file but no such mode loaded
        std::string description;  // DESCRIPTION of the mode ("" for entry 0 and unknown ids)
    };
    using ReloadFn = std::function<bool(std::string *err)>;

    void setReloadCallback(ReloadFn f) { reload_ = std::move(f); }

    // Loads the file (missing file = defaults). false + *err if it cannot be read.
    bool load(const std::string &path, std::vector<std::string> *warnings, std::string *err) {
        ProfileSet ps;
        if (!loadProfiles(path, &ps, warnings)) {
            if (err) *err = "cannot read " + path;
            return false;
        }
        ps_ = ps;
        path_ = path;
        dirty_ = false;
        return true;
    }
    // Takes an already loaded set (tests, other sources).
    void setProfiles(const ProfileSet &ps, const std::string &path) {
        ps_ = ps;
        path_ = path;
        dirty_ = false;
    }
    const ProfileSet &profiles() const { return ps_; }
    const std::string &path() const { return path_; }
    bool dirty() const { return dirty_; }

    // ---- device / button limit ----
    // Number of editable buttons: factoryButtonCount(name, reported) if plausible (1..kMaxButtons),
    // otherwise kMaxButtons (device unknown).
    void setDeviceInfo(const std::string &deviceName, int reportedButtons) {
        const int n = factoryButtonCount(deviceName, reportedButtons);
        limit_ = n >= 1 && n <= kMaxButtons ? n : int(kMaxButtons);
    }
    int buttonLimit() const { return limit_; }
    // Highest BUTTON_<n> a section can hold. [device:NAME]: the factory count of that model if it is in the
    // device table, otherwise the hard limit (32). [global] and [app:*] apply to every device, so they use
    // the connected device's count (buttonLimit(), 32 while unknown): a binding there is meant for the
    // devices that have the button, and each device section is checked against its own count.
    int sectionButtonLimit(size_t i) const {
        if (i < ps_.sections.size() && ps_.sections[i].kind == Section::DEVICE) {
            const int n = factoryButtonCount(ps_.sections[i].pattern, 0);
            return n >= 1 && n <= kMaxButtons ? n : int(kMaxButtons);
        }
        return limit_;
    }

    // ---- modes ----
    struct ChanPrefix { const char *id, *title, *pre; };
    static const std::vector<ChanPrefix> &channelPrefixes() {
        static const std::vector<ChanPrefix> c = {{"scroll_v", "Vertical scroll", "SCROLL_V_"},
                                                  {"scroll_h", "Horizontal scroll", "SCROLL_H_"},
                                                  {"zoom", "Twist / zoom", "ZOOM_"}};
        return c;
    }
    void setModes(const ModeSet &ms, const std::string &modesDir) {
        modes_ = ms;
        modesDir_ = modesDir;
    }
    // Effective mode id of section i (the MODE of [global], "normal" when it sets none).
    std::string effectiveMode(size_t i) const {
        if (i >= ps_.sections.size()) return "normal";
        const std::string id = ps_.sectionSettings(i).mode;
        return validModeId(id) ? id : std::string("normal");
    }
    // The loaded joystick mode with this id (nullptr if none): its description and channel actions.
    const ModeDef *modeDef(const std::string &id) const { return modes_.find(id); }
    // Dropdown options for the mode of section i (empty for invalid indices). [device:*] and
    // [app:*] start with the entry "Default mode" (MODE not set); [global] has no such entry.
    std::vector<ModeOption> modeOptions(size_t i) const {
        std::vector<ModeOption> out;
        if (i >= ps_.sections.size()) return out;
        if (i > 0) out.push_back({"", "Same as Global", "", true, ""});
        for (const ModeDef &m : modes_.modes) out.push_back({m.id, m.label.empty() ? m.id : m.label, m.id, true, m.description});
        if (hasKey(ps_.sections[i], "MODE")) {
            const std::string cur = ps_.sectionSettings(i).mode;
            bool found = false;
            for (const ModeOption &o : out) found = found || (!o.id.empty() && o.id == cur);
            if (!found) out.push_back({cur, cur, cur, false, ""});
        }
        return out;
    }
    // Index into modeOptions(i): 0 if MODE is not set in a [device:*]/[app:*] section; for [global]
    // the index of its mode ("normal" when it sets none); -1 for invalid indices or no modes.
    int selectedModeOption(size_t i) const {
        if (i >= ps_.sections.size()) return -1;
        const std::vector<ModeOption> o = modeOptions(i);
        if (i > 0 && !hasKey(ps_.sections[i], "MODE")) return 0;
        const std::string cur = ps_.sectionSettings(i).mode;
        for (size_t k = i > 0 ? 1 : 0; k < o.size(); ++k)
            if (o[k].id == cur) return int(k);
        return i > 0 ? 0 : (o.empty() ? -1 : 0);
    }
    // Picks an option: 0 clears MODE (inherit; [device:*]/[app:*] only), otherwise writes that mode's id.
    bool setMode(size_t i, int optionIndex, std::string *err, bool *layoutChanged = nullptr) {
        const std::vector<ModeOption> o = modeOptions(i);
        if (layoutChanged) *layoutChanged = false;
        if (o.empty()) return failb(err, "no such profile or no joystick modes loaded");
        if (optionIndex < 0 || size_t(optionIndex) >= o.size()) return failb(err, "no such joystick mode option");
        if (i > 0 && optionIndex == 0) return clearOverride(i, "MODE", layoutChanged);
        return setValue(i, "MODE", o[size_t(optionIndex)].id, err, layoutChanged);
    }
    // Channel settings are the same in every mode: always available, edited in [global] (index 0),
    // independent of the selected profile or mode.
    static std::vector<Group> globalChannelGroups() {
        std::vector<Group> g;
        for (const auto &c : channelPrefixes()) {
            Group x{c.id, c.title, {}, false};
            for (const char *k : {"MIX", "SENS", "DEADZONE", "SIGN", "CURVE", "SMOOTH_MS"})
                x.keys.push_back(std::string(c.pre) + k);
            g.push_back(x);
        }
        g.push_back({"zoom_detect", "Zoom detection", {"ZOOM_MODIFIER", "ZOOM_LATERAL", "ZOOM_ENTER_MS", "ZOOM_EXIT_RATIO"}, false});
        g.push_back({"general", "General", {"ENABLED", "STEP_TRIGGER", "BACKEND", "TICK_HZ"}, false});
        return g;
    }
    // Mode-dependent parts of the page of section i (see the rules above). Channel values are not
    // part of it except as optional per-profile overrides (Visible::overrides).
    Visible visibleSettings(size_t i) const {
        Visible v;
        v.modeId = effectiveMode(i);
        v.canChooseMode = i < ps_.sections.size();
        const ModeDef *m = modes_.find(v.modeId);
        v.isDefaultMode = v.modeId == "normal";
        v.modeLoaded = m != nullptr || v.isDefaultMode;
        ModeDef normal;  // all channels "default", no buttons
        if (!m) m = &normal;
        const bool allDefault = m->allDefaultChannels();
        if (v.canChooseMode) v.groups.push_back({"mode", "Joystick mode", {"MODE"}, false});
        if (i > 0) v.groups.push_back({"general", "General", {"ENABLED"}, false});
        if (allDefault)
            v.groups.push_back({"buttons", "Buttons", {}, true});
        if (!v.isDefaultMode && v.modeLoaded) {
            v.mode.id = m->id;
            v.mode.label = m->label.empty() ? m->id : m->label;
            v.mode.description = m->description;
            v.mode.v = modelist::channelActionText(m->v);
            v.mode.h = modelist::channelActionText(m->h);
            v.mode.twist = modelist::channelActionText(m->twist);
            for (const ButtonBinding &b : m->buttons)
                if (b.type != ButtonActionType::Unbound)
                    v.mode.buttons.emplace_back(b.button, modelist::buttonActionText(b));
            v.mode.hasFile = !m->builtin && !modesDir_.empty();
        }
        if (i > 0 && i < ps_.sections.size()) {
            // Override candidates: channel keys of channels the mode uses (action not "none"),
            // zoom detection only for a "default" twist channel.
            const ChannelAction *acts[3] = {&m->v, &m->h, &m->twist};
            std::vector<std::string> cand;
            const auto &cp = channelPrefixes();
            for (size_t c = 0; c < 3; ++c) {
                if (acts[c]->type == ChannelActionType::Off) continue;
                for (const char *k : {"MIX", "SENS", "DEADZONE", "SIGN", "CURVE", "SMOOTH_MS"})
                    cand.push_back(std::string(cp[c].pre) + k);
            }
            if (!(m->v.type == ChannelActionType::Off && m->h.type == ChannelActionType::Off && m->twist.type == ChannelActionType::Off))
                cand.push_back("INVERT_AXES");  // every mode that uses the axes (the preferred direction depends on the user and the program)
            if (m->twist.type == ChannelActionType::Default || m->twist.type == ChannelActionType::Zoom || m->v.type == ChannelActionType::Zoom ||
                m->h.type == ChannelActionType::Zoom)
                cand.push_back("ZOOM_MODIFIER");  // the key differs per program (Cmd on macOS)
            if (m->twist.type == ChannelActionType::Default)
                for (const char *k : {"ZOOM_LATERAL", "ZOOM_ENTER_MS", "ZOOM_EXIT_RATIO"}) cand.push_back(k);
            Group o{"overrides", "Overrides for this profile", {}, false};
            for (const std::string &k : cand)
                (hasKey(ps_.sections[i], k) ? o.keys : v.addableOverrides).push_back(k);
            // Overrides already in the file stay visible even if the mode no longer uses them.
            for (const KV &kv : ps_.sections[i].kv) {
                const std::string k = upper(kv.first);
                static const std::vector<KV> all = allKeys(Settings());
                if (!lookup(all, k) || k == "MODE" || k == "ENABLED" || isButtonKey(k) || k.rfind("NAV_", 0) == 0 ||
                    k.rfind("CLICK_", 0) == 0 || k == "BACKEND" || k == "INPUT" || k == "TICK_HZ")
                    continue;
                if (std::find(o.keys.begin(), o.keys.end(), k) == o.keys.end()) o.keys.push_back(k);
            }
            v.overrides = o;
        }
        return v;
    }
    // Explicitly adds an override of a channel key to a [device:*]/[app:*] section, starting with
    // the inherited value; remove it again with clearOverride.
    bool addOverride(size_t i, const std::string &key, std::string *err) {
        Value cur;
        if (i == 0) return failb(err, "overrides exist only in device and app profiles");
        if (!value(i, key, &cur)) return failb(err, "unknown setting " + key);
        return setValue(i, key, cur.text, err);
    }
    // "Edit mode file": opens the file of the section's effective mode through the runner. false +
    // *err for a built-in without file (create the editable copy first, see
    // modelist::createModeFile) or an unloaded mode.
    bool editModeFile(size_t i, ActionRunner &runner, std::string *err) const {
        const std::string id = effectiveMode(i);
        const ModeDef *m = modes_.find(id);
        if (!m || m->builtin || modesDir_.empty())
            return failb(err, "this joystick mode has no file (built-in or not loaded)");
        return runner.openPath(modelist::modeFilePath(modesDir_, id), err);
    }

    // ---- sections ----
    std::vector<SectionInfo> sections() const {
        std::vector<SectionInfo> out;
        for (size_t i = 0; i < ps_.sections.size(); ++i) out.push_back(info(i));
        return out;
    }
    size_t sectionCount() const { return ps_.sections.size(); }
    // Adds (or finds) a DEVICE/APP section. npos + *err for an invalid pattern, kind GLOBAL or
    // too many sections.
    size_t addSection(Section::Kind kind, const std::string &pattern, std::string *err) {
        if (kind == Section::GLOBAL) return fail(err, "[global] already exists");
        size_t idx = ps_.find(kind, pattern);
        if (idx != ProfileSet::npos) return idx;
        if (ps_.sections.size() >= kMaxSections) return fail(err, "too many profiles");
        idx = ps_.findOrAdd(kind, pattern);
        if (idx == ProfileSet::npos) return fail(err, "invalid profile name (empty, ']' or line break)");
        dirty_ = true;
        return idx;
    }
    // Renames a device/app profile (changes its match string). Fails for [global], for an invalid
    // pattern and when another profile of the same kind already uses it (case-insensitive).
    bool renameSection(size_t i, const std::string &pattern, std::string *err) {
        if (i == 0 || i >= ps_.sections.size()) return failb(err, "this profile cannot be renamed");
        std::string norm;
        if (!ProfileSet::normalizePattern(pattern, &norm))
            return failb(err, "invalid profile name (empty, ']' or line break)");
        auto low = [](std::string s) {
            for (char &c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        for (size_t k = 1; k < ps_.sections.size(); ++k)
            if (k != i && ps_.sections[k].kind == ps_.sections[i].kind &&
                low(ps_.sections[k].pattern) == low(norm))
                return failb(err, "a profile with this name already exists");
        if (ps_.sections[i].pattern == norm) return true;
        ps_.sections[i].pattern = norm;
        ProfileSet fresh;  // drops the stale search index
        fresh.sections = ps_.sections;
        ps_ = fresh;
        dirty_ = true;
        return true;
    }
    bool removeSection(size_t i, std::string *err) {
        if (i == 0 || i >= ps_.sections.size()) return failb(err, "this profile cannot be removed");
        ps_.remove(i);
        dirty_ = true;
        return true;
    }

    // ---- values ----
    // Settings of section i (global: defaults + [global]; else global + the section's keys).
    Settings effective(size_t i) const { return ps_.sectionSettings(i); }
    // The same with the overrides of the effective mode on top (an [app:*] section's own keys
    // still win, as in the daemon): what a channel of this profile really does.
    Settings effectiveWithMode(size_t i) const {
        Settings s = ps_.sectionSettings(i);
        if (i >= ps_.sections.size()) return s;
        const ModeDef *m = modes_.find(effectiveMode(i));
        if (!m || m->overrides.empty()) return s;
        for (const auto &kv : m->overrides) applyKey(s, upper(kv.first), kv.second, nullptr);
        if (ps_.sections[i].kind == Section::APP)
            for (const KV &kv : ps_.sections[i].kv) applyKey(s, upper(kv.first), kv.second, nullptr);
        return s;
    }

    bool value(size_t i, const std::string &key, Value *out) const {
        const std::string k = upper(key);
        if (i >= ps_.sections.size() || !isEditableKey(k)) return false;
        const std::vector<KV> own = allKeys(ps_.sectionSettings(i));
        const std::vector<KV> base = allKeys(i == 0 ? Settings() : ps_.sectionSettings(0));
        const std::string *t = lookup(own, k), *b = lookup(base, k);
        if (!t || !b) return false;
        out->text = *t;
        out->inherited = *b;
        out->overridden = hasKey(ps_.sections[i], k);
        return true;
    }
    bool isOverridden(size_t i, const std::string &key) const {
        return i < ps_.sections.size() && hasKey(ps_.sections[i], upper(key));
    }
    // Device and program profiles that set the key themselves (section indices): what "N profiles
    // override this" counts when a value of [global] is edited.
    std::vector<size_t> profilesOverriding(const std::string &key) const {
        std::vector<size_t> out;
        const std::string k = upper(key);
        for (size_t i = 1; i < ps_.sections.size(); ++i)
            if (hasKey(ps_.sections[i], k)) out.push_back(i);
        return out;
    }
    // Number of settings (keys, buttons included) the profile sets itself; [global] counts what the
    // file holds.
    size_t overrideCount(size_t i) const { return i < ps_.sections.size() ? ps_.sections[i].kv.size() : 0; }

    // Value the effective mode of section i sets for key, else fallback.
    std::string modeOverrideValue(size_t i, const std::string &key, const std::string &fallback) const {
        if (i >= ps_.sections.size()) return fallback;
        const ModeDef *m = modes_.find(effectiveMode(i));
        if (m)
            for (const auto &kv : m->overrides)
                if (upper(kv.first) == upper(key)) return kv.second;
        return fallback;
    }
    // Where the effective value of a key in section i comes from. Precedence (AGENTS.md):
    // global < device < mode overrides < app.
    enum class Source { BuiltIn, Global, Device, Mode, App };
    bool sourceOf(size_t i, const std::string &key, Source *out) const {
        const std::string k = upper(key);
        if (i >= ps_.sections.size() || !isEditableKey(k)) return false;
        const Section &s = ps_.sections[i];
        const bool own = hasKey(s, k);
        if (own && s.kind == Section::APP) {
            *out = Source::App;
            return true;
        }
        {
            const ModeDef *m = modes_.find(effectiveMode(i));
            if (m)
                for (const auto &kv : m->overrides)
                    if (upper(kv.first) == k) {
                        *out = Source::Mode;
                        return true;
                    }
        }
        if (own) {
            *out = i == 0 ? Source::Global : Source::Device;
            return true;
        }
        *out = i > 0 && hasKey(ps_.sections[0], k) ? Source::Global : Source::BuiltIn;
        return true;
    }

    // Validates with applyKey and stores the canonical value as an override of the section.
    bool setValue(size_t i, const std::string &key, const std::string &text, std::string *err,
                  bool *layoutChanged = nullptr) {
        const std::string k = upper(key);
        if (layoutChanged) *layoutChanged = false;
        if (i >= ps_.sections.size()) return failb(err, "no such profile");
        if (!isEditableKey(k)) return failb(err, "unknown setting " + key);
        Settings t = ps_.sectionSettings(i);
        if (!applyKey(t, k, text, err)) return false;
        const std::vector<KV> tk = allKeys(t);
        const std::string *cp = lookup(tk, k);
        if (!cp) return failb(err, "unknown setting " + key);
        const std::string canon = *cp;
        const std::string before = effectiveMode(i);
        if (!store(i, k, canon, err)) return false;
        if (layoutChanged) *layoutChanged = effectiveMode(i) != before;
        return true;
    }
    // Removes the override ([global]: removes the key from the file = default). false if unknown.
    bool clearOverride(size_t i, const std::string &key, bool *layoutChanged = nullptr) {
        const std::string k = upper(key);
        if (layoutChanged) *layoutChanged = false;
        if (i >= ps_.sections.size() || !(isEditableKey(k) || isButtonKey(k))) return false;
        const std::string before = effectiveMode(i);
        if (!eraseKey(ps_.sections[i], k)) return true;
        dirty_ = true;
        if (layoutChanged) *layoutChanged = effectiveMode(i) != before;
        return true;
    }

    // ---- buttons ----
    std::vector<ButtonRow> buttonRows(size_t i) const {
        std::vector<ButtonRow> rows;
        if (i >= ps_.sections.size()) return rows;
        const Settings s = ps_.sectionSettings(i);
        for (int n = 1; n <= limit_; ++n) {
            auto it = s.buttons.find(n);
            rows.push_back({n, it == s.buttons.end() ? "none" : it->second,
                            hasKey(ps_.sections[i], "BUTTON_" + std::to_string(n)), false});
        }
        for (const auto &b : s.buttons)
            if (b.first > limit_)
                rows.push_back({b.first, b.second, hasKey(ps_.sections[i], "BUTTON_" + std::to_string(b.first)), true});
        return rows;
    }
    // action: none | click | rightclick | middleclick | nav | key:<name>
    bool setButton(size_t i, int number, const std::string &action, std::string *err) {
        if (i >= ps_.sections.size()) return failb(err, "no such profile");
        if (number < 1 || number > limit_)
            return failb(err, "this device has " + std::to_string(limit_) + " buttons");
        const std::string k = "BUTTON_" + std::to_string(number);
        Settings t = ps_.sectionSettings(i);
        if (!applyKey(t, k, action, err)) return false;
        auto it = t.buttons.find(number);
        return store(i, k, it == t.buttons.end() ? "none" : it->second, err);
    }
    bool clearButton(size_t i, int number) {
        return clearOverride(i, "BUTTON_" + std::to_string(number));
    }

    // ---- deprecated NAV_BUTTON / CLICK_BUTTON ----
    // The two legacy keys pairs (0-based button number plus an enable flag) are replaced by
    // BUTTON_<n> = nav / click (1-based). Result of migrateLegacyButtons().
    struct LegacyMigration {
        bool changed = false;                   // the model was edited (save it)
        std::vector<std::string> migrated;      // what was moved, e.g. "[global]: NAV_BUTTON -> BUTTON_1 = nav"
        std::vector<std::string> notices;       // what could not be moved and stays in the file untouched
    };
    // True if any section still has one of the legacy keys (older saves wrote them with their
    // defaults, so this is true for most old files; migrateLegacyButtons() drops unused ones silently).
    bool hasLegacyButtonKeys() const {
        for (const Section &sec : ps_.sections)
            for (const KV &kv : sec.kv) {
                const std::string k = upper(kv.first);
                if (isLegacyButtonKey(k)) return true;
            }
        return false;
    }
    // Moves NAV_BUTTON / CLICK_BUTTON into BUTTON_<n> entries so that every profile behaves as
    // before: n = number + 1, action "nav" or "click". All or nothing per key pair: if any profile
    // cannot be moved (the button is above the device's button count, or the profile already binds
    // that button to something else) the pair stays in the file untouched and a notice says so.
    // Legacy keys that are disabled are simply dropped (they have no effect).
    LegacyMigration migrateLegacyButtons(const StringProvider &str) {
        LegacyMigration r;
        struct Kind { const char *pre, *action; };
        for (const Kind &kd : {Kind{"NAV_", "nav"}, Kind{"CLICK_", "click"}}) {
            const std::string keyNum = std::string(kd.pre) + "BUTTON", keyOn = keyNum + "_ENABLED";
            bool touched = false;
            for (const Section &sec : ps_.sections) touched = touched || hasKey(sec, keyNum) || hasKey(sec, keyOn);
            if (!touched) continue;
            auto eff = [&](size_t i, bool *on, int *n) {
                const Settings t = ps_.sectionSettings(i);
                *on = keyOn[0] == 'N' ? t.nav_button_enabled : t.click_button_enabled;
                *n = (keyOn[0] == 'N' ? t.nav_button : t.click_button) + 1;
            };
            // check first: nothing is changed unless every profile can be moved
            std::vector<std::string> problems;
            for (size_t i = 0; i < ps_.sections.size(); ++i) {
                bool on;
                int n;
                eff(i, &on, &n);
                if (!on) continue;
                const std::string title = info(i).title;
                const int lim = sectionButtonLimit(i);
                if (n < 1 || n > lim) {
                    const bool own = hasKey(ps_.sections[i], keyNum);
                    if (i > 0 && !own && !hasKey(ps_.sections[i], keyOn)) continue;  // inherited, reported at the source
                    problems.push_back(own || i == 0
                                           ? format(str.tr("%1: %2 = %3 is above the %4 buttons of this device"),
                                                    {title, keyNum, std::to_string(n - 1), std::to_string(lim)})
                                           : format(str.tr("%1: %2 is on, but the inherited button %3 does not exist on this "
                                                           "device (%4 buttons); assign a button in the Buttons tab"),
                                                    {title, keyOn, std::to_string(n - 1), std::to_string(lim)}));
                    continue;
                }
                const std::string bk = "BUTTON_" + std::to_string(n);
                for (const KV &kv : ps_.sections[i].kv)
                    if (upper(kv.first) == bk && kv.second != kd.action)
                        problems.push_back(format(str.tr("%1: %2 is already set; %3 was left as it is"),
                                                  {title, bk, keyNum}));
            }
            if (!problems.empty()) {
                for (const std::string &p : problems) r.notices.push_back(p);
                continue;
            }
            // as before, per profile: the global setting first, then what each profile changes
            bool gOn;
            int gN;
            eff(0, &gOn, &gN);
            std::vector<std::pair<size_t, std::pair<bool, int>>> want;  // the effective state of every profile
            for (size_t i = 1; i < ps_.sections.size(); ++i) {
                bool on;
                int n;
                eff(i, &on, &n);
                want.push_back({i, {on, n}});
            }
            auto bind = [&](size_t i, int n, const std::string &action) {
                const std::string bk = "BUTTON_" + std::to_string(n);
                if (n > sectionButtonLimit(i)) return;    // the device has no such button: nothing to bind or turn off
                if (hasKey(ps_.sections[i], bk)) return;  // the profile's own binding is kept
                if (store(i, bk, action, nullptr))
                    r.migrated.push_back(format(str.tr("%1: %2 -> %3 = %4"),
                                                {info(i).title, keyNum, bk, action}));
            };
            if (gOn) bind(0, gN, kd.action);
            for (const auto &w : want) {
                const size_t i = w.first;
                const bool on = w.second.first;
                const int n = w.second.second;
                if (on && !(gOn && gN == n)) bind(i, n, kd.action);
                if (gOn && !(on && gN == n)) bind(i, gN, "none");  // this profile turned the global one off
            }
            for (Section &sec : ps_.sections) {
                eraseKey(sec, keyNum);
                eraseKey(sec, keyOn);
            }
            r.changed = true;
        }
        if (r.changed) dirty_ = true;
        return r;
    }

    // ---- mode ----
    std::string mode(size_t i) const { return i < ps_.sections.size() ? ps_.sectionSettings(i).mode : "normal"; }
    // By id; only for [device:*] / [app:*] (refused for [global]).
    bool setMode(size_t i, const std::string &id, std::string *err, bool *layoutChanged = nullptr) {
        if (!validModeId(id)) return failb(err, "invalid joystick mode id (a-z, 0-9, _ and -, at most 32 characters)");
        return setValue(i, "MODE", id, err, layoutChanged);
    }

    // ---- validation and saving ----
    // Errors that would make the file invalid or lose settings; empty = can be saved.
    std::vector<std::string> validate(const StringProvider &s) const {
        std::vector<std::string> out;
        if (ps_.sections.size() > kMaxSections) out.push_back(s.tr("Too many profiles"));
        for (size_t i = 0; i < ps_.sections.size(); ++i) {
            const Section &sec = ps_.sections[i];
            const std::string title = info(i).title;
            if (i > 0) {
                std::string norm;
                if (!ProfileSet::normalizePattern(sec.pattern, &norm) || norm != sec.pattern)
                    out.push_back(format(s.tr("%1: invalid profile name"), {title}));
            }
            if (sec.kv.size() > kMaxKeysPerSection) out.push_back(format(s.tr("%1: too many settings"), {title}));
            Settings t;
            for (const KV &kv : sec.kv) {
                std::string e;
                if (!applyKey(t, kv.first, kv.second, &e))
                    out.push_back(format(s.tr("%1: %2 = %3: %4"), {title, kv.first, kv.second, e}));
            }
        }
        return out;
    }
    // Mode ids used by a section but not loaded (a hint, not an error: the daemon falls back).
    std::vector<std::string> unknownModes(const ModeSet &ms) const {
        std::vector<std::string> out;
        for (size_t i = 0; i < ps_.sections.size(); ++i) {
            if (!hasKey(ps_.sections[i], "MODE")) continue;
            const std::string id = ps_.sectionSettings(i).mode;
            if (!ms.find(id) && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
        }
        return out;
    }

    // validate(), saveProfiles (atomic replace), then the reload callback. Nothing is written if
    // validation fails. dirty() is cleared once the file is written.
    SaveResult save(const StringProvider &s) {
        SaveResult r;
        const std::vector<std::string> errs = validate(s);
        if (!errs.empty()) {
            for (size_t i = 0; i < errs.size(); ++i) r.error += (i ? "\n" : "") + errs[i];
            return r;
        }
        std::string e;
        if (!saveProfiles(path_, ps_, &e)) {
            r.error = e.empty() ? s.tr("cannot write the configuration") : e;
            return r;
        }
        r.saved = true;
        dirty_ = false;
        r.reloaded = true;
        if (reload_ && !reload_(&e)) {
            r.reloaded = false;
            r.error = e.empty() ? s.tr("saved, but the daemon could not be notified") : e;
        }
        return r;
    }

private:
    static std::string upper(std::string s) {
        for (char &c : s) c = char(spn::utf8::upperChar(c));
        return s;
    }
    static bool isButtonKey(const std::string &k) { return k.rfind("BUTTON_", 0) == 0; }
    static bool isLegacyButtonKey(const std::string &k) {
        return k == "NAV_BUTTON" || k == "NAV_BUTTON_ENABLED" || k == "CLICK_BUTTON" || k == "CLICK_BUTTON_ENABLED";
    }
    static bool isEditableKey(const std::string &k) {
        static const std::vector<KV> all = allKeys(Settings());
        return lookup(all, k) != nullptr;
    }
    static const std::string *lookup(const std::vector<KV> &kv, const std::string &k) {
        for (const KV &p : kv)
            if (p.first == k) return &p.second;
        return nullptr;
    }
    static bool hasKey(const Section &s, const std::string &k) {
        for (const KV &p : s.kv)
            if (upper(p.first) == k) return true;
        return false;
    }
    // Erases all entries of the key; true if there was one.
    static bool eraseKey(Section &s, const std::string &k) {
        const size_t n = s.kv.size();
        s.kv.erase(std::remove_if(s.kv.begin(), s.kv.end(), [&](const KV &p) { return upper(p.first) == k; }),
                   s.kv.end());
        return s.kv.size() != n;
    }
    // Replaces the first entry of the key in place (others removed) or appends.
    bool store(size_t i, const std::string &k, const std::string &v, std::string *err) {
        Section &sec = ps_.sections[i];
        bool found = false;
        for (KV &p : sec.kv)
            if (upper(p.first) == k && !found) {
                p.first = k;
                p.second = v;
                found = true;
            }
        if (found) {
            bool first = true;
            sec.kv.erase(std::remove_if(sec.kv.begin(), sec.kv.end(),
                                        [&](const KV &p) {
                                            if (upper(p.first) != k) return false;
                                            if (first) { first = false; return false; }
                                            return true;
                                        }),
                         sec.kv.end());
        } else {
            if (sec.kv.size() >= kMaxKeysPerSection) return failb(err, "too many settings in this profile");
            sec.kv.emplace_back(k, v);
        }
        dirty_ = true;
        return true;
    }
    SectionInfo info(size_t i) const {
        const Section &s = ps_.sections[i];
        std::string title = "[global]";
        if (s.kind == Section::DEVICE) title = "[device:" + s.pattern + "]";
        else if (s.kind == Section::APP) title = "[app:" + s.pattern + "]";
        return {i, s.kind, s.pattern, title};
    }
    static size_t fail(std::string *err, const char *m) {
        if (err) *err = m;
        return ProfileSet::npos;
    }
    static bool failb(std::string *err, const std::string &m) {
        if (err) *err = m;
        return false;
    }

    ProfileSet ps_;
    std::string path_;
    bool dirty_ = false;
    int limit_ = int(kMaxButtons);
    ReloadFn reload_;
    ModeSet modes_;
    std::string modesDir_;
};

}  // namespace spn::ui
