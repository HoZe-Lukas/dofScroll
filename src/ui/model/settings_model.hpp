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
//   m.setValue(i, "MODE", "timeline", &err);   // the MODE key like any other
//   m.validate(strings) -> error strings; m.save() -> atomic write, then the reload callback.
//
// Mode rules (the settings page is mode-aware):
//  * [global] is the default mode ("normal") only: the model neither offers nor writes MODE there
//    (setValue/setMode refuse it, visibleSettings omits it, modeOptions is empty). A MODE key
//    found in an old [global] section is kept in the file, reported by notices() and as a load
//    warning with the hint to move it to a [device:*] or [app:*] section, and can be removed with
//    clearOverride.
//  * MODE can be chosen in [device:*] and [app:*] sections only, as a dropdown: modeOptions(i) is
//    the ordered option list (entry 0 "Default mode" = MODE not set, inherited; then all loaded
//    modes in load order, built-ins first, with display name and the id as secondary text; an id
//    that is set but not loaded is appended), selectedModeOption(i) the current index and
//    setMode(i, optionIndex) writes MODE, or clears it for entry 0. Exactly one mode is active.
//  * The effective mode of a section is its own MODE, else the inherited one (global < device <
//    mode < app as in the daemon; which device/app combination is active at runtime is unknown
//    here, so an app section only looks at itself and [global]).
//  * Channel settings (mix, sensitivity, deadzone, curve, smoothing, sign, zoom detection) are the
//    same in every mode: globalChannelGroups() lists them as an always-available group set that
//    is edited in [global] (index 0), whatever profile or mode is selected. General settings
//    (start at login, language) and the axes assistant are not part of profile sections.
//  * visibleSettings(i) returns only the mode-dependent parts of the page of a profile: the
//    "mode" and "general" (ENABLED) groups for [device:*]/[app:*], the "buttons" group (NAV/CLICK
//    button and the BUTTON_<n> rows) while all three channel actions of the mode are "default"
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
// CURVE, SMOOTH_MS; ZOOM_LATERAL, ZOOM_ENTER_MS, ZOOM_EXIT_RATIO, BACKEND, TICK_HZ, ENABLED,
// MODE, NAV_BUTTON[_ENABLED], CLICK_BUTTON[_ENABLED]); BUTTON_<n> goes through setButton.
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
        if (warnings && legacyGlobalMode())
            warnings->push_back(globalModeNotice(IdentityStrings()));
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
    // Effective mode id of section i ("normal" for [global]).
    std::string effectiveMode(size_t i) const {
        if (i == 0 || i >= ps_.sections.size()) return "normal";
        const std::string id = ps_.sectionSettings(i).mode;
        return validModeId(id) ? id : std::string("normal");
    }
    // Dropdown options for the mode of section i (empty for [global] and invalid indices).
    std::vector<ModeOption> modeOptions(size_t i) const {
        std::vector<ModeOption> out;
        if (i == 0 || i >= ps_.sections.size()) return out;
        out.push_back({"", "Default mode", "", true});
        for (const ModeDef &m : modes_.modes) out.push_back({m.id, m.label.empty() ? m.id : m.label, m.id, true});
        if (hasKey(ps_.sections[i], "MODE")) {
            const std::string cur = ps_.sectionSettings(i).mode;
            bool found = false;
            for (size_t k = 1; k < out.size(); ++k) found = found || out[k].id == cur;
            if (!found) out.push_back({cur, cur, cur, false});
        }
        return out;
    }
    // Index into modeOptions(i): 0 if MODE is not set in the section; -1 for [global]/invalid.
    int selectedModeOption(size_t i) const {
        if (i == 0 || i >= ps_.sections.size()) return -1;
        if (!hasKey(ps_.sections[i], "MODE")) return 0;
        const std::string cur = ps_.sectionSettings(i).mode;
        const std::vector<ModeOption> o = modeOptions(i);
        for (size_t k = 1; k < o.size(); ++k)
            if (o[k].id == cur) return int(k);
        return 0;
    }
    // Picks an option: 0 clears MODE (inherit), otherwise writes that mode's id.
    bool setMode(size_t i, int optionIndex, std::string *err, bool *layoutChanged = nullptr) {
        const std::vector<ModeOption> o = modeOptions(i);
        if (o.empty()) return setValue(i, "MODE", "normal", err, layoutChanged);  // refused with the [global] message
        if (optionIndex < 0 || size_t(optionIndex) >= o.size()) return failb(err, "no such mode option");
        if (optionIndex == 0) {
            if (layoutChanged) *layoutChanged = false;
            return clearOverride(i, "MODE", layoutChanged);
        }
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
        g.push_back({"zoom_detect", "Zoom detection", {"ZOOM_LATERAL", "ZOOM_ENTER_MS", "ZOOM_EXIT_RATIO"}, false});
        g.push_back({"general", "General", {"ENABLED", "STEP_TRIGGER", "BACKEND", "TICK_HZ"}, false});
        return g;
    }
    // Mode-dependent parts of the page of section i (see the rules above). Channel values are not
    // part of it except as optional per-profile overrides (Visible::overrides).
    Visible visibleSettings(size_t i) const {
        Visible v;
        v.modeId = effectiveMode(i);
        v.canChooseMode = i > 0;
        const ModeDef *m = modes_.find(v.modeId);
        v.isDefaultMode = v.modeId == "normal";
        v.modeLoaded = m != nullptr || v.isDefaultMode;
        ModeDef normal;  // all channels "default", no buttons
        if (!m) m = &normal;
        const bool allDefault = m->allDefaultChannels();
        if (v.canChooseMode) {
            v.groups.push_back({"mode", "Mode", {"MODE"}, false});
            v.groups.push_back({"general", "General", {"ENABLED"}, false});
        }
        if (allDefault)
            v.groups.push_back({"buttons", "Buttons",
                                {"NAV_BUTTON", "NAV_BUTTON_ENABLED", "CLICK_BUTTON", "CLICK_BUTTON_ENABLED"}, true});
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
                    k.rfind("CLICK_", 0) == 0 || k == "BACKEND" || k == "TICK_HZ")
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
            return failb(err, "this mode has no file (built-in or not loaded)");
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

    // Value the effective mode of section i sets for key, else fallback.
    std::string modeOverrideValue(size_t i, const std::string &key, const std::string &fallback) const {
        if (i == 0 || i >= ps_.sections.size()) return fallback;
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
        if (i > 0) {
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
        if (i == 0 && k == "MODE")
            return failb(err, "MODE cannot be set in [global] (it is the default mode); choose it in a device or app profile");
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

    // ---- mode ----
    std::string mode(size_t i) const { return i < ps_.sections.size() ? ps_.sectionSettings(i).mode : "normal"; }
    // By id; only for [device:*] / [app:*] (refused for [global]).
    bool setMode(size_t i, const std::string &id, std::string *err, bool *layoutChanged = nullptr) {
        if (!validModeId(id)) return failb(err, "invalid mode id (a-z, 0-9, _ and -, at most 32 characters)");
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
    // Hints that are no errors: a legacy MODE in [global].
    std::vector<std::string> notices(const StringProvider &s) const {
        std::vector<std::string> out;
        if (legacyGlobalMode()) out.push_back(globalModeNotice(s));
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
    // A MODE other than "normal" in [global] (saving always writes MODE = normal there).
    bool legacyGlobalMode() const {
        return !ps_.sections.empty() && hasKey(ps_.sections[0], "MODE") && ps_.sectionSettings(0).mode != "normal";
    }
    static std::string globalModeNotice(const StringProvider &s) {
        return s.tr("[global]: MODE is only supported for the default mode; move it to a device or app profile");
    }
    static std::string upper(std::string s) {
        for (char &c : s) c = char(spn::utf8::upperChar(c));
        return s;
    }
    static bool isButtonKey(const std::string &k) { return k.rfind("BUTTON_", 0) == 0; }
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
