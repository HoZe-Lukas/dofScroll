// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The raised ceilings of docs/plan-motion-functions.md, section "Limits": profile file 1 MiB, action text 32 KiB, 10000 keys
// per section, 1000 own functions per section, 256 macro steps. Each one is tested just below and just above, and the loader's
// file size limit (src/profile_files.*) is checked against the constants of config.hpp (the parser's limits): both must agree.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "config.hpp"
#include "macro.hpp"
#include "modes.hpp"
#include "profile_files.hpp"
#include "testutil.hpp"

using namespace spn;
namespace stdfs = std::filesystem;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

static void limitPut(const std::string &p, const std::string &c) {
    stdfs::create_directories(stdfs::path(p).parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << c;
}

static bool anyContains(const std::vector<std::string> &v, const std::string &needle) {
    for (const std::string &x : v)
        if (x.find(needle) != std::string::npos) return true;
    return false;
}

// "macro:+a" + blanks + "-a", exactly n bytes long (blanks between steps are allowed).
static std::string macroTextOfSize(size_t n) {
    const std::string head = "macro:+a", tail = "-a";
    assert(n > head.size() + tail.size() + 1);
    return head + std::string(n - head.size() - tail.size(), ' ') + tail;
}

// A comment-only file of exactly n bytes after `head`: lines of 200 bytes, so no line is near the line limit.
static std::string padTo(std::string head, size_t n) {
    assert(head.size() <= n);
    while (head.size() < n) {
        const size_t room = n - head.size();
        const size_t len = room > 201 ? 200 : room - 1;  // '#' plus filler, then '\n'
        if (room == 1) {  // only a newline fits
            head += "\n";
            break;
        }
        head += "#" + std::string(len - 1, 'x') + "\n";
    }
    return head;
}

static void testConstants() {
    CHECK(kMaxConfigBytes == 1024u * 1024u, "dofscroll.conf: 1 MiB");
    CHECK(kMaxProfileFileBytes == 1024u * 1024u, "a profile file: 1 MiB (was 64 KiB)");
    CHECK(kMaxProfileFileBytes == kMaxConfigBytes, "the loader and config.cpp agree on the file size");
    CHECK(kMaxActionTextBytes == 32u * 1024u, "action text: 32 KiB");
    CHECK(kMaxKeysPerSection == 10000u, "keys per section: 10000 (was 512)");
    CHECK(kMaxOwnFunctionsPerSection == 1000u, "own functions per section: 1000 (was 32)");
    CHECK(kMaxMacroSteps == 256u, "macro steps: 256");
    CHECK(kMaxLineBytes >= kMaxActionTextBytes + 64, "a line holds the longest action and its key");
    CHECK(kMaxLineBytes <= kMaxConfigBytes, "and a line is not longer than a file");
}

// ---------------------------------------------------------------- the profile file size
static void testProfileFileSize() {
    const std::string d = tu::makeTempDir("limits-profile");
    limitPut(d + "/dofscroll.conf", "[global]\n");
    const std::string head = "[app:Edge]\nENABLED = 0\n";
    const std::string at = padTo(head, kMaxProfileFileBytes);
    CHECK(at.size() == kMaxProfileFileBytes, "built %zu bytes", at.size());
    limitPut(d + "/apps/below.conf", padTo("[app:Below]\nENABLED = 0\n", kMaxProfileFileBytes - 1));
    limitPut(d + "/apps/at.conf", at);
    limitPut(d + "/apps/over.conf", padTo("[app:Over]\nENABLED = 0\n", kMaxProfileFileBytes + 1));
    ProfileSet ps;
    std::vector<FileDiag> diag;
    CHECK(loadProfileTree(d, &ps, &diag), "the tree loads");
    const FileDiag *dBelow = nullptr, *dAt = nullptr, *dOver = nullptr;
    for (const FileDiag &x : diag) {
        if (x.path == d + "/apps/below.conf") dBelow = &x;
        if (x.path == d + "/apps/at.conf") dAt = &x;
        if (x.path == d + "/apps/over.conf") dOver = &x;
    }
    CHECK(dBelow && !dBelow->fatal && dBelow->section != ProfileSet::npos, "1 MiB - 1 byte loads");
    CHECK(dAt && !dAt->fatal && dAt->section != ProfileSet::npos, "exactly 1 MiB loads");
    CHECK(dOver && dOver->fatal && dOver->section == ProfileSet::npos, "1 MiB + 1 byte is rejected");
    CHECK(dOver && anyContains(dOver->warnings, "too large") && anyContains(dOver->warnings, std::to_string(kMaxProfileFileBytes)),
          "the message names the limit: %s", dOver && !dOver->warnings.empty() ? dOver->warnings[0].c_str() : "-");
    CHECK(dOver && dOver->path == d + "/apps/over.conf", "and the file");
    CHECK(ps.find(Section::APP, "Edge") != ProfileSet::npos && ps.find(Section::APP, "Below") != ProfileSet::npos &&
              ps.find(Section::APP, "Over") == ProfileSet::npos,
          "only the profiles within the limit are in the set");
    tu::removeAll(d);
}

// ---------------------------------------------------------------- the total budget of the profile tree (#317)
static const FileDiag *budgetDiagOf(const std::vector<FileDiag> &diag, const std::string &path) {
    for (const FileDiag &x : diag)
        if (x.path == path) return &x;
    return nullptr;
}

static std::string budgetName(const char *dir, int i) {
    char b[32];
    std::snprintf(b, sizeof b, "/%s/f%03d.conf", dir, i);
    return b;
}

static void testProfileTotalBudget() {
    CHECK(kMaxProfileTotalBytes == 16u * 1024u * 1024u, "the total budget: 16 MiB");
    CHECK(kMaxProfileTotalBytes % kMaxProfileFileBytes == 0 && kMaxProfileTotalBytes >= 2 * kMaxProfileFileBytes,
          "the budget holds several full files");
    const size_t nFull = kMaxProfileTotalBytes / kMaxProfileFileBytes - 1;  // 15 files of 1 MiB, the 16th is cut to size
    for (int variant = 0; variant < 2; variant++) {  // 0: exactly the budget, 1: one byte over
        const std::string d = tu::makeTempDir("limits-budget");
        limitPut(d + "/dofscroll.conf", "[global]\nZOOM_SENS = 9\n");
        for (size_t i = 0; i < nFull; i++)
            limitPut(d + budgetName("devices", static_cast<int>(i)),
                     padTo("[device:D" + std::to_string(i) + "]\nENABLED = 0\n", kMaxProfileFileBytes));
        const size_t rest = kMaxProfileTotalBytes - nFull * kMaxProfileFileBytes;  // 1 MiB left for two files
        limitPut(d + "/apps/a.conf", padTo("[app:A]\nENABLED = 0\n", rest - 100));
        limitPut(d + "/apps/b.conf", padTo("[app:B]\nENABLED = 0\n", 100 + static_cast<size_t>(variant)));
        ProfileSet ps;
        std::vector<FileDiag> diag;
        CHECK(loadProfileTree(d, &ps, &diag), "the tree loads");
        CHECK(ps.resolve("", "").zoom.sensitivity == 9, "the global file is loaded");
        const FileDiag *dA = budgetDiagOf(diag, d + "/apps/a.conf"), *dB = budgetDiagOf(diag, d + "/apps/b.conf");
        CHECK(dA && !dA->fatal && dA->section != ProfileSet::npos, "variant %d: a.conf within the budget", variant);
        if (variant == 0) {
            CHECK(dB && !dB->fatal && dB->section != ProfileSet::npos, "exactly the budget loads");
            CHECK(ps.find(Section::APP, "B") != ProfileSet::npos, "B is in the set");
        } else {
            CHECK(dB && dB->fatal && dB->section == ProfileSet::npos, "one byte over the budget: skipped");
            CHECK(dB && anyContains(dB->warnings, d + "/apps/b.conf") && anyContains(dB->warnings, std::to_string(kMaxProfileTotalBytes)),
                  "the warning names the file and the budget: %s", dB && !dB->warnings.empty() ? dB->warnings[0].c_str() : "-");
            CHECK(ps.find(Section::APP, "B") == ProfileSet::npos && ps.find(Section::APP, "A") != ProfileSet::npos,
                  "the daemon keeps what was loaded");
        }
        CHECK(ps.find(Section::DEVICE, "D0") != ProfileSet::npos && ps.find(Section::DEVICE, "D" + std::to_string(nFull - 1)) != ProfileSet::npos,
              "the files read first are loaded");
        tu::removeAll(d);
    }
}

static void testProfileBudgetGlobalFirst() {
    // devices/ alone fills the budget; the global file and the files in sort order up to the budget are loaded, the rest is named.
    const std::string d = tu::makeTempDir("limits-budget-global");
    limitPut(d + "/dofscroll.conf", "[global]\nZOOM_SENS = 4\n");
    const size_t n = kMaxProfileTotalBytes / kMaxProfileFileBytes + 3;
    for (size_t i = 0; i < n; i++)
        limitPut(d + budgetName("devices", static_cast<int>(i)), padTo("[device:G" + std::to_string(i) + "]\nENABLED = 0\n", kMaxProfileFileBytes));
    limitPut(d + "/apps/x.conf", "[app:X]\nENABLED = 0\n");
    ProfileSet ps;
    std::vector<FileDiag> diag;
    CHECK(loadProfileTree(d, &ps, &diag), "the tree loads");
    CHECK(ps.resolve("", "").zoom.sensitivity == 4, "the global file is loaded first, whatever the profile files hold");
    size_t loaded = 0, skipped = 0;
    for (size_t i = 0; i < n; i++) {
        const FileDiag *x = budgetDiagOf(diag, d + budgetName("devices", static_cast<int>(i)));
        CHECK(x != nullptr, "every file has an entry");
        if (!x) continue;
        if (x->section != ProfileSet::npos) {
            loaded++;
            CHECK(skipped == 0, "files load in sort order, no loaded file after a skipped one (file %zu)", i);
        } else {
            skipped++;
            CHECK(x->fatal && anyContains(x->warnings, x->path) && anyContains(x->warnings, std::to_string(kMaxProfileTotalBytes)),
                  "a skipped file is named with the budget");
        }
    }
    CHECK(loaded == kMaxProfileTotalBytes / kMaxProfileFileBytes && skipped == 3, "loaded %zu skipped %zu", loaded, skipped);
    const FileDiag *dx = budgetDiagOf(diag, d + "/apps/x.conf");
    CHECK(dx && dx->fatal && dx->section == ProfileSet::npos, "apps/ is read after devices/: the budget is used up");
    tu::removeAll(d);
}

static void testProfileBudgetManyFiles() {
    const std::string d = tu::makeTempDir("limits-budget-many");
    limitPut(d + "/dofscroll.conf", "[global]\nZOOM_SENS = 6\n");
    for (int i = 0; i < 1000; i++) {
        const char *dir = i < 600 ? "devices" : "apps";
        const std::string head = std::string(i < 600 ? "[device:M" : "[app:M") + std::to_string(i) + "]\nENABLED = 0\n";
        limitPut(d + budgetName(dir, i), padTo(head, 40 * 1024));
    }
    ProfileSet ps;
    std::vector<FileDiag> diag;
    CHECK(loadProfileTree(d, &ps, &diag), "1000 files: the tree loads");
    CHECK(ps.resolve("", "").zoom.sensitivity == 6, "the global file is loaded");
    size_t bytes = 0, skipped = 0;
    for (const FileDiag &x : diag) {
        if (x.origin != FileDiag::PROFILE_FILE) continue;
        if (x.section != ProfileSet::npos) bytes += 40 * 1024;
        else if (x.fatal) skipped++;
    }
    CHECK(bytes <= kMaxProfileTotalBytes && skipped > 0, "loaded %zu bytes, skipped %zu", bytes, skipped);
    tu::removeAll(d);
}

static void testMainFileSize() {
    const std::string d = tu::makeTempDir("limits-main");
    const std::string p = d + "/dofscroll.conf";
    ProfileSet ps;
    std::vector<std::string> w;
    limitPut(p, padTo("[global]\nZOOM_SENS = 7\n", kMaxConfigBytes));
    CHECK(loadProfiles(p, &ps, &w) && ps.resolve("", "").zoom.sensitivity == 7, "exactly 1 MiB loads");
    limitPut(p, padTo("[global]\nZOOM_SENS = 7\n", kMaxConfigBytes - 1));
    w.clear();
    CHECK(loadProfiles(p, &ps, &w) && ps.resolve("", "").zoom.sensitivity == 7, "1 MiB - 1 byte loads");
    limitPut(p, padTo("[global]\nZOOM_SENS = 7\n", kMaxConfigBytes + 1));
    w.clear();
    CHECK(!loadProfiles(p, &ps, &w) && anyContains(w, "too large"), "1 MiB + 1 byte is rejected: %s", w.empty() ? "-" : w[0].c_str());
    tu::removeAll(d);
}

// ---------------------------------------------------------------- the action text
static void testActionTextLimit() {
    ButtonActionType t = ButtonActionType::Unbound;
    Key k = Key::Space;
    std::string err;
    MacroSide side;
    // exactly 32 KiB and one byte less: valid; one byte more: refused with a message that names the limit
    CHECK(macroTextOfSize(kMaxActionTextBytes).size() == kMaxActionTextBytes, "built the text");
    CHECK(parseButtonAction(macroTextOfSize(kMaxActionTextBytes), &t, &k, &err, nullptr, nullptr, &side) && side.press.size() == 2,
          "a button action of 32 KiB: %s", err.c_str());
    CHECK(parseButtonAction(macroTextOfSize(kMaxActionTextBytes - 1), &t, &k, &err), "32 KiB - 1 byte: %s", err.c_str());
    CHECK(!parseButtonAction(macroTextOfSize(kMaxActionTextBytes + 1), &t, &k, &err) && err.find("too long") != std::string::npos &&
              err.find(std::to_string(kMaxActionTextBytes)) != std::string::npos,
          "32 KiB + 1 byte: %s", err.c_str());
    ChannelAction a;
    const std::string pad = macroTextOfSize(kMaxActionTextBytes - 5);  // "macro:+a   -a" ... then ",none"
    CHECK(parseChannelAction(pad + ",none", &a, &err) && a.type == ChannelActionType::Macro, "a channel action of 32 KiB: %s", err.c_str());
    CHECK(!parseChannelAction(macroTextOfSize(kMaxActionTextBytes - 4) + ",none", &a, &err) && err.find("too long") != std::string::npos,
          "a channel action of 32 KiB + 1 byte: %s", err.c_str());
    // the old limit of the plan (256 bytes) and the old line limit (8 KiB) no longer apply
    CHECK(parseButtonAction(macroTextOfSize(257), &t, &k, &err), "257 bytes");
    CHECK(parseButtonAction(macroTextOfSize(9000), &t, &k, &err), "9000 bytes: %s", err.c_str());
    // the same through applyKey, which every file goes through
    Settings s;
    CHECK(applyKey(s, "BUTTON_3", macroTextOfSize(kMaxActionTextBytes), &err), "BUTTON_3 at the limit: %s", err.c_str());
    CHECK(s.buttons.at(3) == "macro:+a -a", "and it is stored in the short canonical form: %s", s.buttons.at(3).c_str());
    CHECK(!applyKey(s, "BUTTON_4", macroTextOfSize(kMaxActionTextBytes + 1), &err) && s.buttons.count(4) == 0, "BUTTON_4 above it");
    CHECK(applyKey(s, "H_ACTION", pad + ",none", &err), "H_ACTION at the limit: %s", err.c_str());
    CHECK(!applyKey(s, "V_ACTION", macroTextOfSize(kMaxActionTextBytes + 1) + ",none", &err), "V_ACTION above it");
}

// A file line holds an action of 32 KiB; a longer line is dropped as a line, a longer action with its line and key named.
static void testLinesInFiles() {
    const std::string longest = "BUTTON_5 = " + macroTextOfSize(kMaxActionTextBytes) + "\n";
    CHECK(longest.size() < kMaxLineBytes, "the longest action fits a line");
    ProfileSet ps;
    std::vector<std::string> w;
    CHECK(parseProfilesText("[global]\nZOOM_SENS = 3\n" + longest + "SCROLL_V_SENS = 4\n", "t.conf", &ps, &w), "parse");
    Settings s = ps.resolve("", "");
    CHECK(s.buttons.count(5) == 1 && s.buttons.at(5) == "macro:+a -a" && s.zoom.sensitivity == 3 && s.scroll_v.sensitivity == 4,
          "the 32 KiB action loaded and its neighbours too");
    CHECK(!anyContains(w, "too long"), "without a warning: %s", w.empty() ? "-" : w[0].c_str());

    // one byte over the action limit: the line is read, the action refused, the message names file, line and key
    w.clear();
    ProfileSet ps2;
    parseProfilesText("[global]\nZOOM_SENS = 3\nBUTTON_5 = " + macroTextOfSize(kMaxActionTextBytes + 1) + "\nSCROLL_V_SENS = 4\n", "t.conf", &ps2, &w);
    s = ps2.resolve("", "");
    CHECK(s.buttons.count(5) == 0 && s.scroll_v.sensitivity == 4, "the line was dropped, the next one kept");
    CHECK(anyContains(w, "t.conf:3:") && anyContains(w, "BUTTON_5") && anyContains(w, "action text too long"), "message: %s", w.empty() ? "-" : w[0].c_str());

    // a line over the line limit: ignored as a line
    w.clear();
    ProfileSet ps3;
    parseProfilesText("[global]\nBUTTON_5 = " + std::string(kMaxLineBytes, 'x') + "\nSCROLL_V_SENS = 4\n", "t.conf", &ps3, &w);
    CHECK(ps3.resolve("", "").scroll_v.sensitivity == 4 && anyContains(w, "line too long") && anyContains(w, "t.conf:2:"), "line limit: %s", w.empty() ? "-" : w[0].c_str());
    // the 257th macro step: rejected with the line named
    w.clear();
    std::string steps;
    for (int i = 0; i < 257; ++i) steps += (i ? " " : "") + std::string("wait:1");
    ProfileSet ps4;
    parseProfilesText("[global]\nBUTTON_7 = macro:+a\nBUTTON_8 = macro:" + steps + "\n", "t.conf", &ps4, &w);
    CHECK(ps4.resolve("", "").buttons.count(7) == 1 && ps4.resolve("", "").buttons.count(8) == 0, "the 257th step is rejected");
    CHECK(anyContains(w, "t.conf:3:") && anyContains(w, "too many steps"), "message: %s", w.empty() ? "-" : w[0].c_str());
}

static void testModeFileLines() {
    // a mode file takes the same line length (it was 4096 bytes)
    const std::string dir = tu::makeTempDir("limits-mode");
    limitPut(dir + "/long.conf", "BUTTON_1 = " + macroTextOfSize(kMaxActionTextBytes) + "\nBUTTON_2 = " + macroTextOfSize(6000) + "\n");
    std::vector<std::string> w;
    ModeSet set = loadModes(dir, &w);
    const ModeDef *m = set.find("long");
    CHECK(m && m->buttons.size() == 2, "both long lines load: %s", w.empty() ? "-" : w[0].c_str());
    limitPut(dir + "/long.conf", "BUTTON_1 = " + macroTextOfSize(kMaxActionTextBytes + 1) + "\nBUTTON_2 = macro:+a\n");
    w.clear();
    set = loadModes(dir, &w);
    m = set.find("long");
    CHECK(m && m->buttons.size() == 1 && m->buttons[0].button == 2 && anyContains(w, "action text too long"), "one byte over: %s", w.empty() ? "-" : w[0].c_str());
    limitPut(dir + "/long.conf", "BUTTON_1 = " + std::string(kMaxLineBytes, 'x') + "\nBUTTON_2 = macro:+a\n");
    w.clear();
    set = loadModes(dir, &w);
    m = set.find("long");
    CHECK(m && m->buttons.size() == 1 && anyContains(w, "line too long"), "a line over the line limit: %s", w.empty() ? "-" : w[0].c_str());
    tu::removeAll(dir);
}

// ---------------------------------------------------------------- keys and own functions per section
static void testKeysPerSection() {
    // No more than a few hundred distinct valid keys exist today, so the limit is exercised through the check the loader and the
    // interface share (sectionKeysFull): just below and at the limit.
    CHECK(!sectionKeysFull(0) && !sectionKeysFull(kMaxKeysPerSection - 1), "9999 keys: room for one more");
    CHECK(sectionKeysFull(kMaxKeysPerSection) && sectionKeysFull(kMaxKeysPerSection + 1), "10000 keys: the 10001st is refused");
    // the keys that exist all fit
    Settings s;
    CHECK(allKeys(s).size() < kMaxKeysPerSection / 10, "a full key list is far below the limit");
    CHECK(kMaxOwnFunctionsPerSection <= kMaxKeysPerSection, "own functions are keys of their section");
    CHECK(kMaxOwnFunctionsPerSection == 1000 && kMaxKeysPerSection == 10 * kMaxOwnFunctionsPerSection, "1000 of 10000");
}

int main() {
    testConstants();
    testProfileFileSize();
    testProfileTotalBudget();
    testProfileBudgetGlobalFirst();
    testProfileBudgetManyFiles();
    testMainFileSize();
    testActionTextLimit();
    testLinesInFiles();
    testModeFileLines();
    testKeysPerSection();
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("test_limits OK");
    return 0;
}
