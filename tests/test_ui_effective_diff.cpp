// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Differential test between the settings window and the daemon: for random layered profiles (global, device, program,
// joystick modes with setting overrides, channel actions and buttons) the values the window shows for a profile
// (SettingsModel::effectiveWithMode) must be the values the daemon resolves for a device or program that matches only
// that profile (ProfileSet::resolve with the overrides of the mode the daemon selects, as DaemonCore::selectMode does).
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "profile_actions.hpp"
#include "profile_files.hpp"
#include "testutil.hpp"
#include "ui/model/settings_model.hpp"

using namespace spn;
using namespace spn::ui;

namespace {

const char *const kModeLines[] = {"SCROLL_V_SENS = 9", "SCROLL_H_SENS = 1", "ZOOM_SIGN = -1", "ZOOM_MODIFIER = shift", "V_ACTION = keys:up,down",
                                  "H_ACTION = wheel_v", "TWIST_ACTION = none", "V_TILT_ACTION = none", "V_PUSH_ACTION = zoom", "H_TILT_ACTION = keys:left,right",
                                  "BUTTON_1 = key:space", "BUTTON_2 = click", "BUTTON_3 = none", "LIFT_ACTION = step:none,click", "SCROLL_V_TILT_SENS = 3",
                                  "SCROLL_V_DEADZONE = 77", "INVERT_AXES = z"};
const char *const kProfileLines[] = {"SCROLL_V_SENS = 2", "SCROLL_V_SENS = 12", "SCROLL_H_SENS = 7", "ZOOM_SIGN = 1", "ZOOM_MODIFIER = ctrl", "V_ACTION = wheel_v",
                                     "H_ACTION = default", "TWIST_ACTION = zoom", "V_TILT_ACTION = keys:pageup,pagedown", "V_PUSH_ACTION = none",
                                     "BUTTON_1 = rightclick", "BUTTON_2 = none", "BUTTON_3 = key:enter", "LIFT_ACTION = none", "SCROLL_V_TILT_SENS = 6",
                                     "SCROLL_V_DEADZONE = 5", "INVERT_AXES = none", "MODE = a", "MODE = b", "MODE = normal", "MODE = nosuch", "ENABLED = 0"};

std::string lower(std::string s) {
    for (char &c : s) c = char(std::tolower((unsigned char)c));
    return s;
}

bool contains(const std::string &hay, const std::string &needle) { return lower(hay).find(lower(needle)) != std::string::npos; }

}  // namespace

int main() {
    int cases = 0, compared = 0;
    for (int seed = 1; seed <= 4000; ++seed) {
        std::mt19937 rng{static_cast<unsigned>(seed)};
        auto pick = [&](size_t n) { return size_t(rng() % n); };
        const std::string dir = tu::makeTempDir("dofscroll-effdiff");
        std::filesystem::create_directories(dir + "/modes");
        for (const char *id : {"a", "b"}) {
            std::ofstream f(dir + "/modes/" + id + ".conf", std::ios::binary);
            f << "NAME = " << id << "\n";
            const size_t n = pick(6);
            for (size_t k = 0; k < n; ++k) f << kModeLines[pick(sizeof kModeLines / sizeof *kModeLines)] << "\n";
        }
        std::string text = "[global]\n";
        for (size_t k = pick(4); k > 0; --k) text += std::string(kProfileLines[pick(sizeof kProfileLines / sizeof *kProfileLines)]) + "\n";
        static const char *const devs[] = {"Pro", "Compact", "Wireless"};
        static const char *const apps[] = {"edit", "view", "code"};
        for (int k = 0; k < 3; ++k) {
            if (pick(2)) continue;
            text += std::string("\n[device:") + devs[k] + "]\n";
            for (size_t j = pick(4); j > 0; --j) text += std::string(kProfileLines[pick(sizeof kProfileLines / sizeof *kProfileLines)]) + "\n";
        }
        for (int k = 0; k < 3; ++k) {
            if (pick(2)) continue;
            text += std::string("\n[app:") + apps[k] + "]\n";
            for (size_t j = pick(5); j > 0; --j) text += std::string(kProfileLines[pick(sizeof kProfileLines / sizeof *kProfileLines)]) + "\n";
        }
        std::ofstream(dir + "/dofscroll.conf", std::ios::binary) << text;

        SettingsModel m;
        std::vector<std::string> warn;
        std::string err;
        assert(m.load(dir + "/dofscroll.conf", &warn, &err));
        const ModeSet modes = loadModes(dir + "/modes", nullptr);
        m.setModes(modes, dir + "/modes");
        ProfileSet dp;
        std::vector<FileDiag> diag;
        assert(loadProfileTreeAt(dir + "/dofscroll.conf", &dp, &diag));

        auto daemon = [&](const std::string &dev, const std::string &app) {
            // DaemonCore::applyProfile / selectMode
            Settings first = dp.resolve(dev, app);
            const ModeDef *md = modes.find(first.mode);
            if (!md) md = &modes.normal();
            return dp.resolve(dev, app, &md->overrides);
        };
        for (size_t i = 1; i < m.profiles().sections.size(); ++i) {
            const Section &sec = m.profiles().sections[i];
            // a name that matches only this profile
            bool alone = true;
            for (size_t k = 1; k < m.profiles().sections.size(); ++k)
                if (k != i && m.profiles().sections[k].kind == sec.kind && contains(sec.pattern, m.profiles().sections[k].pattern)) alone = false;
            if (!alone) continue;
            const Settings ui = m.effectiveWithMode(i);
            const Settings dm = sec.kind == Section::DEVICE ? daemon(sec.pattern, "") : daemon("", sec.pattern);
            ++compared;
            if (!sameSettings(ui, dm)) {
                std::cerr << "seed " << seed << ": profile " << sec.pattern << " differs\n" << text << "\n";
                const auto a = allKeys(ui), b = allKeys(dm);
                for (size_t k = 0; k < a.size() && k < b.size(); ++k)
                    if (a[k] != b[k]) std::cerr << "  window " << a[k].first << "=" << a[k].second << "  daemon " << b[k].first << "=" << b[k].second << "\n";
                std::abort();
            }
        }
        ++cases;
    }
    std::cout << "ok " << cases << " cases, " << compared << " profiles compared\n";
    return 0;
}
