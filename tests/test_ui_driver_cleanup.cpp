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

    // ---- the wizard's rules: many programs, ticks independent of any row selection ----
    {
        const std::string many = root + "/many";
        std::filesystem::create_directories(many);
        // 40 programs: every 5th is a 3D/CAD one (passes the axes on), every 4th has a mouse wheel and keys
        for (int i = 0; i < 40; ++i) {
            const std::string pn = "Prog" + std::to_string(100 + i);
            std::string axes;
            if (i % 5 == 0) axes = axis("true", "HIDMultiAxis_X", "HIDMultiAxis_X") + axis("true", "HIDMultiAxis_Ry", "HIDMultiAxis_Rz");
            else if (i % 4 == 0) axes = axis("true", "HIDMultiAxis_X", "HIDMouse_Wheel") + axis("true", "HIDMultiAxis_Z", "KB_Keystroke");
            else axes = axis("true", "HIDMultiAxis_Ry", "HIDMouse_Wheel") + axis("true", "HIDMultiAxis_Rx", "HIDMouse_Wheel");
            put(many + "/" + pn + ".xml", file(pn.c_str(), (pn + ".exe").c_str(), axes));
        }
        std::vector<DriverApp> list;
        DriverScanReport rp;
        assert(scanDriverFolder(many, &list, &rp));
        assert(list.size() == 40 && rp.programs == 40 && rp.xmlFiles == 40 && rp.skipped.empty() && rp.folderFound && rp.error.empty());
        assert(driverNavigationCount(list) == 8);
        assert(driverTickedCount(list) == 32);  // the scan's default: everything except 3D and CAD

        // tick all / none / except 3D and CAD / invert, on all rows and on a subset (the rows a filter shows)
        applyDriverTick(list, DriverTick::All, {});
        assert(driverTickedCount(list) == 40);
        applyDriverTick(list, DriverTick::None, {});
        assert(driverTickedCount(list) == 0);
        applyDriverTick(list, DriverTick::ExceptNavigation, {});
        assert(driverTickedCount(list) == 32);
        for (const DriverApp &a : list) assert(a.selected == !a.navigation);
        applyDriverTick(list, DriverTick::Invert, {});
        assert(driverTickedCount(list) == 8);
        for (const DriverApp &a : list) assert(a.selected == a.navigation);
        applyDriverTick(list, DriverTick::None, {});
        applyDriverTick(list, DriverTick::All, {1, 3, 5});  // three rows, not just "the highlighted one"
        assert(driverTickedCount(list) == 3 && list[1].selected && list[3].selected && list[5].selected && !list[0].selected);
        applyDriverTick(list, DriverTick::All, {999});  // out of range: ignored
        assert(driverTickedCount(list) == 3);

        // Space on several selected rows: one group action
        assert(toggleDriverTicks(list, {0, 1, 2}));  // 0 and 2 are off: all three get ticked
        assert(list[0].selected && list[1].selected && list[2].selected);
        assert(!toggleDriverTicks(list, {0, 1, 2}));  // all on: all off
        assert(!list[0].selected && !list[1].selected && !list[2].selected);
        assert(!toggleDriverTicks(list, {}) && !toggleDriverTicks(list, {500}));

        // the filter: name, executable or file, ASCII case folded
        assert(driverFilterRows(list, "").size() == 40 && driverFilterRows(list, "  ").size() == 40);
        assert(driverFilterRows(list, "prog105").size() == 1 && driverFilterRows(list, "PROG10").size() == 10);
        assert(driverFilterRows(list, "prog139.EXE").size() == 1 && driverFilterRows(list, "nothing").empty());
        applyDriverTick(list, DriverTick::None, {});
        applyDriverTick(list, DriverTick::All, driverFilterRows(list, "prog12"));
        assert(driverTickedCount(list) == 10);

        // preview: only the ticked programs, which axes go off; nothing is written
        applyDriverTick(list, DriverTick::None, {});
        list[0].selected = list[1].selected = list[4].selected = true;  // Prog100 (3D), Prog101 (wheel), Prog104 (wheel + keys)
        const DriverPreview pv = buildDriverPreview(list);
        assert(pv.files == 3 && pv.items.size() == 3 && pv.navigationTicked == 1 && pv.axes == 6);
        assert(pv.items[0].file == "Prog100.xml" && pv.items[0].navigation && pv.items[0].axisCount == 2);
        assert(pv.items[1].file == "Prog101.xml" && !pv.items[1].navigation && pv.items[1].groups.size() == 1 &&
               pv.items[1].groups[0].output == DriverOutput::MouseWheel && pv.items[1].groups[0].axes.size() == 2 &&
               pv.items[1].groups[0].axes[0] == "Ry" && pv.items[1].groups[0].axes[1] == "Rx");
        assert(pv.items[2].groups.size() == 2 && pv.items[2].groups[0].output == DriverOutput::MouseWheel &&
               pv.items[2].groups[1].output == DriverOutput::Keystroke && pv.items[2].groups[1].axes[0] == "Z");
        assert(get(many + "/Prog101.xml").find("<Enabled>true</Enabled>") != std::string::npos);
        assert(buildDriverPreview(std::vector<DriverApp>()).files == 0);

        // apply many at once with progress; a file that vanished and a link fail by name, the others are changed
        applyDriverTick(list, DriverTick::All, {});
        std::filesystem::remove(many + "/Prog110.xml");  // gone between scan and apply
        std::filesystem::remove(many + "/Prog111.xml");
        std::error_code lec;
        std::filesystem::create_symlink(root + "/outside/target.xml", many + "/Prog111.xml", lec);  // refused: not read through
        size_t calls = 0, lastDone = 0, lastTotal = 0;
        DriverCleanupReport rr;
        const bool all = applyDriverCleanup(many, list, "20261101-090000", &rr, [&](size_t done, size_t total, const std::string &) {
            ++calls;
            lastDone = done;
            lastTotal = total;
        });
        assert(!all && lastDone == 40 && lastTotal == 40 && calls == 41);
        assert(rr.failed.size() == 2 && rr.changed.size() == 38);
        bool named = false;
        for (const std::string &f : rr.failed) named = named || f.rfind("Prog110.xml: ", 0) == 0;
        assert(named);
        assert(rr.backupDir == many + "/dofscroll-backup/20261101-090000");
        assert(get(many + "/dofscroll-backup/20261101-090000/Prog101.xml").find("<Enabled>true</Enabled>") != std::string::npos);
        std::vector<DriverApp> left;
        assert(scanDriverFolder(many, &left, &rp) && left.empty());  // the 38 others are cleaned

        // the backups are listed with their date and number of files; a chosen older one can be restored
        assert(driverBackupWhen("20261101-090000") == "2026-11-01 09:00:00");
        assert(driverBackupWhen("20261101-090000-2") == "2026-11-01 09:00:00 (2)");
        assert(driverBackupWhen("odd-name") == "odd-name");
        std::filesystem::remove(many + "/Prog111.xml");
        DriverCleanupReport r1;
        assert(restoreDriverBackup(many, "20261101-090000", &r1) && r1.changed.size() == 38);
        std::vector<DriverApp> again;
        assert(scanDriverFolder(many, &again, &rp) && again.size() == 38);
        applyDriverTick(again, DriverTick::All, {});
        DriverCleanupReport r2;
        assert(applyDriverCleanup(many, again, "20261102-100000", &r2) && r2.changed.size() == 38);
        const std::vector<DriverBackupInfo> infos = listDriverBackupInfo(many);
        assert(infos.size() == 2 && infos[0].stamp == "20261102-100000" && infos[1].stamp == "20261101-090000");
        assert(infos[1].files == 38 && infos[1].when == "2026-11-01 09:00:00" && infos[0].files == 38);
        assert(listDriverBackupInfo(root + "/absent").empty());
        DriverCleanupReport r3;
        assert(restoreDriverBackup(many, infos[1].stamp, &r3) && r3.changed.size() == 38);  // the older one
        assert(scanDriverFolder(many, &left, &rp) && left.size() == 38);
    }

    // ---- what the scan reports: files it cannot use are named, not silently dropped ----
    {
        const std::string odd = root + "/odd";
        std::filesystem::create_directories(odd);
        put(odd + "/Good.xml", file("Good", "good.exe", axis("true", "HIDMultiAxis_X", "HIDMouse_Wheel")));
        put(odd + "/Off.xml", file("Off", "off.exe", axis("false", "HIDMultiAxis_X", "HIDMouse_Wheel")));
        put(odd + "/NoBank.xml", "<AppCfg><AppInfo/></AppCfg>");
        put(odd + "/Global.xml", file("Global", "", axis("true", "HIDMultiAxis_X", "HIDMouse_Wheel")));
        put(odd + "/UserInfo.xml", "<UserInfo/>");
        put(odd + "/Wide.xml", std::string("\xFF\xFE<\0?\0x\0m\0l\0", 12));  // UTF-16 with a byte order mark
        put(odd + "/Huge.xml", std::string(kMaxDriverFileBytes + 10, 'x'));
        std::vector<DriverApp> a;
        DriverScanReport rp;
        assert(scanDriverFolder(odd, &a, &rp));
        assert(a.size() == 1 && rp.programs == 1 && rp.withoutActiveAxes == 1 && rp.withoutAxisBank == 1 && rp.hasGlobalFile);
        assert(rp.xmlFiles == 5);  // Good, Off, NoBank, Wide, Huge (Global.xml and UserInfo.xml are not programs)
        assert(rp.skipped.size() == 2);
        bool wide = false, huge = false;
        for (const DriverSkippedFile &s : rp.skipped) {
            wide = wide || (s.file == "Wide.xml" && s.reason == DriverSkipReason::Utf16);
            huge = huge || (s.file == "Huge.xml" && s.reason == DriverSkipReason::TooLarge);
        }
        assert(wide && huge);
        assert(!scanDriverFolder(root + "/absent", &a, &rp) && !rp.folderFound && !rp.error.empty() && a.empty());
        assert(looksLikeUtf16(std::string("<\0?\0", 4)) && !looksLikeUtf16("<?xml") && !looksLikeUtf16(""));
        // a program file that cannot be read (here: a link, which is refused) is named too
        std::error_code ec;
        std::filesystem::create_symlink(root + "/outside/target.xml", odd + "/Locked.xml", ec);
        if (!ec) {
            assert(scanDriverFolder(odd, &a, &rp));
            bool locked = false;
            for (const DriverSkippedFile &s : rp.skipped) locked = locked || (s.file == "Locked.xml" && s.reason == DriverSkipReason::Unreadable);
            assert(locked);
        }
    }

    tu::removeAll(root);
    return 0;
}
