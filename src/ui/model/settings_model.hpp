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
// Layout (docs/plan-config-split.md): load() reads dofscroll.conf plus devices/ and apps/ (loadProfileTreeAt)
// and remembers the file every profile came from. save() writes only what changed: one file per changed
// profile (saveProfileFile with the FileDiag it was read from, so a hand edit in between is a Conflict and
// nothing is overwritten), the removed profiles (deleteProfileFile with the stamp), and dofscroll.conf only when
// [global] or a profile that still lives in it changed (saveGlobalFile). A profile of an old single file that is
// edited moves into its own file, unless a defective file of the same name exists (then it stays where it is, and
// the defective file is never touched). When dofscroll.conf is a symlink (or has another name) new and edited
// profiles stay in the single file as before. A Conflict or a refused write is reported next to the profile
// (profileProblem) and, for a Conflict, the model is reloaded from disk (SaveResult::conflict).
// The profiles of a kind are kept in the order the daemon applies them (PRIORITY ascending, later wins);
// moveProfile changes PRIORITY, orderText says the order in words.
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
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "config.hpp"
#include "utf8.hpp"
#include "device_db.hpp"
#include "mode_list.hpp"
#include "modes.hpp"
#include "platform/fs.hpp"
#include "profile_files.hpp"
#include "ui/model/action_runner.hpp"
#include "ui/model/display_text.hpp"
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
        bool conflict = false;  // a file was changed outside (or refused): the model was reloaded from disk
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

    // Loads dofscroll.conf and the profile files next to it (missing file = defaults). false + *err if it
    // cannot be read. *warnings gets every parser warning and the reason of every defective file.
    bool load(const std::string &path, std::vector<std::string> *warnings, std::string *err) {
        return loadImpl(path, warnings, err, false);
    }
    // Takes an already loaded set (tests, other sources). The next save() writes everything (no profile has
    // a file yet); dirty() stays false until the first edit.
    void setProfiles(const ProfileSet &ps, const std::string &path) {
        ps_ = ps;
        path_ = path;
        diags_.clear();
        st_.assign(ps_.sections.size(), SecState());
        removed_.clear();
        problems_.clear();
        globalDirty_ = false;
        forceAll_ = true;
        globalStamp_.clear();
        newInFiles_ = filesAllowed(path);
    }
    const ProfileSet &profiles() const { return ps_; }
    const std::string &path() const { return path_; }
    // Something is waiting to be written (an edit, a removed profile, or a write that failed earlier).
    bool dirty() const {
        if (globalDirty_ || !removed_.empty()) return true;
        for (const SecState &t : st_)
            if (t.dirty) return true;
        return false;
    }
    // True when new profiles get their own file (dofscroll.conf is a regular file): the split layout. False for
    // a symlinked dofscroll.conf or a differently named file: the old single-file format stays in use.
    bool splitLayout() const { return newInFiles_; }
    // The file profile i lives in ("" for [global], a profile of the single file or a new one).
    std::string profileFile(size_t i) const {
        return i > 0 && i < st_.size() && st_[i].home == SecState::FILE ? st_[i].file.path : std::string();
    }
    // The profile is still read from dofscroll.conf (an old single file or a symlink), not from a file of its own.
    bool inSingleFile(size_t i) const { return i > 0 && i < st_.size() && st_[i].home == SecState::LEGACY; }
    // True once after a save reloaded the model from disk (a Conflict): the view must show the profiles again.
    bool takeReloaded() {
        const bool r = reloadedByConflict_;
        reloadedByConflict_ = false;
        return r;
    }
    // The profile has edits that are not written yet.
    bool profileDirty(size_t i) const { return i < st_.size() && (i == 0 ? globalDirty_ : st_[i].dirty); }

    // ---- defective profile files ----
    // A profile file that is not loaded (and never overwritten while the error stands), or a second file with the
    // name of a profile that is loaded (duplicate), or a directory problem.
    struct Defect {
        Section::Kind kind = Section::DEVICE;
        std::string name;      // profile name from the header, "" if unusable
        std::string file;      // full path
        std::string message;   // why (English, from the loader)
        bool duplicate = false;      // another file holds the same profile and wins
        bool legacyActive = false;   // a section of dofscroll.conf with this name is active instead
        bool directory = false;      // a whole directory (too many files, cannot be opened)
    };
    std::vector<Defect> defects() const {
        std::vector<Defect> out;
        for (const FileDiag &d : diags_) {
            if (!d.fatal || (d.origin != FileDiag::PROFILE_FILE && d.origin != FileDiag::DIRECTORY)) continue;
            Defect x;
            x.kind = d.kind;
            x.name = displayText(d.name);
            x.file = displayText(d.path, 400);
            for (const std::string &w : d.warnings) x.message += (x.message.empty() ? "" : "; ") + displayText(w, 400);
            x.directory = d.origin == FileDiag::DIRECTORY;
            if (!x.directory) {
                for (size_t i = 1; i < ps_.sections.size() && i < st_.size(); ++i) {
                    if (ps_.sections[i].kind != d.kind) continue;
                    if (!d.name.empty() && fold(ps_.sections[i].pattern) == fold(d.name) && st_[i].home == SecState::FILE)
                        x.duplicate = true;
                    if (st_[i].home == SecState::LEGACY && defectiveFor(d.kind, ps_.sections[i].pattern) == &d)
                        x.legacyActive = true;
                }
            }
            out.push_back(x);
        }
        return out;
    }
    // Words next to a profile that is read from dofscroll.conf only because its own file is defective ("" if not).
    std::string legacyHint(size_t i, const StringProvider &s) const {
        if (i == 0 || i >= ps_.sections.size() || st_[i].home != SecState::LEGACY) return std::string();
        const FileDiag *d = defectiveFor(ps_.sections[i].kind, ps_.sections[i].pattern);
        if (!d) return std::string();
        return format(s.tr("This profile is still read from dofscroll.conf because its own file %1 is defective. The "
                           "file is not touched; fix or remove it to use the profile file again."),
                      {baseName(d->path)});
    }
    // A binding that can do harm when it comes from a file of someone else, decided with the parsers of the daemon
    // (so "key:ctrl +w" is seen like "key:ctrl+w"): a chord that chordNeedsWarning flags, a modifier or a destructive
    // key (Delete, Backspace, Enter, Esc, F4) as a plain key or channel key, a mouse click as button action (also the
    // legacy CLICK_BUTTON), and a pointer to a joystick mode of the user's own (a mode file can bind anything).
    bool bindingIsRisky(const std::string &key, const std::string &value) const {
        const std::string k = upper(utf8::trim(key));
        auto plainRisky = [](Key kk) {
            switch (kk) {
                case Key::Ctrl: case Key::Alt: case Key::Shift: case Key::Super: case Key::Delete: case Key::Backspace:
                case Key::Enter: case Key::KpEnter: case Key::Esc: case Key::F4:
                    return true;
                default:
                    return false;
            }
        };
        auto ownMode = [&](const std::string &mid) {
            const ModeDef *md = modes_.find(utf8::lowerAscii(utf8::trim(mid)));
            return md && !md->builtin;
        };
        if (k == "CLICK_BUTTON") return true;
        if (k == "MODE") return ownMode(value);
        if (k.compare(0, 7, "BUTTON_") == 0) {
            ButtonActionType t;
            Key kk = Key::Space;
            ChordMods mods;
            std::string mode;
            if (!parseButtonAction(value, &t, &kk, nullptr, &mode, &mods)) return false;
            switch (t) {
                case ButtonActionType::Click: case ButtonActionType::RightClick: case ButtonActionType::MiddleClick:
                    return true;
                case ButtonActionType::Key:
                    return mods.n == 0 ? plainRisky(kk) : chordNeedsWarning(kk, mods);
                case ButtonActionType::ModeSwitch: case ButtonActionType::ToggleMode: case ButtonActionType::HoldMode:
                    return ownMode(mode);
                default:
                    return false;
            }
        }
        if (k.size() > 7 && k.compare(k.size() - 7, 7, "_ACTION") == 0) {
            ChannelAction a;
            if (!parseChannelAction(value, &a, nullptr) || !a.usesKeys()) return false;
            const struct { Key key; const ChordMods &mods; } args[2] = {{a.neg, a.negMods}, {a.pos, a.posMods}};
            for (const auto &arg : args)
                if (arg.mods.n == 0 ? plainRisky(arg.key) : chordNeedsWarning(arg.key, arg.mods)) return true;
        }
        return false;
    }
    bool bindsRiskyChord(size_t i) const {
        if (i == 0 || i >= ps_.sections.size()) return false;
        for (const KV &kv : ps_.sections[i].kv)
            if (bindingIsRisky(kv.first, kv.second)) return true;
        return false;
    }
    // The risk wording for a profile that binds such a chord ("" if it does not); the same idea as the note of the
    // joystick mode editor.
    std::string chordNote(size_t i, const StringProvider &s) const {
        if (!bindsRiskyChord(i)) return std::string();
        return s.tr("This profile presses shortcut chords or keys that can do harm (Ctrl, Alt, Super, Delete, ...) or clicks, "
                    "or uses a Joystick mode of your own, when a button is pressed or the mouse is moved. Only keep profile "
                    "files from sources you trust and read the file first.");
    }
    // What the last save or load found about this profile (a refused write, a changed file, ...): "" if nothing.
    std::string profileProblem(size_t i) const {
        if (i == 0 || i >= ps_.sections.size()) return std::string();
        auto it = problems_.find(keyOf(ps_.sections[i].kind, ps_.sections[i].pattern));
        return it == problems_.end() ? std::string() : it->second;
    }

    // ---- order of the profiles of a kind ----
    // The profiles of a kind are applied in ascending PRIORITY (later wins); sections() lists them in that order.
    int priority(size_t i) const { return i > 0 && i < ps_.sections.size() ? sectionPriority(ps_.sections[i]) : 0; }
    bool canMove(size_t i, bool up) const {
        const std::vector<size_t> o = kindOrder(i);
        if (o.empty()) return false;
        const size_t pos = size_t(std::find(o.begin(), o.end(), i) - o.begin());
        return up ? pos > 0 : pos + 1 < o.size();
    }
    // Moves a profile one place earlier (up: applied before its neighbour, loses where both match) or later
    // (down: wins). Changes the PRIORITY of the two profiles (the gaps stay); only when the order would otherwise
    // stay ambiguous (equal values, hand-written numbers) the whole kind is numbered again. *newIndex = the
    // profile's section index afterwards (the indexes of others change).
    bool moveProfile(size_t i, bool up, size_t *newIndex, std::string *err) {
        const std::vector<size_t> o = kindOrder(i);
        const size_t pos = size_t(std::find(o.begin(), o.end(), i) - o.begin());
        if (newIndex) *newIndex = i;
        if (o.empty() || pos >= o.size() || (up ? pos == 0 : pos + 1 >= o.size()))
            return failb(err, "this profile cannot be moved any further");
        const size_t j = up ? pos - 1 : pos + 1;
        std::vector<size_t> want = o;
        std::swap(want[pos], want[j]);
        std::map<size_t, int> np;
        for (size_t k : o) np[k] = sectionPriority(ps_.sections[k]);
        if (np[o[pos]] != np[o[j]]) std::swap(np[o[pos]], np[o[j]]);
        const size_t lo = std::min(pos, j), hi = std::max(pos, j);
        bool strict = true;  // the new order must not depend on ties around the two profiles
        for (size_t k = lo == 0 ? 0 : lo - 1; k <= hi && k + 1 < want.size(); ++k)
            strict = strict && np[want[k]] < np[want[k + 1]];
        if (!strict) {
            const int step = std::max(1, std::min(10, int(kMaxPriority) / int(want.size() + 1)));
            for (size_t k = 0; k < want.size(); ++k) np[want[k]] = int(k + 1) * step;
        }
        const Section::Kind kind = ps_.sections[i].kind;
        const std::string name = ps_.sections[i].pattern;
        for (size_t k : o) {
            if (np[k] == sectionPriority(ps_.sections[k])) continue;
            if (np[k] == 0) {
                eraseKey(ps_.sections[k], "PRIORITY");
                touch(k);
            } else if (!store(k, "PRIORITY", std::to_string(np[k]), err)) {
                return false;
            }
        }
        resort();
        if (newIndex) *newIndex = ps_.find(kind, name);
        return true;
    }
    // The order of profile i in words ("" for [global]): which profile it is applied after (and therefore wins
    // over where both match) and which one is applied after it.
    std::string orderText(size_t i, const StringProvider &s) const {
        const std::vector<size_t> o = kindOrder(i);
        if (o.empty()) return std::string();
        const size_t pos = size_t(std::find(o.begin(), o.end(), i) - o.begin());
        if (o.size() == 1)
            return ps_.sections[i].kind == Section::APP
                       ? s.tr("This is the only program profile, so there is no order to think about.")
                       : s.tr("This is the only device profile, so there is no order to think about.");
        auto nm = [&](size_t k) { return "\"" + displayText(ps_.sections[o[k]].pattern) + "\""; };
        std::string t;
        if (pos > 0) t += format(s.tr("Applied after %1, so it wins over %1 where both match."), {nm(pos - 1)});
        else t += s.tr("Applied first, so every other profile of this kind wins over it where both match.");
        if (pos + 1 < o.size()) t += " " + format(s.tr("%1 is applied after it and wins over it."), {nm(pos + 1)});
        else if (pos > 0) t += " " + s.tr("Nothing is applied after it.");
        if ((pos > 0 && priority(o[pos - 1]) == priority(i)) || (pos + 1 < o.size() && priority(o[pos + 1]) == priority(i)))
            t += " " + s.tr("Profiles with the same priority are applied in the order of their file names; use Move up or "
                            "Move down to fix the order.");
        return t;
    }

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
            auto zooms = [](const ChannelAction &a, const SplitChannel &sp) {
                return a.type == ChannelActionType::Zoom ||
                       (sp.active() && (sp.pushAction(a).type == ChannelActionType::Zoom || sp.tiltAction(a).type == ChannelActionType::Zoom));
            };
            if (m->twist.type == ChannelActionType::Default || m->twist.type == ChannelActionType::Zoom || zooms(m->v, m->vSplit) ||
                zooms(m->h, m->hSplit))
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
    size_t addSection(Section::Kind kind, const std::string &pattern, std::string *err, bool appliedLast = true) {
        if (kind == Section::GLOBAL) return fail(err, "[global] already exists");
        size_t idx = ps_.find(kind, pattern);
        if (idx != ProfileSet::npos) return idx;
        if (ps_.sections.size() >= kMaxSections) return fail(err, "too many profiles");
        std::string norm;
        if (!ProfileSet::normalizePattern(pattern, &norm)) return fail(err, "invalid profile name (empty, ']' or line break)");
        if (const FileDiag *d = defectiveFor(kind, norm)) {
            if (err) *err = "the file " + baseName(d->path) + " has this name and is defective; repair or remove it first";
            return ProfileSet::npos;
        }
        // The profile of a removed one whose file is still there: the new profile takes that file over.
        const FileDiag *old = nullptr;
        for (const Removed &r : removed_)
            if (r.file.kind == kind && fold(r.file.name) == fold(norm)) old = &r.file;
        const FileDiag oldFile = old ? *old : FileDiag();
        // appliedLast (a profile of the user): after every other profile of its kind, whatever their numbers (also
        // all negative, all 0 or a single one). !appliedLast (an imported profile): before all of them, so it never
        // wins over the user's own. The kind is numbered again when the number would leave the range.
        int newPrio = 0;
        for (int round = 0; round < 2; ++round) {
            size_t same = 0;
            int lo = 0, hi = 0;
            for (const Section &s : ps_.sections)
                if (s.kind == kind) {
                    const int p = sectionPriority(s);
                    lo = same == 0 ? p : std::min(lo, p);
                    hi = same == 0 ? p : std::max(hi, p);
                    ++same;
                }
            if (same == 0) break;
            newPrio = appliedLast ? hi + 10 : lo - 10;
            if (newPrio <= kMaxPriority && newPrio >= kMinPriority) break;
            if (round == 1 || !renumberKind(kind, err)) return fail(err, "the order of the profiles cannot be extended");
        }
        idx = ps_.findOrAdd(kind, norm);
        if (idx == ProfileSet::npos) return fail(err, "invalid profile name (empty, ']' or line break)");
        st_.resize(ps_.sections.size());
        SecState &t = st_[idx];
        t = SecState();
        if (old) {
            t.home = SecState::FILE;
            t.file = oldFile;
            t.file.section = idx;
            removed_.erase(std::remove_if(removed_.begin(), removed_.end(),
                                          [&](const Removed &r) { return r.file.path == oldFile.path; }),
                           removed_.end());
        }
        t.dirty = true;
        problems_.erase(keyOf(kind, norm));
        if (newPrio != 0) ps_.sections[idx].kv.emplace_back("PRIORITY", std::to_string(newPrio));
        resort();
        return ps_.find(kind, norm);
    }
    // Renames a device/app profile (changes its match string). Fails for [global], for an invalid
    // pattern and when another profile of the same kind already uses it (case-insensitive).
    bool renameSection(size_t i, const std::string &pattern, std::string *err) {
        if (i == 0 || i >= ps_.sections.size()) return failb(err, "this profile cannot be renamed");
        std::string norm;
        if (!ProfileSet::normalizePattern(pattern, &norm))
            return failb(err, "invalid profile name (empty, ']' or line break)");
        for (size_t k = 1; k < ps_.sections.size(); ++k)
            if (k != i && ps_.sections[k].kind == ps_.sections[i].kind && fold(ps_.sections[k].pattern) == fold(norm))
                return failb(err, "a profile with this name already exists");
        if (ps_.sections[i].pattern == norm) return true;
        if (const FileDiag *d = defectiveFor(ps_.sections[i].kind, norm))
            return failb(err, "the file " + baseName(d->path) + " has this name and is defective; repair or remove it first");
        // The new name is the one of a profile that was removed in the same autosave window and whose file is still
        // there: the profile takes that file over (as addSection does) and its own old file is removed instead, so
        // the delete of the removed profile cannot hit the file that was just written.
        for (size_t k = 0; k < removed_.size(); ++k) {
            if (removed_[k].file.kind != ps_.sections[i].kind || fold(removed_[k].file.name) != fold(norm)) continue;
            const FileDiag taken = removed_[k].file;
            removed_.erase(removed_.begin() + long(k));
            if (st_[i].home == SecState::FILE) removed_.push_back({st_[i].file});
            else if (st_[i].home == SecState::LEGACY) globalDirty_ = true;  // the profile leaves dofscroll.conf
            st_[i].home = SecState::FILE;
            st_[i].file = taken;
            st_[i].file.section = i;
            break;
        }
        ps_.sections[i].pattern = norm;
        reindex();
        touch(i);  // the file is written under the new name first, then the old one is removed
        return true;
    }
    // Replaces every setting of a device/app profile by `kv` (each key and value is validated first; nothing changes
    // when one is invalid). The profile keeps its own PRIORITY (the order is not part of what Import brings).
    bool setProfileKeys(size_t i, const std::vector<KV> &kv, std::string *err) {
        if (i == 0 || i >= ps_.sections.size()) return failb(err, "this profile cannot be replaced");
        if (kv.size() > kMaxKeysPerSection) return failb(err, "too many settings in this profile");
        // Every value is stored in its canonical form, as setValue does (what the parser accepts, not the raw text).
        std::vector<KV> next;
        for (const KV &p : ps_.sections[i].kv)
            if (upper(p.first) == "PRIORITY") next.push_back(p);
        for (const KV &p : kv) {
            const std::string k = upper(p.first);
            if (k == "PRIORITY") continue;
            std::string e;
            Settings t;
            if (!applyKeyLayer(t, k, p.second, layerOfSection(ps_.sections[i].kind), &e))
                return failb(err, p.first + " = " + p.second + ": " + e);
            const std::vector<KV> tk = allKeys(t);
            const std::string *cp = lookup(tk, k);
            next.emplace_back(cp ? k : p.first, cp ? *cp : p.second);
        }
        ps_.sections[i].kv = next;
        touch(i);
        return true;
    }
    // Removes a profile; its file is deleted by the next save (after checking that it is unchanged).
    bool removeSection(size_t i, std::string *err) {
        if (i == 0 || i >= ps_.sections.size()) return failb(err, "this profile cannot be removed");
        if (st_[i].home == SecState::FILE) removed_.push_back({st_[i].file});
        else if (st_[i].home == SecState::LEGACY) globalDirty_ = true;
        problems_.erase(keyOf(ps_.sections[i].kind, ps_.sections[i].pattern));
        ps_.remove(i);
        st_.erase(st_.begin() + long(i));
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
        std::vector<KV> ov;
        for (const auto &kv : m->overrides) ov.emplace_back(upper(kv.first), kv.second);
        applyKvList(s, ov, 0);
        // The program layer (1) beats the mode's channel actions and buttons, as in ProfileSet::resolve.
        if (ps_.sections[i].kind == Section::APP) applyKvList(s, ps_.sections[i].kv, 1);
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
    // PRIORITY (the order of the profiles) is not a setting and is not counted.
    size_t overrideCount(size_t i) const {
        if (i >= ps_.sections.size()) return 0;
        size_t n = 0;
        for (const KV &kv : ps_.sections[i].kv)
            if (upper(kv.first) != "PRIORITY") ++n;
        return n;
    }

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
        if (!applyKeyLayer(t, k, text, layerOfSection(ps_.sections[i].kind), err)) return false;
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
        touch(i);
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
                                                           "device (%4 buttons); assign a button on the Motions and buttons tab"),
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
            for (size_t i = 0; i < ps_.sections.size(); ++i) {
                const bool a = eraseKey(ps_.sections[i], keyNum), b = eraseKey(ps_.sections[i], keyOn);
                if (a || b) touch(i);
            }
            r.changed = true;
        }
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

    // validate(), then the changed files (see the layout notes at the top; atomic replace per file), then one
    // call of the reload callback. Nothing is written if validation fails. A file that was changed on disk since
    // it was read is never overwritten or removed: the problem is reported in words (SaveResult::error and
    // profileProblem) and the model is reloaded from disk (SaveResult::conflict). saved is true only when every
    // planned write went through.
    SaveResult save(const StringProvider &s) {
        SaveResult r;
        const std::vector<std::string> errs = validate(s);
        if (!errs.empty()) {
            for (size_t i = 0; i < errs.size(); ++i) r.error += (i ? "\n" : "") + errs[i];
            return r;
        }
        // A profile folder that could not be read: nothing is written (the profiles in it are unknown, a write could
        // replace or duplicate them).
        for (const FileDiag &d : diags_)
            if (d.fatal && d.origin == FileDiag::DIRECTORY) {
                r.error = format(s.tr("could not read the folder %1: nothing is written until it can be read again"),
                                 {displayText(d.path, 400)});
                return r;
            }
        const std::string dir = configDirOf(path_);
        std::vector<std::string> msgs;
        bool wrote = false, attempted = false;
        auto problem = [&](const Section &sec, const std::string &m) {
            problems_[keyOf(sec.kind, sec.pattern)] = m;
            msgs.push_back("\"" + displayText(sec.pattern) + "\": " + m);
        };
        // 1. one file per changed profile (a profile of the single file moves into its own file once it is edited)
        std::vector<size_t> toSingle;
        for (size_t i = 1; i < ps_.sections.size() && i < st_.size(); ++i) {
            SecState &t = st_[i];
            if (!(forceAll_ || t.dirty)) continue;
            const Section &sec = ps_.sections[i];
            const bool file = t.home == SecState::FILE ||
                              (newInFiles_ && !defectiveFor(sec.kind, sec.pattern));
            if (!file) {
                if (t.home == SecState::NEW) t.home = SecState::LEGACY;
                toSingle.push_back(i);
                globalDirty_ = true;
                continue;
            }
            attempted = true;
            const WriteResult w = saveProfileFile(dir, sec, t.home == SecState::FILE ? &t.file : nullptr);
            if (w.ok()) {
                const bool fromSingle = t.home == SecState::LEGACY;
                t.file = FileDiag();
                t.file.origin = FileDiag::PROFILE_FILE;
                t.file.kind = sec.kind;
                t.file.name = sec.pattern;
                t.file.path = w.path;
                t.file.stamp = w.stamp;
                t.file.section = i;
                t.home = SecState::FILE;
                t.dirty = false;
                problems_.erase(keyOf(sec.kind, sec.pattern));
                if (fromSingle) globalDirty_ = true;  // the old section leaves dofscroll.conf
                if (!w.warning.empty()) problems_[keyOf(sec.kind, sec.pattern)] = w.warning;
                wrote = true;
                continue;
            }
            r.conflict = r.conflict || w.status == WriteStatus::Conflict || w.status == WriteStatus::Refused;
            problem(sec, w.status == WriteStatus::Conflict
                             ? format(s.tr("the file %1 was changed outside the settings window, so it was not "
                                           "overwritten; the profile was reloaded from it. Repeat your change if you "
                                           "still want it."),
                                      {baseName(w.path)})
                             : w.error.empty() ? s.tr("cannot write the profile file") : displayText(w.error, 400));
        }
        // 2. profiles that were removed: their files go (only when unchanged since they were read)
        for (size_t k = 0; k < removed_.size();) {
            const FileDiag &f = removed_[k].file;
            attempted = true;
            const WriteResult w = deleteProfileFile(dir, f.kind, f.path, f.stamp);
            if (w.ok()) {
                wrote = true;
                removed_.erase(removed_.begin() + long(k));
                continue;
            }
            Section ghost;
            ghost.kind = f.kind;
            ghost.pattern = f.name;
            if (w.status == WriteStatus::Failed) {
                problem(ghost, displayText(w.error, 400));
                ++k;
                continue;
            }
            r.conflict = true;  // the file stays, so the profile comes back after the reload
            problem(ghost, w.status == WriteStatus::Conflict
                               ? format(s.tr("the file %1 was changed outside the settings window, so it was not "
                                             "removed; the profile was reloaded from it."),
                                        {baseName(w.path)})
                               : displayText(w.error, 400));
            removed_.erase(removed_.begin() + long(k));
        }
        // 3. dofscroll.conf: [global] plus the profiles that still live in it (only when something there changed)
        if (forceAll_ || globalDirty_) {
            attempted = true;
            ProfileSet g;
            g.sections[0] = ps_.sections[0];
            for (size_t i = 1; i < ps_.sections.size() && i < st_.size(); ++i) {
                if (st_[i].home != SecState::LEGACY) continue;
                const size_t idx = g.findOrAdd(ps_.sections[i].kind, ps_.sections[i].pattern);
                if (idx != ProfileSet::npos) g.sections[idx].kv = ps_.sections[i].kv;
            }
            WriteResult w;
            if (isGlobalName(path_)) {
                w = saveGlobalFile(dir, g, globalStamp_);
            } else {
                std::string e;
                w.path = path_;
                if (!saveProfiles(path_, g, &e)) {
                    w.status = WriteStatus::Failed;
                    w.error = e.empty() ? s.tr("cannot write the configuration") : e;
                }
                w.stamp = profileFileStamp(path_, true);
            }
            if (w.ok()) {
                globalStamp_ = w.stamp;
                globalDirty_ = false;
                wrote = true;
                for (size_t i : toSingle) st_[i].dirty = false;
                for (SecState &t : st_)
                    if (t.home == SecState::LEGACY) t.dirty = false;
            } else {
                r.conflict = r.conflict || w.status == WriteStatus::Conflict;
                msgs.push_back(w.status == WriteStatus::Conflict
                                   ? format(s.tr("%1 was changed outside the settings window, so it was not overwritten; "
                                                 "it was reloaded. Repeat your change if you still want it."),
                                            {baseName(w.path)})
                                   : w.error.empty() ? s.tr("cannot write the configuration") : displayText(w.error, 400));
            }
        }
        forceAll_ = false;
        for (size_t i = 0; i < msgs.size(); ++i) r.error += (i ? "\n" : "") + msgs[i];
        r.saved = msgs.empty();
        r.reloaded = true;
        std::string e;
        if (r.conflict) {
            std::string le;
            loadImpl(path_, nullptr, &le, true);  // what is on disk wins; the problems stay visible
            reloadedByConflict_ = true;
        }
        if ((wrote || !attempted) && reload_ && !reload_(&e) && r.saved) {
            r.reloaded = false;
            r.error = e.empty() ? s.tr("saved, but the daemon could not be notified") : e;
        }
        return r;
    }

private:
    // Where a profile lives and whether it has edits that are not written. Parallel to ps_.sections.
    struct SecState {
        enum Home { NEW, FILE, LEGACY } home = NEW;  // NEW: no file yet; FILE: devices/ or apps/; LEGACY: dofscroll.conf
        bool dirty = false;
        FileDiag file;  // FILE: the file as read or as last written (path and stamp for the writer)
    };
    struct Removed {
        FileDiag file;
    };
    static std::string baseName(const std::string &p0) {
        const std::string p = displayText(p0, 400);
        const size_t k = p.find_last_of('/');
        return k == std::string::npos ? p : p.substr(k + 1);
    }
    static std::string fold(const std::string &s) { return spn::utf8::lowerAscii(s); }
    static std::string keyOf(Section::Kind k, const std::string &name) {
        return std::string(k == Section::DEVICE ? "d:" : "a:") + fold(name);
    }
    // dofscroll.conf in the configuration directory (the profile files need that name)?
    static bool isGlobalName(const std::string &path) { return baseName(path) == "dofscroll.conf"; }
    // New profiles get their own file only when dofscroll.conf is a regular file called dofscroll.conf.
    static bool filesAllowed(const std::string &path) {
        if (path.empty() || !isGlobalName(path)) return false;
        fs::FileInfo fi;
        return !(fs::lstatPath(path, &fi) && fi.isLink);
    }
    // The defective file with this profile name, if the profile is not loaded from another file.
    const FileDiag *defectiveFor(Section::Kind kind, const std::string &name) const {
        for (const SecState &t : st_)
            if (t.home == SecState::FILE && t.file.kind == kind && fold(t.file.name) == fold(name)) return nullptr;
        // by the header name, or - when the header could not be read - by the file name the profile would get
        const std::string f1 = profileSlug(name) + ".conf", f2 = profileSlugHashed(name) + ".conf";
        for (const FileDiag &d : diags_) {
            if (!d.fatal || d.origin != FileDiag::PROFILE_FILE || d.kind != kind) continue;
            if (!d.name.empty() ? fold(d.name) == fold(name) : (baseName(d.path) == f1 || baseName(d.path) == f2)) return &d;
        }
        return nullptr;
    }
    // Section indexes of the kind of section i, in applied order (empty for [global] or a bad index).
    // Applied order of two profiles of one kind, as the loader breaks ties: PRIORITY, then the profiles of
    // dofscroll.conf (file order), then the file name (the one the profile has, or gets at the next save).
    bool appliedBefore(size_t a, size_t b) const {
        const int pa = sectionPriority(ps_.sections[a]), pb = sectionPriority(ps_.sections[b]);
        if (pa != pb) return pa < pb;
        if (a >= st_.size() || b >= st_.size()) return false;
        const bool la = st_[a].home == SecState::LEGACY, lb = st_[b].home == SecState::LEGACY;
        if (la != lb) return la;
        if (la) return false;
        auto name = [&](size_t k) {
            if (st_[k].home == SecState::FILE && !st_[k].dirty) return baseName(st_[k].file.path);
            return profileSlug(ps_.sections[k].pattern) + ".conf";
        };
        return name(a) < name(b);
    }
    std::vector<size_t> kindOrder(size_t i) const {
        std::vector<size_t> o;
        if (i == 0 || i >= ps_.sections.size()) return o;
        for (size_t k = 1; k < ps_.sections.size(); ++k)
            if (ps_.sections[k].kind == ps_.sections[i].kind) o.push_back(k);
        std::stable_sort(o.begin(), o.end(), [&](size_t a, size_t b) { return appliedBefore(a, b); });
        return o;
    }
    void touch(size_t i) {
        if (i == 0) {
            globalDirty_ = true;
            return;
        }
        if (i >= st_.size()) return;
        st_[i].dirty = true;
        if (st_[i].home == SecState::LEGACY) globalDirty_ = true;
    }
    void reindex() {
        ProfileSet fresh;  // drops the stale search index
        fresh.sections = ps_.sections;
        ps_ = fresh;
    }
    // Numbers the profiles of a kind 10, 20, 30, ... in their applied order (a smaller step for very many, as in the
    // migration), leaving room below and above for one more.
    bool renumberKind(Section::Kind kind, std::string *err) {
        std::vector<size_t> o;
        for (size_t k = 1; k < ps_.sections.size(); ++k)
            if (ps_.sections[k].kind == kind) o.push_back(k);
        const int step = std::max(1, std::min(10, int(kMaxPriority) / int(o.size() + 2)));
        for (size_t k = 0; k < o.size(); ++k) {
            const int want = int(k + 1) * step;
            if (want == sectionPriority(ps_.sections[o[k]])) continue;
            if (!store(o[k], "PRIORITY", std::to_string(want), err)) return false;
        }
        resort();
        return true;
    }
    // Keeps the profiles of each kind in applied order (PRIORITY ascending; ties as the loader breaks them: profiles
    // of dofscroll.conf first, then by file name, so the order shown is the order the daemon uses). The slots of
    // the two kinds stay where they are (the daemon applies all devices before all programs whatever their index).
    void resort() {
        std::vector<size_t> idx(ps_.sections.size());
        for (size_t k = 0; k < idx.size(); ++k) idx[k] = k;
        for (Section::Kind kind : {Section::DEVICE, Section::APP}) {
            std::vector<size_t> places;
            for (size_t k = 1; k < idx.size(); ++k)
                if (ps_.sections[k].kind == kind) places.push_back(k);
            std::vector<size_t> sorted = places;
            st_.resize(ps_.sections.size());
            std::stable_sort(sorted.begin(), sorted.end(), [&](size_t a, size_t b) { return appliedBefore(a, b); });
            for (size_t k = 0; k < places.size(); ++k) idx[places[k]] = sorted[k];
        }
        std::vector<Section> ns;
        std::vector<SecState> nt;
        st_.resize(ps_.sections.size());
        for (size_t k : idx) {
            ns.push_back(ps_.sections[k]);
            nt.push_back(st_[k]);
        }
        ps_.sections = ns;
        st_ = nt;
        reindex();
        for (size_t k = 1; k < st_.size(); ++k) st_[k].file.section = k;
    }
    bool loadImpl(const std::string &path, std::vector<std::string> *warnings, std::string *err, bool keepProblems) {
        ProfileSet ps;
        std::vector<FileDiag> dg;
        if (isGlobalName(path)) {
            if (!loadProfileTreeAt(path, &ps, &dg)) {
                if (err) *err = "cannot read " + path;
                return false;
            }
        } else {  // a differently named file is a single file: no devices/ and apps/ beside it
            FileDiag g;
            g.origin = FileDiag::GLOBAL_FILE;
            g.path = path;
            g.stamp = profileFileStamp(path, true);
            g.section = 0;
            if (!loadLegacyProfileFile(path, &ps, &g.warnings)) {
                if (err) *err = "cannot read " + path;
                return false;
            }
            dg.push_back(g);
            for (size_t i = 1; i < ps.sections.size(); ++i) {
                FileDiag d;
                d.origin = FileDiag::LEGACY_SECTION;
                d.kind = ps.sections[i].kind;
                d.name = ps.sections[i].pattern;
                d.path = path;
                d.section = i;
                dg.push_back(d);
            }
        }
        ps_ = ps;
        path_ = path;
        diags_ = dg;
        st_.assign(ps_.sections.size(), SecState());
        removed_.clear();
        if (!keepProblems) problems_.clear();
        globalDirty_ = false;
        forceAll_ = false;
        globalStamp_.clear();
        for (const FileDiag &d : dg) {
            if (warnings)
                for (const std::string &w : d.warnings) warnings->push_back(w);
            if (d.origin == FileDiag::GLOBAL_FILE) globalStamp_ = d.stamp;
            if (d.fatal || d.section == ProfileSet::npos || d.section == 0 || d.section >= st_.size()) continue;
            if (d.origin == FileDiag::PROFILE_FILE) {
                st_[d.section].home = SecState::FILE;
                st_[d.section].file = d;
            } else if (d.origin == FileDiag::LEGACY_SECTION) {
                st_[d.section].home = SecState::LEGACY;
            }
        }
        newInFiles_ = filesAllowed(path);
        return true;
    }

    static std::string upper(std::string s) {
        for (char &c : s) c = char(spn::utf8::upperChar(c));
        return s;
    }
    static bool isButtonKey(const std::string &k) { return k.rfind("BUTTON_", 0) == 0; }
    static bool isLegacyButtonKey(const std::string &k) {
        return k == "NAV_BUTTON" || k == "NAV_BUTTON_ENABLED" || k == "CLICK_BUTTON" || k == "CLICK_BUTTON_ENABLED";
    }
    // V_ACTION and friends (the channel actions of a profile) are written only when a profile sets them, so they are
    // not in allKeys(Settings()).
    static bool isActionKey(const std::string &k) {
        return k == "V_ACTION" || k == "H_ACTION" || k == "TWIST_ACTION" || k == "V_PUSH_ACTION" || k == "V_TILT_ACTION" ||
               k == "H_PUSH_ACTION" || k == "H_TILT_ACTION";
    }
    static bool isEditableKey(const std::string &k) {
        static const std::vector<KV> all = allKeys(Settings());
        return lookup(all, k) != nullptr || isActionKey(k);
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
        touch(i);
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
    std::vector<SecState> st_;        // parallel to ps_.sections
    std::vector<FileDiag> diags_;     // what the loader said about every file (defective ones, stamps)
    std::vector<Removed> removed_;    // profile files to delete at the next save
    std::map<std::string, std::string> problems_;  // keyOf(profile) -> what went wrong with it
    bool globalDirty_ = false;        // [global] or a profile of dofscroll.conf changed
    bool reloadedByConflict_ = false;
    bool forceAll_ = false;           // setProfiles: the next save writes everything
    bool newInFiles_ = true;          // new profiles get their own file (split layout)
    std::string globalStamp_;         // stamp of dofscroll.conf as read or written
    int limit_ = int(kMaxButtons);
    ReloadFn reload_;
    ModeSet modes_;
    std::string modesDir_;
};

}  // namespace spn::ui
