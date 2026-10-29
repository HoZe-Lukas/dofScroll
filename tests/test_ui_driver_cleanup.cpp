// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Cleanup of the 3Dconnexion driver's per-program axis settings (ui/model/driver_cleanup_model.hpp).
#undef NDEBUG
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "testutil.hpp"
#include "ui/model/driver_cleanup_model.hpp"

using namespace spn::ui;

static void put(const std::string &p, const std::string &c) { std::ofstream(p, std::ios::binary) << c; }
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

static std::string axis(const char *enabled, const char *in, const char *out) {
    return std::string("        <Axis>\n          <Enabled>") + enabled + "</Enabled>\n          <Input>\n            <ActionID>" + in +
           "</ActionID>\n            <Min>-512</Min>\n          </Input>\n          <Output>\n            <ActionID>" + out +
           "</ActionID>\n          </Output>\n        </Axis>\n";
}

static std::string file(const char *name, const char *exe, const std::string &axes) {
    return std::string("\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!--\n(c) comment <Enabled>true</Enabled>\n-->\n"
                       "<AppCfg Default=\"false\">\n  <AppInfo>\n    <Signature>\n      <Name>") +
           name + "</Name>\n" + (*exe ? std::string("      <ExecutableName>") + exe + "</ExecutableName>\n" : "") +
           "    </Signature>\n  </AppInfo>\n  <Devices>\n    <Device>\n      <ID>X</ID>\n      <ButtonBank><Button><Enabled>true</Enabled></Button></ButtonBank>\n"
           "      <AxisBank Default=\"true\">\n" +
           axes + "      </AxisBank>\n    </Device>\n  </Devices>\n</AppCfg>\n";
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-driver"), dir = root + "/Cfg";
    std::filesystem::create_directories(dir);
    const std::string wheel = file("Code", "Code.exe", axis("true", "HIDMultiAxis_X", "HIDMouse_Wheel") + axis("false", "HIDMultiAxis_Y", "HIDMouse_Wheel") +
                                                           axis("true", "HIDMultiAxis_Rx", "HIDMouse_Wheel"));
    const std::string keys = file("STR_GAME", "game.exe", axis("true", "HIDMultiAxis_Z", "KB_Keystroke"));
    const std::string nav = file("Fusion", "", axis("true", "HIDMultiAxis_X", "HIDMultiAxis_X") + axis("true", "HIDMultiAxis_Ry", "HIDMultiAxis_Rz"));
    const std::string idle = file("Chrome", "chrome.exe", axis("false", "HIDMultiAxis_X", "HIDMouse_Wheel"));
    put(dir + "/Code.xml", wheel);
    put(dir + "/game.xml", keys);
    put(dir + "/Fusion.xml", nav);
    put(dir + "/Chrome.xml", idle);
    put(dir + "/Global.xml", wheel);        // not a program
    put(dir + "/notes.txt", wheel);         // not an XML file

    // scan: only programs with active axes; navigation programs are listed but not picked
    std::vector<DriverApp> apps;
    std::string err;
    assert(scanDriverConfigs(dir, &apps, &err));
    assert(apps.size() == 3);
    assert(apps[0].file == "Code.xml" && apps[0].name == "Code" && apps[0].exe == "Code.exe");
    assert(apps[0].active.size() == 2 && apps[0].active[0].input == "X" && apps[0].active[0].output == DriverOutput::MouseWheel);
    assert(apps[0].selected && !apps[0].navigation);
    assert(apps[1].file == "Fusion.xml" && apps[1].navigation && !apps[1].selected);
    assert(apps[2].file == "game.xml" && apps[2].name == "game" && apps[2].active[0].output == DriverOutput::Keystroke);  // STR_ key -> file name
    assert(!scanDriverConfigs(root + "/missing", &apps, &err) && !err.empty());
    assert(scanDriverConfigs(dir, &apps, &err));

    // the text edit touches the axis flags only (BOM, comment, button flag stay)
    int n = 0;
    const std::string cleaned = disableDriverAxes(wheel, &n);
    assert(n == 2);
    assert(cleaned.rfind("\xEF\xBB\xBF", 0) == 0 && cleaned.find("(c) comment <Enabled>true</Enabled>") != std::string::npos);
    assert(cleaned.find("<ButtonBank><Button><Enabled>true</Enabled>") != std::string::npos);
    DriverApp after;
    assert(parseDriverFile("Code.xml", cleaned, &after) && after.active.empty());
    assert(disableDriverAxes(cleaned, &n) == cleaned && n == 0);

    // apply: only the picked programs, with a backup of each original
    apps[1].selected = false;  // keep the navigation program
    DriverCleanupReport rep;
    assert(applyDriverCleanup(dir, apps, "20261023-101500", &rep));
    assert(rep.changed.size() == 2 && rep.failed.empty());
    assert(get(dir + "/Code.xml") == cleaned);
    assert(get(dir + "/Fusion.xml") == nav);  // not picked: untouched
    assert(get(dir + "/Global.xml") == wheel);
    assert(get(dir + "/dofscroll-backup/20261023-101500/Code.xml") == wheel);
    assert(get(dir + "/dofscroll-backup/20261023-101500/game.xml") == keys);
    assert(listDriverBackups(dir).size() == 1);
    assert(scanDriverConfigs(dir, &apps, &err) && apps.size() == 1 && apps[0].file == "Fusion.xml");  // the backup folder is not scanned

    // restore puts the originals back
    assert(restoreDriverBackup(dir, "20261023-101500", &rep) && rep.changed.size() == 2);
    assert(get(dir + "/Code.xml") == wheel && get(dir + "/game.xml") == keys);

    // a stamp is a folder name: nothing else gets through
    assert(!applyDriverCleanup(dir, apps, "../x", &rep) && !restoreDriverBackup(dir, "a/b", &rep));

    // a second cleanup within the same second gets a folder of its own: the backup of the originals is not overwritten
    assert(scanDriverConfigs(dir, &apps, &err) && apps.size() == 3);
    for (DriverApp &a : apps) a.selected = a.file != "Fusion.xml";
    assert(applyDriverCleanup(dir, apps, "20261023-101500", &rep) && rep.changed.size() == 2 &&
           rep.backupDir == dir + "/dofscroll-backup/20261023-101500-2");  // the first run of this stamp is above
    assert(get(dir + "/dofscroll-backup/20261023-101500/Code.xml") == wheel);
    assert(restoreDriverBackup(dir, "20261023-101500", &rep));
    assert(scanDriverConfigs(dir, &apps, &err));
    for (DriverApp &a : apps) a.selected = a.file != "Fusion.xml";
    assert(applyDriverCleanup(dir, apps, "20261023-101500", &rep) && rep.backupDir == dir + "/dofscroll-backup/20261023-101500-3");
    assert(get(dir + "/dofscroll-backup/20261023-101500/Code.xml") == wheel &&
           get(dir + "/dofscroll-backup/20261023-101500-3/Code.xml") == wheel);
    assert(listDriverBackups(dir).size() == 3);
    assert(restoreDriverBackup(dir, "20261023-101500", &rep) && get(dir + "/Code.xml") == wheel);

    // restore does not write through a link at the destination
    {
        std::error_code ec;
        std::filesystem::create_directories(root + "/outside");
        std::ofstream(root + "/outside/target.xml") << "untouched";
        std::filesystem::remove(dir + "/Code.xml");
        std::filesystem::create_symlink(root + "/outside/target.xml", dir + "/Code.xml", ec);
        if (!ec) {
            assert(!restoreDriverBackup(dir, "20261023-101500", &rep));
            assert(get(root + "/outside/target.xml") == "untouched" && !rep.failed.empty());
        }
    }

    // a folder with more entries than are looked at is reported as partial
    {
        const std::string big = root + "/big";
        std::filesystem::create_directories(big);
        for (int i = 0; i < int(kMaxDriverFiles) + 8; ++i) std::ofstream(big + "/f" + std::to_string(i) + ".txt") << "x";
        bool truncated = false;
        std::vector<DriverApp> some;
        assert(scanDriverConfigs(big, &some, &err, &truncated) && truncated && some.empty());
        assert(scanDriverConfigs(dir, &some, &err, &truncated) && !truncated);
    }

    tu::removeAll(root);
    return 0;
}
