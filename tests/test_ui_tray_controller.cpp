// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit tests for src/ui/model/tray_controller.hpp and src/mode_request.hpp.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "mode_request.hpp"
#include "testutil.hpp"
#include "ui/model/tray_controller.hpp"

using namespace spn;
using namespace spn::ui;
using Kind = RecordingActionRunner::Kind;

static void put(const std::string &path, const std::string &text) {
    std::string err;
    if (!fs::writeFileReplace(path, text, &err)) {
        std::fprintf(stderr, "write %s: %s\n", path.c_str(), err.c_str());
        std::abort();
    }
}
static std::string slurp(const std::string &path) {
    std::string t;
    fs::readFileBounded(path, 1 << 20, fs::ReadOptions(), &t);
    return t;
}
static std::string statusFile(const std::string &mode, int64_t updated, const char *enabled = "1") {
    return "profile=global\ndevice=Test\napp=x\nmode=" + mode + "\nenabled=" + enabled +
           "\npaused=0\ndevices_seen=1\nupdated=" + std::to_string(updated) + "\n";
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll tray \xC3\xA4\xC3\xB6 \xE2\x9C\x93");
    TrayController::Paths p;
    p.configPath = root + "/dofscroll.conf";
    p.modesDir = root + "/modes";
    p.runtimeDir = root + "/run";
    put(p.configPath, "[global]\nSCROLL_V_SENS = 5\n");
    put(p.modesDir + "/mine.conf", "NAME = Mine\nBUTTON_1 = key:space\n");
    IdentityStrings str;
    RecordingActionRunner runner;
    TrayController c(p, runner, str);
    const int64_t now = 1000;

    // ---- no status file: not running, default mode checked, all modes listed ----
    c.refresh(now);
    assert(c.icon() == TrayController::Icon::NotRunning);
    assert(c.menu().entries.size() == 13);
    const TrayEntry &modeMenu = c.menu().entries[4];
    assert(modeMenu.kind == TrayEntry::Kind::Submenu && modeMenu.children.size() == 4);  // normal, off, timeline, mine
    int checked = 0;
    for (const auto &r : modeMenu.children)
        if (r.checked) {
            ++checked;
            assert(r.arg == "normal");
        }
    assert(checked == 1 && c.menu().entries[5].checked);

    // ---- running with the timeline mode: that radio item is checked, icon enabled ----
    put(p.runtimeDir + "/dofscroll-status", statusFile("timeline", now));
    assert(c.refresh(now));
    assert(c.icon() == TrayController::Icon::Enabled);
    for (const auto &r : c.menu().entries[4].children) assert(r.checked == (r.arg == "timeline"));
    assert(c.tooltip().find("Mode:") != std::string::npos);
    assert(!c.refresh(now));  // nothing changed
    put(p.runtimeDir + "/dofscroll-status", statusFile("normal", now, "0"));
    c.refresh(now);
    assert(c.icon() == TrayController::Icon::Disabled);
    c.refresh(now + 60);  // stale
    assert(c.icon() == TrayController::Icon::NotRunning);

    // ---- picking a mode: request file + reload, [global] untouched ----
    const std::string before = slurp(p.configPath);
    TrayEntry pick;
    pick.kind = TrayEntry::Kind::Radio;
    pick.action = TrayAction::SetMode;
    pick.arg = "mine";
    assert(c.perform(pick) && c.lastError().empty());
    assert(runner.count(Kind::RequestReload) == 1);
    assert(slurp(p.configPath) == before && before.find("MODE") == std::string::npos);
    assert(takeModeRequest(p.runtimeDir) == "mine" && takeModeRequest(p.runtimeDir).empty());
    pick.arg = "../etc/passwd";
    assert(!c.perform(pick) && !c.lastError().empty());
    assert(runner.count(Kind::RequestReload) == 1 && !tu::exists(modeRequestPath(p.runtimeDir)));
    runner.result = false;
    pick.arg = "off";
    assert(!c.perform(pick) && c.lastError() == "failed");
    runner.result = true;
    clearModeRequest(p.runtimeDir);

    // ---- request file hygiene ----
    put(modeRequestPath(p.runtimeDir), std::string(200, 'a'));
    assert(takeModeRequest(p.runtimeDir).empty() && !tu::exists(modeRequestPath(p.runtimeDir)));
    put(modeRequestPath(p.runtimeDir), "Not Valid!\n");
    assert(takeModeRequest(p.runtimeDir).empty() && !tu::exists(modeRequestPath(p.runtimeDir)));
    assert(!writeModeRequest("", "off", nullptr) && !writeModeRequest(p.runtimeDir, "a/b", nullptr));
    assert(takeModeRequest("").empty());

    // ---- Enabled toggle: ENABLED in [global], then reload ----
    TrayEntry en = c.menu().entries[5];
    assert(en.action == TrayAction::ToggleEnabled && en.checked);
    const int reloads = runner.count(Kind::RequestReload);
    assert(c.perform(en));
    assert(runner.count(Kind::RequestReload) == reloads + 1);
    assert(slurp(p.configPath).find("ENABLED = 0") != std::string::npos);
    assert(slurp(p.configPath).find("SCROLL_V_SENS = 5") != std::string::npos);
    c.refresh(now);
    assert(!c.menu().entries[5].checked);

    // ---- open actions go through the runner ----
    runner.calls.clear();
    for (size_t i : {size_t(8), size_t(9), size_t(10)}) assert(c.perform(c.menu().entries[i]));
    assert(runner.calls.size() == 3 && runner.calls[0].path == p.configPath &&
           runner.calls[1].path == p.runtimeDir + "/dofscroll.log" && runner.calls[2].path == p.modesDir);
    bool opened = false, quit = false;
    c.hooks().openSettings = [&] { opened = true; };
    c.hooks().quit = [&] { quit = true; };
    assert(c.perform(c.menu().entries[7]) && opened && c.perform(c.menu().entries[12]) && quit);

    // no runtime directory: no log, no mode request, but no crash
    TrayController::Paths np = p;
    np.runtimeDir = "";
    TrayController nc(np, runner, str);
    nc.refresh(now);
    assert(nc.icon() == TrayController::Icon::NotRunning);
    assert(!nc.perform(nc.menu().entries[9]) && !nc.lastError().empty());
    pick.arg = "off";
    assert(!nc.perform(pick));

    // ---- UTF-8: display names stay intact, ids stay ASCII, paths with spaces and non-ASCII work ----
    {
        const std::string udir = root + "/modes \xC3\xA4 u";
        const std::string uname = "Gr\xC3\xB6\xC3\x9F" "e \xE2\x9C\x93";
        ModeCatalog uc(udir);
        const ModeCatalog::Result cr = uc.createNew(uname);
        assert(cr.ok && validModeId(cr.id));
        for (unsigned char ch : cr.id) assert(ch < 128);
        const ModeCatalog::ModeEntry *e = uc.find(cr.id);
        assert(e && e->label == uname);
        std::string text = slurp(cr.path);
        assert(text.compare(0, 3, "\xEF\xBB\xBF") != 0 && text.find("NAME = " + uname) != std::string::npos);
        const std::string src = root + "/import \xC3\xA4 file.conf";
        put(src, "NAME = \xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\nDESCRIPTION = caf\xC3\xA9\n");
        const ModeCatalog::Result ir = uc.importFile(src, "nihongo");
        assert(ir.ok && uc.find("nihongo") && uc.find("nihongo")->label == "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E");
        assert(uc.find("nihongo")->description == "caf\xC3\xA9");
        // tray menu and tooltip carry the Unicode label unchanged
        TrayController::Paths up = p;
        up.modesDir = udir;
        put(up.runtimeDir + "/dofscroll-status", statusFile("nihongo", now));
        TrayController uctl(up, runner, str);
        uctl.refresh(now);
        bool found = false;
        for (const auto &r : uctl.menu().entries[4].children)
            if (r.text.find("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") != std::string::npos) found = true;
        assert(found && uctl.tooltip().find("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") != std::string::npos);
        // a long name is cut on a code point boundary
        std::string longName;
        for (int i = 0; i < 40; ++i) longName += "\xC3\xA4";
        const std::string cl = ModeCatalog::cleanLabel(longName);
        assert(cl.size() <= 64 && cl.size() % 2 == 0);
    }

    tu::removeAll(root);
    std::puts("test_ui_tray_controller: ok");
    return 0;
}
