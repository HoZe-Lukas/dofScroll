// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Differential test between the settings window and the daemon: random edits go through SettingsModel (the real
// editing and save path), the files it writes are read back by the daemon's loader, and what the window shows
// (the order of the profiles and their settings) must be what the daemon computes.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "profile_files.hpp"
#include "testutil.hpp"
#include "ui/model/settings_model.hpp"
#include "ui/model/strings.hpp"

using namespace spn;
using namespace spn::ui;

namespace {

const char *const kKeys[] = {"SCROLL_V_SENS", "SCROLL_H_SENS", "ZOOM_SENS", "SCROLL_V_DEADZONE", "ZOOM_SIGN", "ZOOM_MODIFIER", "MODE"};
const char *const kVals[][3] = {{"2", "7", "11"}, {"1", "4", "9"}, {"3", "6", "12"}, {"10", "30", "50"}, {"1", "-1", "1"},
                                {"ctrl", "shift", "none"}, {"normal", "normal", "normal"}};

std::string describe(const std::vector<Section> &s) {
    std::string out;
    for (const Section &x : s) out += std::string(x.kind == Section::DEVICE ? "D:" : x.kind == Section::APP ? "A:" : "G:") + x.pattern + " ";
    return out;
}

// Applied order per kind, as the daemon would use it.
std::vector<std::string> orderOf(const ProfileSet &ps, Section::Kind k) {
    std::vector<std::string> o;
    for (size_t i = 1; i < ps.sections.size(); ++i)
        if (ps.sections[i].kind == k) o.push_back(ps.sections[i].pattern);
    return o;
}

// The settings the daemon resolves for a device / program name that matches only the profile `pattern`.
bool sameResolved(const Settings &a, const Settings &b) { return sameSettings(a, b); }

}  // namespace

int main() {
    const IdentityStrings str;
    int cases = 0;
    for (int seed = 1; seed <= 3000; ++seed) {
        std::mt19937 rng{static_cast<unsigned>(seed)};
        auto pick = [&](size_t n) { return size_t(rng() % n); };
        const std::string dir = tu::makeTempDir("dofscroll-uidiff");
        // Start: a legacy single file with overlapping profiles (half of the cases), or empty.
        if (seed % 2) {
            std::ofstream f(dir + "/dofscroll.conf", std::ios::binary);
            f << "[global]\nSCROLL_V_SENS = 5\n\n[app:edit]\nSCROLL_V_SENS = 3\nZOOM_SENS = 4\n\n[app:editor]\nSCROLL_V_SENS = 9\n\n"
                 "[device:Space]\nSCROLL_H_SENS = 8\n\n[device:SpaceMouse]\nSCROLL_H_SENS = 2\n";
        }
        SettingsModel m;
        std::vector<std::string> warn;
        std::string err;
        assert(m.load(dir + "/dofscroll.conf", &warn, &err));
        const int ops = 3 + int(pick(25));
        std::string trace;
        for (int o = 0; o < ops; ++o) {
            const size_t n = m.profiles().sections.size();
            const int op = int(pick(10));
            std::string e;
            if (op == 0 || n == 1) {
                const bool app = pick(2) == 0;
                static const char *const names[] = {"edit", "editor", "Space", "SpaceMouse", "mouse", "x"};
                const std::string nm = names[pick(6)];
                m.addSection(app ? Section::APP : Section::DEVICE, nm, &e, pick(2) == 0);
                trace += "add " + nm + "; ";
            } else if (op <= 3) {
                const size_t i = pick(n);
                const size_t k = pick(7);
                m.setValue(i, kKeys[k], kVals[k][pick(3)], &e);
                trace += "set " + std::to_string(i) + " " + kKeys[k] + "; ";
            } else if (op == 4) {
                const size_t i = 1 + pick(n - 1);
                size_t ni;
                m.moveProfile(i, pick(2) == 0, &ni, &e);
                trace += "move " + std::to_string(i) + "; ";
            } else if (op == 5) {
                m.removeSection(1 + pick(n - 1), &e);
                trace += "remove; ";
            } else if (op == 6) {
                m.clearOverride(pick(n), kKeys[pick(7)]);
                trace += "clear; ";
            } else if (op == 8) {
                const size_t i = 1 + pick(n - 1);
                static const char *const names[] = {"edit", "Edit2", "Space", "mouse", "x", "Zed y"};
                m.renameSection(i, names[pick(6)], &e);
                trace += "rename " + std::to_string(i) + "; ";
            } else if (op == 9) {
                const size_t i = pick(n);
                const int b = 1 + int(pick(6));
                static const char *const acts[] = {"click", "none", "rightclick", "nav", "key:enter"};
                m.setButton(i, b, acts[pick(5)], &e);
                trace += "button " + std::to_string(i) + "; ";
            } else {
                // save at a random point, as the autosave does
                m.save(str);
                trace += "save; ";
            }
        }
        const SettingsModel::SaveResult r = m.save(str);
        (void)r;
        // The daemon reads the files.
        ProfileSet dp;
        std::vector<FileDiag> diag;
        assert(loadProfileTreeAt(dir + "/dofscroll.conf", &dp, &diag));
        const ProfileSet &mp = m.profiles();
        auto fail = [&](const char *what) {
            std::cerr << "seed " << seed << ": " << what << "\n  trace: " << trace << "\n  window: " << describe(mp.sections)
                      << "\n  daemon: " << describe(dp.sections) << "\n";
            std::abort();
        };
        for (Section::Kind k : {Section::DEVICE, Section::APP})
            if (orderOf(mp, k) != orderOf(dp, k)) fail("order differs");
        // every device / program name that matches some profile
        for (const char *dev : {"", "Space", "SpaceMouse Pro", "mouse", "x", "SpaceMouse Compact"})
            for (const char *app : {"", "edit", "editor", "xedit", "x"})
                if (!sameResolved(mp.resolve(dev, app), dp.resolve(dev, app))) fail("resolved settings differ");
        ++cases;
    }
    std::cout << "ok " << cases << " cases\n";
    return 0;
}
