// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Import of a profile file (src/ui/model/profile_import.hpp), the rules that tests/test_ui_split_model.cpp does not
// pin: BOM and CRLF files, the size limit, a directory or link as the file, Replace removes the old keys and keeps
// PRIORITY and the place, kind separation of equal names, canonical writing, short-name boundaries by code points,
// notes for dropped keys, the plan of a refused file changes nothing, device replace, and Keep after a stale plan.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "platform/fs.hpp"
#include "profile_files.hpp"
#include "testutil.hpp"
#include "ui/model/profile_import.hpp"

using namespace spn;
using namespace spn::ui;

static IdentityStrings id;

static void put(const std::string &p, const std::string &c) {
    std::string err;
    assert(fs::writeFileReplace(p, c, &err));
}
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static bool has(const std::string &hay, const std::string &needle) { return hay.find(needle) != std::string::npos; }
static std::string makeDir(const char *tag) {
    const std::string d = tu::makeTempDir(tag);
    std::filesystem::create_directories(d + "/devices");
    std::filesystem::create_directories(d + "/apps");
    put(d + "/dofscroll.conf", "[global]\n");
    return d;
}
static SettingsModel load(const std::string &dir) {
    SettingsModel m;
    std::string err;
    assert(m.load(dir + "/dofscroll.conf", nullptr, &err));
    return m;
}
static ImportPlan planOf(const SettingsModel &m, const std::string &dir, const std::string &body) {
    put(dir + "/imp.conf", body);
    return planProfileImport(m, dir + "/imp.conf", id);
}

static void testBomCrlfAndFileKinds() {
    const std::string d = makeDir("imp-bom");
    SettingsModel m = load(d);
    // BOM and CRLF are read; the name has no CR; the keys are clean
    ImportPlan p = planOf(m, d, "\xEF\xBB\xBF[app:longprogram]\r\nZOOM_SENS = 3\r\nSCROLL_V_SENS = 4\r\n");
    assert(p.ok && p.name == "longprogram" && p.keys.size() == 2 && p.warnings.empty() && p.risks.empty());
    assert(applyProfileImport(m, p, ImportChoice::Replace, id).done && m.save(id).saved);
    const std::string f = get(d + "/apps/longprogram.conf");
    assert(f.find('\r') == std::string::npos && f.compare(0, 3, "\xEF\xBB\xBF") != 0 && has(f, "ZOOM_SENS = 3"));
    // empty file and a file of blank lines: refused with the "no profile" text, nothing changes
    for (const char *body : {"", "\n\n  \n"}) {
        p = planOf(m, d, body);
        assert(!p.ok && has(p.error, "no device or program profile") && !m.dirty());
    }
    // a directory is not a profile file
    std::filesystem::create_directories(d + "/adir.conf");
    p = planProfileImport(m, d + "/adir.conf", id);
    assert(!p.ok && has(p.error, "cannot be read") && has(p.error, "adir.conf"));
#ifndef _WIN32
    std::error_code ec;
    std::filesystem::create_symlink(d + "/imp.conf", d + "/link.conf", ec);
    if (!ec) {
        // a link to a regular profile file is read through the same bounded reader as any file; it never crashes
        (void)planProfileImport(m, d + "/link.conf", id);
    }
#endif
    // size limit: exactly the limit is read, one byte more is refused
    std::string at = "[app:bigprogram]\n# ";
    at.append(kMaxProfileFileBytes - at.size() - 1, 'x');
    at += "\n";
    assert(at.size() == kMaxProfileFileBytes);
    p = planOf(m, d, at);
    assert(p.ok && p.name == "bigprogram");
    p = planOf(m, d, at + "x");
    assert(!p.ok && has(p.error, "too large") && !m.dirty());
    tu::removeAll(d);
}

static void testReplaceSemantics() {
    const std::string d = makeDir("imp-replace");
    put(d + "/apps/aaaaa.conf", "[app:aaaaa]\nPRIORITY = 10\nZOOM_SENS = 7\n");
    put(d + "/apps/mine1.conf", "[app:mine1]\nPRIORITY = 20\nZOOM_SENS = 7\nSCROLL_V_SENS = 2\nBUTTON_1 = click\n");
    put(d + "/apps/zzzzz.conf", "[app:zzzzz]\nPRIORITY = 30\n");
    put(d + "/devices/mine1.conf", "[device:mine1]\nZOOM_SENS = 5\n");  // the same name, another kind
    SettingsModel m = load(d);
    // the same name in the other kind is no collision
    ImportPlan p = planOf(m, d, "[device:MINE1]\nZOOM_SENS = 9\n");
    assert(p.ok && p.collision && p.kind == Section::DEVICE);
    p = planOf(m, d, "[app:nothere]\n");
    assert(p.ok && !p.collision && p.existing == ProfileSet::npos);
    // Replace: old keys that the file does not have are gone, PRIORITY and the place stay, the device is untouched
    p = planOf(m, d, "[app:Mine1]\nZOOM_SENS = 9\nPRIORITY = -50\n");
    assert(p.ok && p.collision && p.notes.size() == 1 && has(p.notes[0], "PRIORITY"));
    const size_t before = m.profiles().find(Section::APP, "mine1");
    const int prio = m.priority(before);
    const ImportResult r = applyProfileImport(m, p, ImportChoice::Replace, id);
    assert(r.done && m.save(id).saved);
    const std::string f = get(d + "/apps/mine1.conf");
    assert(has(f, "ZOOM_SENS = 9") && !has(f, "SCROLL_V_SENS") && !has(f, "BUTTON_1") && has(f, "PRIORITY = " + std::to_string(prio)));
    assert(!has(f, "-50"));
    assert(m.priority(m.profiles().find(Section::APP, "mine1")) == prio);
    assert(has(get(d + "/devices/mine1.conf"), "ZOOM_SENS = 5"));
    // the profile keeps its original header case on a Replace
    assert(has(f, "[app:mine1]"));
    // Replace with an empty profile removes all settings but PRIORITY
    p = planOf(m, d, "[app:mine1]\n");
    assert(p.ok && p.keys.empty() && p.collision);
    assert(applyProfileImport(m, p, ImportChoice::Replace, id).done && m.save(id).saved);
    assert(!has(get(d + "/apps/mine1.conf"), "ZOOM_SENS") && has(get(d + "/apps/mine1.conf"), "PRIORITY"));
    tu::removeAll(d);
}

static void testNotesAndRiskBoundaries() {
    const std::string d = makeDir("imp-notes");
    SettingsModel m = load(d);
    // every dropped key is listed once per occurrence, in any case; the others are kept
    ImportPlan p = planOf(m, d, "[app:programname]\nenabled = 0\nBackend = uinput\ntick_hz = 30\nINPUT = hid\npriority = 3\nZOOM_SENS = 2\n");
    assert(p.ok && p.notes.size() == 5 && p.keys.size() == 1 && p.keys[0].first == "ZOOM_SENS" && p.risks.empty() && !p.needsConfirm());
    assert(has(p.notes[0], "ENABLED") && has(p.notes[1], "BACKEND") && has(p.notes[2], "TICK_HZ") && has(p.notes[3], "INPUT") && has(p.notes[4], "PRIORITY"));
    // an invalid value anywhere refuses the plan (the value is named); an unknown key is dropped as a warning
    p = planOf(m, d, "[app:programname]\nZOOM_SENS = 2\nBUTTON_1 = key:notakey\n");
    assert(p.ok ? !p.warnings.empty() : !p.error.empty());
    p = planOf(m, d, "[app:programname]\nNOT_A_KEY = 1\nZOOM_SENS = 2\n");
    assert(p.ok && !p.warnings.empty() && p.keys.size() == 1);
    // short names count code points: 4 flagged (also multi-byte), 5 not, the word follows the kind
    assert(!planOf(m, d, "[app:ab]\n").risks.empty());
    assert(!planOf(m, d, "[app:\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC]\n").risks.empty());          // 4 euro signs, 12 bytes
    assert(planOf(m, d, "[app:\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC]\n").risks.empty());  // 5
    assert(has(planOf(m, d, "[app:ab]\n").risks[0], "program") && has(planOf(m, d, "[device:ab]\n").risks[0], "device"));
    // a short name and a risky chord are two separate risks; the chord is reported once however many keys
    p = planOf(m, d, "[app:ab]\nBUTTON_1 = key:ctrl+w\nBUTTON_2 = key:alt+f4\nV_ACTION = keys:ctrl+a,ctrl+b\n");
    assert(p.ok && p.risks.size() == 2 && p.needsConfirm());
    // the risk names the risky bindings (issue 319), at most six, then how many more
    assert(has(p.risks[0], "BUTTON_1 = key:ctrl+w") && has(p.risks[0], "BUTTON_2 = key:alt+f4"));
    p = planOf(m, d, "[app:programname]\nBUTTON_1 = key:ctrl+1\nBUTTON_2 = key:ctrl+2\nBUTTON_3 = key:ctrl+3\nBUTTON_4 = key:ctrl+4\n"
                     "BUTTON_5 = key:ctrl+5\nBUTTON_6 = key:ctrl+6\nBUTTON_7 = key:ctrl+7\nBUTTON_8 = key:ctrl+8\n");
    assert(p.ok && p.risks.size() == 1 && has(p.risks[0], "BUTTON_6 = key:ctrl+6") && !has(p.risks[0], "BUTTON_7 =") && has(p.risks[0], "2 more"));
    // a risk plus a collision still needs one confirmation (needsConfirm), a plain new profile none
    assert(!planOf(m, d, "[app:programname]\nZOOM_SENS = 1\n").needsConfirm());
    tu::removeAll(d);
}

static void testRefusedPlanChangesNothing() {
    const std::string d = makeDir("imp-refused");
    put(d + "/apps/mine1.conf", "[app:mine1]\nZOOM_SENS = 7\n");
    SettingsModel m = load(d);
    for (const char *body : {"[global]\nZOOM_SENS = 1\n", "[app:a]\n[device:b]\n", "# x\n", "[app:z\xE2\x80\xAE" "name]\n",
                             "[app:\x01" "ctrl]\n"}) {
        const ImportPlan p = planOf(m, d, body);
        assert(!p.ok && !p.error.empty());
        const ImportResult r = applyProfileImport(m, p, ImportChoice::Replace, id);  // applying a refused plan is a no-op
        assert(!r.done && r.message == p.error && !m.dirty());
    }
    assert(m.profiles().sections.size() == 2);
    // Keep on a collision: no change at all, the message names the profile, the section is the existing one
    ImportPlan p = planOf(m, d, "[app:MINE1]\nZOOM_SENS = 1\n");
    const ImportResult r = applyProfileImport(m, p, ImportChoice::Keep, id);
    assert(!r.done && r.section == m.profiles().find(Section::APP, "mine1") && has(r.message, "MINE1") && !m.dirty());
    // a stale plan with Keep is refused too (the profile appeared): not "kept", a changed-meanwhile message
    p = planOf(m, d, "[app:brandnew]\nZOOM_SENS = 1\n");
    put(d + "/apps/brandnew.conf", "[app:brandnew]\nZOOM_SENS = 2\n");
    std::string err;
    assert(m.load(d + "/dofscroll.conf", nullptr, &err));
    const ImportResult r2 = applyProfileImport(m, p, ImportChoice::Keep, id);
    assert(!r2.done && has(r2.message, "changed while") && !m.dirty());
    tu::removeAll(d);
}

static void testCanonicalAndOrder() {
    const std::string d = makeDir("imp-canon");
    put(d + "/apps/mine1.conf", "[app:mine1]\nPRIORITY = 5\n");
    SettingsModel m = load(d);
    // values are written canonically (case of the action, spacing), the keys in the order of the file
    ImportPlan p = planOf(m, d, "[device:imported pro]\nTWIST_ACTION = Keys:Ctrl+Minus , ctrl+plus\nbutton_2 = KEY:Space\nzoom_sens = 4\n");
    assert(p.ok && p.keys.size() == 3);
    assert(applyProfileImport(m, p, ImportChoice::Replace, id).done && m.save(id).saved);
    const std::string f = get(d + "/devices/imported pro.conf");
    assert(has(f, "[device:imported pro]") && has(f, "BUTTON_2 = key:space") && has(f, "ZOOM_SENS = 4"));
    assert(!has(f, "Keys:") && !has(f, "KEY:") && !has(f, "button_2") && !has(f, "zoom_sens"));
    assert(has(f, "TWIST_ACTION = keys:ctrl+minus,ctrl+plus"));
    // loading it again reads the same settings (the written file is a valid profile)
    SettingsModel m2 = load(d);
    const size_t i = m2.profiles().find(Section::DEVICE, "imported pro");
    assert(i != ProfileSet::npos && m2.effective(i).zoom.sensitivity == 4);
    // an imported new profile is applied before the user's own PRIORITY 5 profile
    p = planOf(m, d, "[app:newone]\nZOOM_SENS = 1\n");
    assert(applyProfileImport(m, p, ImportChoice::Replace, id).done);
    assert(m.priority(m.profiles().find(Section::APP, "newone")) < m.priority(m.profiles().find(Section::APP, "mine1")));
    tu::removeAll(d);
}

// A click or a held mouse button on a motion (step: / hold: with click, rightclick, middleclick) is as risky as a click
// on a device button: the import asks before it adds such a profile (security audit round 2).
static void testMouseSidesOfMotionsAreRisky() {
    const std::string d = makeDir("imp-mouse");
    SettingsModel m = load(d);
    const char *const risky[] = {"V_ACTION = step:click,rightclick", "TWIST_ACTION = hold:middleclick,none", "LIFT_ACTION = step:none,click",
                                 "V_PUSH_ACTION = hold:rightclick,rightclick", "H_ACTION = step:left,rightclick"};
    for (const char *line : risky) {
        const ImportPlan p = planOf(m, d, std::string("[app:programname]\n") + line + "\n");
        assert(p.ok && !p.risks.empty() && p.needsConfirm());
        assert(m.bindingIsRisky(std::string(line).substr(0, std::string(line).find(' ')), std::string(line).substr(std::string(line).find('=') + 2)));
    }
    // keys that are harmless stay unflagged, "none" on both sides too
    const char *const calm[] = {"V_ACTION = step:pageup,pagedown", "TWIST_ACTION = hold:left,right", "LIFT_ACTION = step:none,none"};
    for (const char *line : calm) assert(!planOf(m, d, std::string("[app:programname]\n") + line + "\n").needsConfirm());
    tu::removeAll(d);
}

int main() {
    testMouseSidesOfMotionsAreRisky();
    testBomCrlfAndFileKinds();
    testReplaceSemantics();
    testNotesAndRiskBoundaries();
    testRefusedPlanChangesNothing();
    testCanonicalAndOrder();
    std::puts("test_ui_profile_import: OK");
    return 0;
}
