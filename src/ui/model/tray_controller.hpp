// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// TrayController: everything the tray icon does except drawing (toolkit-free, unit-tested).
// It reads the daemon's status file, the configuration and the modes directory, builds the
// menu (tray_menu_model.hpp), derives icon state and tooltip, and performs the clicked action.
//
//   TrayController c(paths, runner, strings);
//   c.hooks().openSettings = ...; c.hooks().quit = ...;   // view side effects
//   changed = c.refresh(nowSeconds);                      // about once a second
//   c.menu(), c.icon(), c.tooltip()                       // render these
//   c.perform(entry);                                     // a menu entry was clicked
//
// The "Mode" submenu is a RUNTIME switch, not a setting: the chosen mode is handed to the daemon
// as a request file in the runtime directory followed by a reload request (src/mode_request.hpp),
// so MODE is never written into [global] (global is the default mode only). The radio item that
// is checked is the mode the daemon reports as active (the default mode while it is not running).
// "Enabled" edits ENABLED in [global] through the SettingsModel (atomic save, then reload).
// Mode requests last until the next reload (any config change), a restart or a button switch.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "common.hpp"
#include "mode_request.hpp"
#include "platform/fs.hpp"
#include "ui/model/action_runner.hpp"
#include "ui/model/mode_catalog.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/status_model.hpp"
#include "ui/model/tray_menu_model.hpp"

namespace spn::ui {

class TrayController {
public:
    enum class Icon { NotRunning, Enabled, Disabled };
    struct Paths {
        std::string configPath, modesDir, runtimeDir;  // runtimeDir may be empty (no status/log)
    };

    TrayController(const Paths &paths, ActionRunner &runner, const StringProvider &strings)
        : paths_(paths), runner_(runner), s_(strings), catalog_(paths.modesDir) {
        refresh(0, true);
    }

    TrayHooks &hooks() { return hooks_; }
    const TrayMenu &menu() const { return menu_; }
    Icon icon() const { return icon_; }
    const std::string &tooltip() const { return tooltip_; }
    const StatusModel &status() const { return status_; }
    const ModeCatalog &catalog() const { return catalog_; }
    const std::string &lastError() const { return error_; }  // of the last perform(), "" if it worked

    // Re-reads status (fresh data needs `nowSeconds`, unix time), config (when its file changed)
    // and modes (when the folder changed); rebuilds menu, icon and tooltip. true if anything the
    // view shows changed.
    bool refresh(int64_t nowSeconds) { return refresh(nowSeconds, false); }

    // Runs the action of a clicked entry; false (and lastError()) if it failed.
    bool perform(const TrayEntry &entry) {
        error_.clear();
        TrayHooks h = hooks_;
        h.configPath = paths_.configPath;
        h.modesDir = paths_.modesDir;
        h.setMode = [this](const std::string &id) { requestMode(id); };
        h.setEnabled = [this](bool on) { setEnabled(on); };
        h.showLog = [this] {
            const std::string p = runtimeFilePath(paths_.runtimeDir, "dofscroll.log");
            if (p.empty()) error_ = s_.tr("No log file: the runtime directory is not available");
            else if (!runner_.openPath(p, &error_) && error_.empty()) error_ = "cannot open the log file";
        };
        std::string err;
        if (entry.action == TrayAction::OpenModesFolder) fs::mkdirsPrivate(paths_.modesDir, &err);  // may not exist yet
        err.clear();
        bool ok = performTrayAction(entry, runner_, h, &err);
        if (!ok) error_ = err.empty() ? std::string("action failed") : err;
        return ok && error_.empty();
    }

private:
    bool refresh(int64_t now, bool first) {
        std::string content;
        fs::ReadOptions opt;
        opt.refuseLinks = true;
        opt.requireOwner = true;
        const std::string sp = runtimeFilePath(paths_.runtimeDir, "dofscroll-status");
        const bool readable = !sp.empty() && fs::readFileBounded(sp, kStatusMaxBytes, opt, &content).ok();
        status_.update(content, readable, now);
        catalog_.refreshIfChanged();
        loadConfigIfChanged();

        TrayMenuInput in;
        in.status = &status_;
        in.modes = &catalog_.modeSet();
        in.globalMode = status_.daemonRunning() && !status_.modeId().empty() ? status_.modeId() : globalMode_;
        in.globalEnabled = globalEnabled_;
        menu_ = buildTrayMenu(in, s_);
        const std::string modeLabel =
            status_.modeId().empty() ? std::string() : modelist::modeLabel(catalog_.modeSet(), status_.modeId());
        std::string tip = "dofscroll - " + status_.mainLine(s_, modeLabel);
        Icon ic = Icon::NotRunning;
        if (status_.state() == StatusModel::State::Running || status_.state() == StatusModel::State::Paused)
            ic = Icon::Enabled;
        else if (status_.state() == StatusModel::State::Disabled)
            ic = Icon::Disabled;
        const std::string sig = signature(menu_) + "|" + tip + "|" + std::to_string(int(ic));
        const bool changed = first || sig != signature_;
        signature_ = sig;
        icon_ = ic;
        tooltip_ = tip;
        return changed;
    }

    static std::string signature(const TrayMenu &m) {
        std::string t;
        auto add = [&t](const TrayEntry &e, auto &&self) -> void {
            t += std::to_string(int(e.kind)) + (e.visible ? "v" : "h") + (e.checked ? "c" : "u") + e.text + "\x1f" + e.arg + "\x1e";
            for (const TrayEntry &c : e.children) self(c, self);
        };
        for (const TrayEntry &e : m.entries) add(e, add);
        return t;
    }

    void loadConfigIfChanged() {
        fs::FileInfo fi;
        const bool have = fs::statPath(paths_.configPath, &fi);
        const std::string stamp = have ? std::to_string(fi.mtimeNs) + ":" + std::to_string(fi.size) : std::string("-");
        if (stamp == configStamp_) return;
        configStamp_ = stamp;
        SettingsModel m;
        std::string err;
        if (!m.load(paths_.configPath, nullptr, &err)) return;  // keep the previous values
        globalMode_ = modelist::globalModeId(m.profiles());
        SettingsModel::Value v;
        globalEnabled_ = !m.value(0, "ENABLED", &v) || v.text != "0";
    }

    void requestMode(const std::string &id) {
        std::string err;
        if (!writeModeRequest(paths_.runtimeDir, id, &err) || !runner_.requestReload(&err))
            error_ = err.empty() ? std::string("cannot request the mode") : err;
    }

    void setEnabled(bool on) {
        SettingsModel m;
        std::string err;
        std::vector<std::string> warnings;
        if (!m.load(paths_.configPath, &warnings, &err)) {
            error_ = err;
            return;
        }
        m.setReloadCallback([this](std::string *e) { return runner_.requestReload(e); });
        if (!m.setValue(0, "ENABLED", on ? "1" : "0", &err)) {
            error_ = err;
            return;
        }
        const SettingsModel::SaveResult r = m.save(s_);
        if (!r.saved || !r.reloaded) error_ = r.error.empty() ? std::string("cannot save the configuration") : r.error;
    }

    Paths paths_;
    ActionRunner &runner_;
    const StringProvider &s_;
    TrayHooks hooks_;
    StatusModel status_;
    ModeCatalog catalog_;
    TrayMenu menu_;
    Icon icon_ = Icon::NotRunning;
    std::string tooltip_, error_, signature_, configStamp_ = "?";
    std::string globalMode_ = "normal";
    bool globalEnabled_ = true;
};

}  // namespace spn::ui
