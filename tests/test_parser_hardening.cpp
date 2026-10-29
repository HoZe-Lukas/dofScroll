// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Hardening of the core and the parsers (issue #272, findings L1-L10): control characters in a
// key or value, dropped section names, applyKey without a copy of the whole Settings, clamped axis
// values, permissions of the configuration file and its directory, UTF-8 safe helpers, spoofing
// characters in log and display text. Host independent: temp directories only.
#undef NDEBUG
#include <cassert>
#include <climits>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "config.hpp"
#include "daemon_core.hpp"
#include "log_sanitize.hpp"
#include "platform/fake_input.hpp"
#include "platform/fs.hpp"
#include "platform/running_apps_util.hpp"
#include "testutil.hpp"
#include "utf8.hpp"

using namespace spn;

static bool ph272Has(const std::string &s, const std::string &n) { return s.find(n) != std::string::npos; }
static bool ph272AnyWarn(const std::vector<std::string> &w, const std::string &n) {
    for (const std::string &x : w)
        if (ph272Has(x, n)) return true;
    return false;
}

// L1: a control character in a key or value is refused at parse time, so every loaded file can be saved again.
static void ph272ControlCharacters(const std::string &root) {
    const std::string text =
        "[global]\nSCROLL_H_MIX = x -\rz\nZOOM_SENS = 7\nSCROLL_V_SENS = 4\x01\nTICK_HZ\t=\t100\n"
        "[app:firefox]\nZOOM_MODIFIER = ctrl\x7f\n";
    ProfileSet ps;
    std::vector<std::string> w;
    assert(parseProfilesText(text, "t.conf", &ps, &w));
    assert(ph272AnyWarn(w, "control character"));
    assert(w.size() == 3);  // the CR value, the SOH value and the DEL value
    for (const KV &p : ps.sections[0].kv) assert(p.first != "SCROLL_H_MIX" && p.first != "SCROLL_V_SENS");
    assert(ps.resolve("", "").zoom.sensitivity == 7 && ps.resolve("", "").tick_hz == 100);  // tab stays valid
    std::string err;
    assert(saveProfiles(root + "/ctl.conf", ps, &err));  // what was loaded can be saved
    // validPair and the parser agree: a hand-built set with the same value is refused on save.
    ProfileSet bad;
    bad.sections[0].kv.emplace_back("SCROLL_H_MIX", "x -\rz");
    assert(!saveProfiles(root + "/bad.conf", bad, &err) && ph272Has(err, "line break"));
    ProfileSet bad2;
    bad2.sections[0].kv.emplace_back("ZOOM_SENS", "5\x01");
    assert(!saveProfiles(root + "/bad2.conf", bad2, &err));
    // a control character in the key
    ProfileSet ps2;
    std::vector<std::string> w2;
    parseProfilesText("[global]\nZOOM\x02_SENS = 3\n", "t", &ps2, &w2);
    assert(!w2.empty() && ps2.sections[0].kv.empty());
}

// L2: a section name that cannot be kept is announced, and its keys are not merged into the section above.
static void ph272SectionNames() {
    ProfileSet ps;
    std::vector<std::string> w;
    const std::string longName(5000, 'a');
    parseProfilesText("[global]\nZOOM_SENS = 3\n[app:" + longName + "]\nZOOM_SENS = 9\n[device:a\x01" "b]\nZOOM_SENS = 8\n",
                      "t", &ps, &w);
    assert(ps.sections.size() == 1 && ps.sections[0].kv.size() == 1);
    assert(ph272AnyWarn(w, "section name") && w.size() >= 2);
    assert(ps.resolve("", "").zoom.sensitivity == 3);
}

// L3: applyKey changes nothing when it fails (it no longer works on a copy of the whole Settings).
static void ph272ApplyKeyAtomic() {
    Settings s;
    s.buttons[3] = "click";
    const Settings before = s;
    struct Bad { const char *k, *v; };
    const Bad bads[] = {{"SCROLL_V_MIX", "z + bogus"},   {"SCROLL_V_MIX", "x y z 1"},    {"SCROLL_V_CURVE", "1:2 0:3"},
                        {"SCROLL_V_CURVE", "2:3 x"},     {"SCROLL_V_SENS", "abc"},        {"ZOOM_LATERAL", "x bogus"},
                        {"ZOOM_MODIFIER", "ctrl+bogus"}, {"INVERT_AXES", "x nope"},       {"BUTTON_3", "key:nokey"},
                        {"BUTTON_99", "click"},          {"V_ACTION", "step:bogus,none"}, {"SCROLL_V_TILT_SENS", "x"},
                        {"RESPONSE_CURVE", "abc"},       {"TICK_HZ", "fast"},             {"NOSUCHKEY", "1"}};
    for (const Bad &b : bads) {
        std::string err;
        assert(!applyKey(s, b.k, b.v, &err));
        assert(!err.empty() && sameSettings(s, before));
    }
    std::string err;
    assert(!applyKey(s, "SCROLL_V_MIX", std::string("z\0y", 3), &err) && sameSettings(s, before));
    assert(applyKey(s, "ZOOM_SENS", "9", &err) && s.zoom.sensitivity == 9 && s.buttons.at(3) == "click");
    // many profiles with many buttons still resolve once per window change without trouble
    ProfileSet ps;
    std::string text = "[global]\n";
    for (int i = 1; i <= 32; ++i) text += "BUTTON_" + std::to_string(i) + " = click\n";
    for (int i = 0; i < 200; ++i) text += "[app:p" + std::to_string(i) + "]\nZOOM_SENS = " + std::to_string(1 + i % 9) + "\n";
    parseProfilesText(text, "t", &ps, nullptr);
    for (int i = 0; i < 200; ++i) assert(ps.resolve("", "p" + std::to_string(i)).buttons.size() == 32);
}

// L4: axis values are clamped at the reader, so INVERT_AXES and the mix products cannot overflow.
static void ph272AxisClamp() {
    FakeInputSource src;
    int64_t now = 1;
    DeviceReader rd(&src, [&now] { return now++; }, false);
    InputEvent e;
    e.axes = {INT_MIN, INT_MAX, -40000, 40000, -32768, 5};
    rd.handle(e);
    assert(rd.axes[0].load() == -32767 && rd.axes[1].load() == 32767 && rd.axes[2].load() == -32767);
    assert(rd.axes[3].load() == 32767 && rd.axes[4].load() == -32767 && rd.axes[5].load() == 5);
    const DeviceReader::Slot *sl = rd.slotFor(0);
    assert(sl && sl->axes[0].load() == -32767 && sl->axes[1].load() == 32767);
}

#ifndef _WIN32
static unsigned ph272Mode(const std::string &p) {
    struct stat st;
    assert(::stat(p.c_str(), &st) == 0);
    return static_cast<unsigned>(st.st_mode & 0777);
}

// L5: loose permissions of the configuration file are announced; the written file loses group/other write.
static void ph272Permissions(const std::string &root) {
    const std::string dir = root + "/perm";
    const std::string conf = dir + "/dofscroll.conf";
    std::string err;
    assert(fs::writeFileReplace(conf, "[global]\nZOOM_SENS = 4\n", &err));
    assert(ph272Mode(conf) == 0600);
    ProfileSet ps;
    std::vector<std::string> w;
    assert(loadProfiles(conf, &ps, &w) && w.empty());

    assert(::chmod(conf.c_str(), 0666) == 0);
    w.clear();
    assert(loadProfiles(conf, &ps, &w) && ps.resolve("", "").zoom.sensitivity == 4);  // loaded, but noted
    assert(ph272AnyWarn(w, "writable by others"));
    assert(fs::writeFileReplace(conf, "[global]\nZOOM_SENS = 5\n", &err));
    assert(ph272Mode(conf) == 0644);  // a loosened file keeps read access, loses group/other write
    assert(::chmod(conf.c_str(), 0664) == 0);
    assert(fs::writeFileReplace(conf, "[global]\nZOOM_SENS = 6\n", &err) && ph272Mode(conf) == 0644);
    assert(::chmod(conf.c_str(), 0640) == 0);
    assert(fs::writeFileReplace(conf, "[global]\nZOOM_SENS = 7\n", &err) && ph272Mode(conf) == 0640);  // tighter stays

    // a world-writable directory without the sticky bit is not used for the configuration
    const std::string open = root + "/openDir";
    assert(::mkdir(open.c_str(), 0700) == 0 && ::chmod(open.c_str(), 0777) == 0);
    assert(!fs::writeFileReplace(open + "/dofscroll.conf", "x\n", &err) && ph272Has(err, "writable"));
    assert(!fs::mkdirsPrivate(open, &err));
    assert(::chmod(open.c_str(), 01777) == 0);  // sticky, like /tmp: fine
    assert(fs::mkdirsPrivate(open, &err));
    assert(::chmod(open.c_str(), 0700) == 0);
    assert(fs::mkdirsPrivate(open + "/sub/deeper", &err));

    if (::geteuid() == 0) {  // only root can hand a file or directory to another user
        const std::string foreign = root + "/foreign.conf";
        {
            std::ofstream f(foreign, std::ios::binary);
            f << "[global]\nZOOM_SENS = 3\n";
        }
        assert(::chown(foreign.c_str(), 12345, 12345) == 0);
        w.clear();
        assert(loadProfiles(foreign, &ps, &w) && ph272AnyWarn(w, "owned by another user"));
        const std::string fdir = root + "/foreignDir";
        assert(::mkdir(fdir.c_str(), 0755) == 0 && ::chown(fdir.c_str(), 12345, 12345) == 0);
        assert(!fs::mkdirsPrivate(fdir, &err) && ph272Has(err, "owned"));
    }
}
#endif

// L6: UTF-8 safe cutting and no spoofing characters in log or display text.
static void ph272Utf8() {
    std::string two;
    for (int i = 0; i < 200; ++i) two += "\xC3\x89";  // U+00C9, 400 bytes
    const std::string j = joinWmClass(two.c_str(), "");
    assert(j.size() <= kMaxWmClassLen && utf8::isValid(j) && j.size() == 256);
    const std::string euro = std::string(255, 'a') + "\xE2\x82\xAC";  // the euro sign straddles byte 256
    const std::string j3 = joinWmClass(euro.c_str(), "");
    assert(utf8::isValid(j3) && j3.size() == 255);
    assert(!isOwnAppString("x\xC2\xA0y"));  // bytes >= 0x80 are never white space

    const std::string rlo = "evil\xE2\x80\xAEgnp.exe";          // U+202E
    const std::string iso = "a\xE2\x81\xA6" "b\xE2\x81\xA9";     // U+2066, U+2069
    const std::string zw = "z\xE2\x80\x8B" "w\xEF\xBB\xBF" "v";  // U+200B, U+FEFF
    assert(sanitizeForLog(rlo) == "evilgnp.exe");
    assert(sanitizeForLog(iso) == "ab");
    assert(sanitizeForLog(zw) == "zwv");
    assert(utf8::sanitizeDisplay(rlo, 64) == "evilgnp.exe");
    assert(utf8::sanitizeDisplay(iso, 64) == "ab" && utf8::sanitizeDisplay(zw, 64) == "zwv");
    assert(sanitizeForLog("caf\xC3\xA9 \xE2\x82\xAC") == "caf\xC3\xA9 \xE2\x82\xAC");  // ordinary text is kept
    assert(utf8::sanitizeDisplay("caf\xC3\xA9", 64) == "caf\xC3\xA9");
}

// L8: English messages.
static void ph272Messages() {
    Settings s;
    std::string err;
    assert(!applyKey(s, "ZOOM_CURVE", "-1:2", &err) && ph272Has(err, "point") && !ph272Has(err, "Punkt"));
    assert(!applyKey(s, "ZOOM_CURVE", "1:500", &err) && ph272Has(err, "point") && !ph272Has(err, "Punkt"));
}

int main() {
    const std::string root = tu::makeTempDir("dofscroll-ph272");
    ph272ControlCharacters(root);
    ph272SectionNames();
    ph272ApplyKeyAtomic();
    ph272AxisClamp();
#ifndef _WIN32
    ph272Permissions(root);
#endif
    ph272Utf8();
    ph272Messages();
    tu::removeAll(root);
    std::puts("test_parser_hardening: ok");
    return 0;
}
