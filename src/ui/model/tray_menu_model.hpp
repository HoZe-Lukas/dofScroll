// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// TrayMenuModel: the tray menu as plain data. buildTrayMenu() turns the status and the loaded
// modes into entries (status lines, Mode submenu with one radio item per mode, Enabled toggle,
// open settings/config/log/modes folder, Quit). The view renders the entries and maps a click
// to performTrayAction().
//
// Layout (indices in TrayMenu::entries are stable; empty status lines have visible == false):
//   0 status main line (Info)   1 status info line (Info)   2 warning line (Info)
//   3 separator
//   4 "Mode" submenu with Radio children (arg = mode id, checked = the global mode)
//   5 "Enabled" check item (ENABLED of [global])
//   6 separator
//   7 Settings...   8 Open config file   9 Show log...   10 Open modes folder
//   11 Release all keys (emergency: runs `dofscroll --release-keys`)
//   12 Restart daemon (releases held keys, then restarts the daemon)
//   13 Restart interface (restarts this UI process)
//   14 separator    15 Quit
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "mode_list.hpp"
#include "ui/model/action_runner.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/strings.hpp"

namespace spn::ui {

enum class TrayAction { None, SetMode, ToggleEnabled, OpenSettings, OpenConfig, OpenLog, OpenModesFolder, ReleaseAllKeys, RestartDaemon, RestartInterface, Quit };

struct TrayEntry {
    enum class Kind { Info, Separator, Item, Radio, Check, Submenu } kind = Kind::Item;
    std::string text;
    bool visible = true;
    bool enabled = true;   // false: shown greyed out
    bool checked = false;  // Radio / Check
    TrayAction action = TrayAction::None;
    std::string arg;                  // SetMode: the mode id
    std::vector<TrayEntry> children;  // Submenu
};

struct TrayMenu {
    std::string title;  // tooltip / title
    std::vector<TrayEntry> entries;
};

struct TrayMenuInput {
    const StatusModel *status = nullptr;  // nullptr: treated as "no status"
    const ModeSet *modes = nullptr;       // nullptr: empty Mode submenu
    std::string globalMode = "normal";    // effective MODE of [global] (modelist::globalModeId)
    bool globalEnabled = true;            // ENABLED of [global]
};

inline TrayMenu buildTrayMenu(const TrayMenuInput &in, const StringProvider &s) {
    TrayMenu m;
    const StatusModel none;
    const StatusModel &st = in.status ? *in.status : none;
    const std::string modeLabel =
        in.modes && !st.modeId().empty() ? modelist::modeLabel(*in.modes, st.modeId()) : std::string();
    m.title = st.title(s);
    auto info = [](const std::string &t) {
        TrayEntry e;
        e.kind = TrayEntry::Kind::Info;
        e.text = t;
        e.visible = !t.empty();
        e.enabled = false;
        return e;
    };
    auto sep = [] {
        TrayEntry e;
        e.kind = TrayEntry::Kind::Separator;
        return e;
    };
    auto item = [&s](const char *src, TrayAction a) {
        TrayEntry e;
        e.text = s.tr(src);
        e.action = a;
        return e;
    };
    m.entries.push_back(info(st.mainLine(s, modeLabel)));
    m.entries.push_back(info(st.infoLine(s)));
    m.entries.push_back(info(st.warningLine(s)));
    m.entries.push_back(sep());

    TrayEntry mode;
    mode.kind = TrayEntry::Kind::Submenu;
    mode.text = s.tr("Mode");
    if (in.modes)
        for (const auto &e : modelist::buildMenuEntries(*in.modes, in.globalMode)) {
            TrayEntry r;
            r.kind = TrayEntry::Kind::Radio;
            r.text = e.text;
            r.checked = e.active;
            r.action = TrayAction::SetMode;
            r.arg = e.id;
            mode.children.push_back(r);
        }
    m.entries.push_back(mode);
    TrayEntry en = item("Enabled", TrayAction::ToggleEnabled);
    en.kind = TrayEntry::Kind::Check;
    en.checked = in.globalEnabled;
    m.entries.push_back(en);
    m.entries.push_back(sep());
    m.entries.push_back(item("Settings...", TrayAction::OpenSettings));
    m.entries.push_back(item("Open config file", TrayAction::OpenConfig));
    m.entries.push_back(item("Show log...", TrayAction::OpenLog));
    m.entries.push_back(item("Open modes folder", TrayAction::OpenModesFolder));
    m.entries.push_back(item("Release all keys", TrayAction::ReleaseAllKeys));
    m.entries.push_back(item("Restart daemon", TrayAction::RestartDaemon));
    m.entries.push_back(item("Restart interface", TrayAction::RestartInterface));
    m.entries.push_back(sep());
    m.entries.push_back(item("Quit", TrayAction::Quit));
    return m;
}

// What performTrayAction needs from the application. Unset callbacks are skipped.
struct TrayHooks {
    std::string configPath, modesDir;                 // for OpenConfig / OpenModesFolder
    std::function<void(const std::string &)> setMode; // pick a mode (writes [global] MODE)
    std::function<void(bool)> setEnabled;             // new state of the Enabled toggle
    std::function<void()> openSettings, showLog, quit;
};

// Runs the action of a clicked entry. `entry.checked` of a Check item is the state BEFORE the
// click. Returns false (and *err) only if the ActionRunner failed.
inline bool performTrayAction(const TrayEntry &entry, ActionRunner &runner, const TrayHooks &h,
                              std::string *err) {
    switch (entry.action) {
        case TrayAction::None: return true;
        case TrayAction::SetMode:
            if (h.setMode) h.setMode(entry.arg);
            return true;
        case TrayAction::ToggleEnabled:
            if (h.setEnabled) h.setEnabled(!entry.checked);
            return true;
        case TrayAction::OpenSettings:
            if (h.openSettings) h.openSettings();
            return true;
        case TrayAction::OpenLog:
            if (h.showLog) h.showLog();
            return true;
        case TrayAction::Quit:
            if (h.quit) h.quit();
            return true;
        case TrayAction::OpenConfig: return runner.openPath(h.configPath, err);
        case TrayAction::OpenModesFolder: return runner.openPath(h.modesDir, err);
        case TrayAction::ReleaseAllKeys: {
            // Also a reload request: the daemon clears its toggle state (toggle:nav, toggle:mode)
            // on a reload; the separate --release-keys process cannot reach it.
            const bool ok = runner.releaseKeys(err);
            std::string ignored;
            runner.requestReload(&ignored);
            return ok;
        }
        case TrayAction::RestartDaemon: {
            // Keys first (a failure here must not block the restart: the daemon releases on quit).
            std::string ignored;
            runner.releaseKeys(&ignored);
            return runner.restartDaemon(err);
        }
        case TrayAction::RestartInterface:
            if (!runner.restartInterface(err)) return false;
            if (h.quit) h.quit();
            return true;
    }
    return true;
}

}  // namespace spn::ui
