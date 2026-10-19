// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// ActionRunner: the platform actions the UI needs, behind one interface. The Linux, Windows and
// macOS implementations (xdg-open/systemctl, ShellExecute/Run key, open/LaunchAgent, signal or
// pipe reload) live with the platform code; the models and tests only use this interface.
#pragma once
#include <functional>
#include <string>
#include <vector>

namespace spn::ui {

class ActionRunner {
public:
    virtual ~ActionRunner() = default;
    // Opens a file or folder with the system's default application. false + *err on failure.
    virtual bool openPath(const std::string &path, std::string *err) = 0;
    // "Start at login" on/off (affects the next login only). false + *err on failure.
    virtual bool setAutostart(bool enable, std::string *err) = 0;
    // Asks the daemon to re-read the configuration (SIGHUP or the platform equivalent).
    virtual bool requestReload(std::string *err) = 0;
    // Emergency exit for a stuck key or button: runs `dofscroll --release-keys` as a separate process
    // (argv, no shell; see release_keys.hpp). Does not wait for it. false + *err if it did not start.
    virtual bool releaseKeys(std::string *err) = 0;
};

// The reload callback of the SettingsModel (and of every page that saves): one requestReload per
// call, so a page never has to know which OS channel the daemon listens on. The runner must outlive
// the callback.
inline std::function<bool(std::string *err)> reloadVia(ActionRunner &runner) {
    return [&runner](std::string *err) { return runner.requestReload(err); };
}

// Records the calls and returns the configured result (tests).
class RecordingActionRunner : public ActionRunner {
public:
    enum class Kind { OpenPath, SetAutostart, RequestReload, ReleaseKeys };
    struct Call {
        Kind kind;
        std::string path;     // OpenPath
        bool enable = false;  // SetAutostart
    };
    std::vector<Call> calls;
    bool result = true;            // return value of every call
    std::string error = "failed";  // *err when result is false

    bool openPath(const std::string &path, std::string *err) override {
        return rec({Kind::OpenPath, path, false}, err);
    }
    bool setAutostart(bool enable, std::string *err) override {
        return rec({Kind::SetAutostart, "", enable}, err);
    }
    bool requestReload(std::string *err) override { return rec({Kind::RequestReload, "", false}, err); }
    bool releaseKeys(std::string *err) override { return rec({Kind::ReleaseKeys, "", false}, err); }
    int count(Kind k) const {
        int n = 0;
        for (const auto &c : calls) n += c.kind == k;
        return n;
    }

private:
    bool rec(const Call &c, std::string *err) {
        calls.push_back(c);
        if (!result && err) *err = error;
        return result;
    }
};

}  // namespace spn::ui
