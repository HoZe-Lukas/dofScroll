// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Text is UTF-8 everywhere (docs/encoding.md): a table of nasty strings goes through the config
// round trip, section names, mode names and descriptions, the log sanitizer, profile matching,
// the autostart renderers and the file-system layer, the last ones inside a temporary directory
// whose name has spaces, parentheses, non-ASCII text and shell/INI special characters.
// The source is ASCII only: all non-ASCII text is written as \x escapes (check-english: allow).
#include <cassert>
#include <clocale>
#include <cstdio>
#include <string>
#include <vector>

#include "config.hpp"
#include "log_sanitize.hpp"
#include "modes.hpp"
#include "platform/autostart_os.hpp"
#include "platform/fs.hpp"
#include "platform/path_rules.hpp"
#include "profile_match.hpp"
#include "profile_files.hpp"
#include "seed.hpp"
#include "testutil.hpp"
#include "ui/model/mode_catalog.hpp"
#include "utf8.hpp"

using namespace spn;

namespace {

struct Nasty {
    std::string text;
    bool valid;     // valid UTF-8
    const char *what;
};

// Every entry is a candidate for a name or a text value.
std::vector<Nasty> table() {
    return {
        {"plain", true, "ascii"},
        {"Visual Studio Code", true, "spaces"},
        {"M\xc3\xbcller Tool", true, "umlaut"},
        {"caf\xc3\xa9", true, "2-byte"},
        {"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", true, "CJK 3-byte"},
        {"\xf0\x9f\x98\x80 smile", true, "emoji 4-byte"},
        {"e\xcc\x81 (e + combining acute)", true, "combining mark"},
        {"\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \xd8\xb3\xd9\x84\xd8\xa7\xd9\x85", true, "right-to-left"},
        {"\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7", true, "emoji ZWJ sequence"},
        {"a\xe2\x80\xaez" "b", true, "bidi override"},
        {"x\xef\xbb\xbfy", true, "BOM inside the text"},
        {"quote\"s and 'apos'", true, "quotes"},
        {"a#b", true, "hash"},
        {"a;b", true, "semicolon"},
        {"a=b", true, "equals"},
        {"100% & (x86)", true, "percent ampersand parentheses"},
        {"[1] list", true, "leading bracket"},
        {"a[1]b", true, "brackets inside"},
        {"name]", true, "trailing bracket"},
        {"\xf4\x8f\xbf\xbf", true, "U+10FFFF"},
        {"\xc0\x80", false, "overlong NUL"},
        {"\xe0\x80\x80", false, "overlong 3-byte"},
        {"\xed\xa0\x80", false, "surrogate"},
        {"\xf4\x90\x80\x80", false, "above U+10FFFF"},
        {"\xc3", false, "truncated 2-byte"},
        {"ab\xe6\x97", false, "truncated 3-byte"},
        {"\xf0\x9f\x98", false, "truncated 4-byte"},
        {"\xff\xfe", false, "never valid"},
        {"\x80 stray continuation", false, "stray continuation"},
    };
}

bool hasControl(const std::string &s) {
    for (unsigned char c : s)
        if (c < 0x20 || c == 0x7f) return true;
    return false;
}

std::string readAll(const std::string &path) {
    std::string out;
    const fs::ReadResult r = fs::readFileBounded(path, 1u << 20, fs::ReadOptions(), &out);
    assert(r.status == fs::ReadStatus::Ok);
    return out;
}

void writeAll(const std::string &path, const std::string &text) {
    std::string err;
    const bool ok = fs::writeFileReplace(path, text, &err);
    if (!ok) std::fprintf(stderr, "write %s: %s\n", path.c_str(), err.c_str());
    assert(ok);
}

bool anyWarningContains(const std::vector<std::string> &w, const std::string &needle) {
    for (const std::string &s : w)
        if (s.find(needle) != std::string::npos) return true;
    return false;
}

// ---------------------------------------------------------------- helpers

// Bidirectional controls and zero-width characters are dropped from display text (utf8::isSpoofing).
bool hasSpoofing(const std::string &s) {
    for (size_t i = 0; i < s.size(); ++i)
        if (utf8::isSpoofing(s, i)) return true;
    return false;
}

void testUtf8Helpers() {
    for (const Nasty &n : table()) {
        assert(utf8::isValid(n.text) == n.valid && n.what);
        // truncation at every length: a prefix, valid when the input was valid, never mid-character
        for (size_t cap = 0; cap <= n.text.size() + 1; cap++) {
            const std::string t = utf8::truncate(n.text, cap);
            assert(t.size() <= cap && n.text.compare(0, t.size(), t) == 0);
            if (n.valid) assert(utf8::isValid(t));
            const std::string d = utf8::sanitizeDisplay(n.text, cap);
            assert(d.size() <= cap && utf8::isValid(d) && !hasControl(d));
        }
        assert(utf8::sanitizeDisplay(n.text, 4096).find('\0') == std::string::npos);
        if (n.valid && !hasControl(n.text) && !hasSpoofing(n.text)) assert(utf8::sanitizeDisplay(n.text, 4096) == n.text);
        else if (!n.valid) assert(utf8::sanitizeDisplay(n.text, 4096).find("\xEF\xBF\xBD") != std::string::npos);
    }
    assert(utf8::firstInvalid("ab\xe6\x97") == 2);
    assert(utf8::sanitizeDisplay("a\x01\x7f\xc2\x85" "b", 64) == "ab");  // C0, DEL, C1 dropped
    assert(utf8::truncate("\xe6\x97\xa5\xe6\x9c\xac", 5) == "\xe6\x97\xa5");
    // ASCII-only helpers never change bytes >= 0x80, whatever the locale says
    for (int b = 0x80; b <= 0xff; b++) {
        const std::string s(1, static_cast<char>(b));
        assert(utf8::lowerAscii(s) == s && utf8::upperAscii(s) == s && utf8::trim(s) == s);
    }
    assert(utf8::lowerAscii("AbC\xc3\x9c") == "abc\xc3\x9c" && utf8::upperAscii("abc\xc3\xbc") == "ABC\xc3\xbc");
    assert(utf8::trim(" \t\xc2\xa0x\xc2\xa0 \r\n") == "\xc2\xa0x\xc2\xa0");  // NBSP is not white space
}

void testNumbersAreLocaleFree() {
    assert(strtodC("0.5", nullptr) == 0.5 && formatG(6, 0.5) == "0.5");
    // a locale with a decimal comma, when the machine has one
    const char *locs[] = {"de_DE.UTF-8", "de_DE.utf8", "de_DE", "fr_FR.UTF-8", "fr_FR.utf8"};
    const std::string old = std::setlocale(LC_NUMERIC, nullptr) ? std::setlocale(LC_NUMERIC, nullptr) : "C";
    for (const char *l : locs) {
        if (!std::setlocale(LC_NUMERIC, l)) continue;
        char *end = nullptr;
        assert(strtodC("0.5x", &end) == 0.5 && *end == 'x');
        assert(formatG(6, 1.25) == "1.25");
        assert(strtodC("1,5", &end) == 1.0 && *end == ',');  // a comma is not a decimal point
        Settings s;
        std::string err;
        assert(applyKey(s, "ZOOM_EXIT_RATIO", "1.5", &err) && s.zoom_exit_ratio == 1.5);  // C format
        break;
    }
    std::setlocale(LC_NUMERIC, old.c_str());
}

void testProfileMatching() {
    assert(patternMatches("m\xc3\xbcller", "Tool M\xc3\x9cLLER") == false);  // U+00FC vs U+00DC: no Unicode folding
    assert(patternMatches("m\xc3\xbcller", "Tool M\xc3\xbcller 2"));         // ASCII letters fold, umlaut exact
    assert(patternMatches("\xe6\x97\xa5\xe6\x9c\xac", "app \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"));
    assert(patternMatches("\xf0\x9f\x98\x80", "x\xf0\x9f\x98\x80y"));
    assert(patternMatches("VISUAL STUDIO CODE", "visual studio code"));
    // NFC and NFD are different texts (no normalization, docs/encoding.md)
    assert(!patternMatches("caf\xc3\xa9", "cafe\xcc\x81"));
    assert(!patternMatches("", "x") && !patternMatches("x", ""));
    for (const Nasty &n : table()) {
        assert(patternMatches(n.text, n.text) == !n.text.empty());
        assert(lowerAscii(n.text).size() == n.text.size());
    }
    // the same with a single-byte locale active: bytes of multi-byte characters must stay untouched
    const char *locs[] = {"en_US.ISO-8859-1", "de_DE.ISO-8859-1", "de_DE.iso88591", "en_US.iso88591"};
    const std::string old = std::setlocale(LC_CTYPE, nullptr) ? std::setlocale(LC_CTYPE, nullptr) : "C";
    for (const char *l : locs) {
        if (!std::setlocale(LC_CTYPE, l)) continue;
        for (const Nasty &n : table()) {
            assert(lowerAscii(n.text) == utf8::lowerAscii(n.text));
            assert(patternMatches(n.text, "<" + n.text + ">") == !n.text.empty());
        }
        break;
    }
    std::setlocale(LC_CTYPE, old.c_str());
}

void testLogSanitizer() {
    for (const Nasty &n : table()) {
        const std::string s = sanitizeForLog(n.text, 4096);
        assert(utf8::isValid(s) && !hasControl(s));
        if (n.valid && !hasControl(n.text) && utf8::sanitizeDisplay(n.text, 4096) == n.text)
            assert(s == n.text || n.text.find("\xc2") != std::string::npos);  // valid text stays intact
        // cut at every length: never a half character, at most maxLen + "..."
        for (size_t cap = 1; cap < n.text.size() + 2; cap++) {
            const std::string c = sanitizeForLog(n.text, cap);
            assert(utf8::isValid(c) && c.size() <= cap + 3);
        }
    }
    assert(sanitizeForLog("a\x01" "b\xc3\xbc") == "a?b\xc3\xbc");
    assert(sanitizeForLog("\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", 7) == "\xe6\x97\xa5\xe6\x9c\xac...");
    assert(sanitizeForLog("\xf0\x9f\x98\x80\xf0\x9f\x98\x80", 5) == "\xf0\x9f\x98\x80...");
}

// ---------------------------------------------------------------- config

void testConfigRoundTrip(const std::string &dir) {
    const std::string path = dir + "/rt.conf";
    for (int kind = 0; kind < 2; kind++) {
        ProfileSet ps;
        std::vector<std::string> expect;
        for (const Nasty &n : table()) {
            const std::string pat = utf8::trim(n.text);
            if (!n.valid || hasControl(pat)) continue;
            const size_t i = ps.findOrAdd(kind ? Section::DEVICE : Section::APP, pat);
            assert(i != ProfileSet::npos);
            ps.sections[i].kv.emplace_back("MODE", "normal");
            expect.push_back(pat);
        }
        std::string err;
        assert(saveProfiles(path, ps, &err));
        const std::string bytes1 = readAll(path);
        assert(bytes1.compare(0, 3, "\xEF\xBB\xBF") != 0);  // no BOM written
        assert(bytes1.find('\r') == std::string::npos);     // LF only
        assert(utf8::isValid(bytes1));

        ProfileSet back;
        std::vector<std::string> warn;
        assert(loadProfiles(path, &back, &warn) && warn.empty());
        assert(back.sections.size() == ps.sections.size());
        for (size_t i = 1; i < back.sections.size(); i++) assert(back.sections[i].pattern == ps.sections[i].pattern);
        assert(back.sections.size() == expect.size() + 1);

        // load -> save is byte exact
        assert(saveProfiles(path, back, &err));
        assert(readAll(path) == bytes1);

        // BOM + CRLF + a case change of the section prefix: same result
        std::string crlf = "\xEF\xBB\xBF";
        for (char c : bytes1) {
            if (c == '\n') crlf += '\r';
            crlf += c;
        }
        writeAll(path, crlf);
        ProfileSet viaCrlf;
        warn.clear();
        assert(loadProfiles(path, &viaCrlf, &warn) && warn.empty());
        assert(viaCrlf.sections.size() == back.sections.size());
        for (size_t i = 1; i < viaCrlf.sections.size(); i++)
            assert(viaCrlf.sections[i].pattern == back.sections[i].pattern);
    }
}

void testConfigSectionNames(const std::string &dir) {
    const std::string path = dir + "/names.conf";
    const std::string text =
        "\xEF\xBB\xBF[global]\r\nMODE = normal\r\n\r\n"
        "[app:Visual Studio Code]\r\nMODE = off\r\n"
        "[APP: M\xc3\xbcller Tool ]\n MODE = timeline\n"
        "[device:3Dconnexion \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x98\x80]\nMODE = off\n"
        "[app:a[1]]\nMODE = off\n"
        "[app:b] # comment with ] bracket\nMODE = timeline\n"
        "[app:semi;colon=eq#hash]\nMODE = off\n";
    writeAll(path, text);
    ProfileSet ps;
    std::vector<std::string> warn;
    assert(loadProfiles(path, &ps, &warn) && warn.empty());
    assert(ps.sections.size() == 7);
    assert(ps.find(Section::APP, "visual studio code") == 1);
    assert(ps.sections[2].pattern == "M\xc3\xbcller Tool");
    assert(ps.find(Section::APP, "m\xc3\xbcller tool") == 2);
    assert(ps.find(Section::APP, "M\xc3\x9cLLER TOOL") == ProfileSet::npos);  // no Unicode folding
    assert(ps.sections[3].kind == Section::DEVICE &&
           ps.sections[3].pattern == "3Dconnexion \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x98\x80");
    assert(ps.sections[4].pattern == "a[1]");
    assert(ps.sections[5].pattern == "b");
    assert(ps.sections[6].pattern == "semi;colon=eq#hash");
    std::string err;
    assert(saveProfiles(path, ps, &err));
    ProfileSet again;
    assert(loadProfiles(path, &again, &warn) && again.sections.size() == 7);
    for (size_t i = 1; i < 7; i++) assert(again.sections[i].pattern == ps.sections[i].pattern);

    // a name that would end its own header early cannot be saved (and is refused, not corrupted)
    ProfileSet bad;
    assert(bad.findOrAdd(Section::APP, "x] # y") != ProfileSet::npos);
    assert(!saveProfiles(dir + "/bad.conf", bad, &err) && !tu::exists(dir + "/bad.conf"));
    // control characters and over-long names are refused
    assert(bad.findOrAdd(Section::APP, "tab\x01" "x") == ProfileSet::npos);
    assert(bad.findOrAdd(Section::APP, std::string(kMaxPatternBytes + 1, 'a')) == ProfileSet::npos);
    // at the limit, in 3-byte characters: kept whole
    std::string big;
    while (big.size() + 3 <= kMaxPatternBytes) big += "\xe6\x97\xa5";
    ProfileSet lim;
    assert(lim.findOrAdd(Section::APP, big) == 1);
    assert(saveProfiles(path, lim, &err));
    ProfileSet limBack;
    assert(loadProfiles(path, &limBack, &warn) && limBack.sections.size() == 2 && limBack.sections[1].pattern == big);
}

void testConfigInvalidUtf8(const std::string &dir) {
    const std::string path = dir + "/invalid.conf";
    // invalid bytes in a section name: warning with file:line, rest of the file kept, bytes kept on save
    const std::string odd = "bad\xc3 name \xff\xfe \xed\xa0\x80";
    writeAll(path, "[global]\nMODE = normal\n[app:" + odd + "]\nMODE = off\n[app:good]\nMODE = timeline\n");
    ProfileSet ps;
    std::vector<std::string> warn;
    assert(loadProfiles(path, &ps, &warn));
    assert(warn.size() == 1 && warn[0].find("invalid.conf:3:") != std::string::npos &&
           warn[0].find("invalid UTF-8") != std::string::npos);
    assert(ps.sections.size() == 3 && ps.sections[1].pattern == odd && ps.sections[2].pattern == "good");
    std::string err;
    assert(saveProfiles(path, ps, &err));
    ProfileSet back;
    warn.clear();
    assert(loadProfiles(path, &back, &warn) && back.sections.size() == 3 && back.sections[1].pattern == odd);
    assert(anyWarningContains(warn, "invalid.conf:"));
    // invalid bytes in a value: the value is refused by its parser, the rest of the file is used
    writeAll(path, "[global]\nMODE = nor\xc3mal\nSCROLL_SPEED = 2\n");
    warn.clear();
    assert(loadProfiles(path, &back, &warn));
    assert(anyWarningContains(warn, "invalid.conf:2:"));
    // lines over the limit are ignored whole (counted in bytes), the rest is kept
    std::string longLine = "[app:";
    while (longLine.size() < kMaxLineBytes + 8) longLine += "\xe6\x97\xa5";
    writeAll(path, "[global]\n" + longLine + "]\nMODE = off\n[app:after]\nMODE = off\n");
    warn.clear();
    assert(loadProfiles(path, &back, &warn));
    assert(back.sections.size() == 2 && back.sections[1].pattern == "after" && anyWarningContains(warn, "too long"));
}

// ---------------------------------------------------------------- modes

void testModes(const std::string &dir) {
    const std::string mdir = dir + "/modes";
    std::string err;
    assert(fs::mkdirsPrivate(mdir, &err));
    const std::string cjk = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e";
    std::string longName, longDesc;
    for (int i = 0; i < 40; i++) longName += cjk;   // 360 bytes
    for (int i = 0; i < 200; i++) longDesc += "\xf0\x9f\x98\x80";  // 800 bytes
    writeAll(mdir + "/uni.conf", "NAME = M\xc3\xbcller \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x98\x80 a#b\nDESCRIPTION = " +
                                     std::string("e\xcc\x81 \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \"q\" 100% & [x] =y ;z") + "\n");
    writeAll(mdir + "/cut.conf", "NAME = " + longName + "\nDESCRIPTION = " + longDesc + "\n");
    writeAll(mdir + "/bad.conf", "NAME = x\xc3y\xff\xed\xa0\x80z\nDESCRIPTION = d\x01\xc2\x85" "e\n");
    writeAll(mdir + "/bom.conf", "\xEF\xBB\xBFNAME = Bom \xc3\xa9\r\nV_ACTION = zoom\r\n");
    std::vector<std::string> warn;
    const ModeSet ms = loadModes(mdir, &warn);
    const ModeDef *uni = ms.find("uni"), *cut = ms.find("cut"), *bad = ms.find("bad"), *bom = ms.find("bom");
    assert(uni && cut && bad && bom);
    assert(uni->label == "M\xc3\xbcller \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x98\x80 a#b");
    assert(uni->description == "e\xcc\x81 \xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \"q\" 100% & [x] =y ;z");
    assert(cut->label.size() <= 64 && cut->label.size() >= 63 && utf8::isValid(cut->label));
    assert(cut->label.size() % 3 == 0 && cut->label.compare(0, 3, cjk, 0, 3) == 0);  // whole characters
    assert(cut->description.size() <= 256 && cut->description.size() == 256 && utf8::isValid(cut->description));
    assert(utf8::isValid(bad->label) && utf8::isValid(bad->description));
    assert(bad->label == "x\xEF\xBF\xBD" "y\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD\xEF\xBF\xBD" "z");
    assert(bad->description == "de");
    assert(anyWarningContains(warn, "bad.conf:1: invalid UTF-8"));
    assert(bom->label == "Bom \xc3\xa9" && bom->v.type == ChannelActionType::Zoom);
}

void testModeImportAndCatalog(const std::string &dir) {
    const std::string src = dir + "/import source (copy)";
    std::string err;
    assert(fs::mkdirsPrivate(src, &err));
    const std::string file = src + "/my mode \xe6\x97\xa5\xe6\x9c\xac #1.conf";
    writeAll(file, "NAME = Imported \xc3\xa4\xc3\xb6\xc3\xbc \xf0\x9f\x98\x80\nV_ACTION = zoom\n");
    ui::ModeCatalog c(dir + "/catalog modes");
    const ui::ModeCatalog::Result r = c.importFile(file, "Zoom \xc3\xa4 mode");
    if (!r.ok) std::fprintf(stderr, "import: %s\n", r.error.c_str());
    assert(r.ok && r.id == "zoom-mode");
    const ui::ModeCatalog::Result r2 = c.importFile(file);  // id from the odd file name
    assert(r2.ok || !r2.error.empty());
    const ui::ModeCatalog::ModeEntry *m = c.find("zoom-mode");
    assert(m && m->label == "Imported \xc3\xa4\xc3\xb6\xc3\xbc \xf0\x9f\x98\x80");
    assert(ui::ModeCatalog::cleanLabel(std::string(30, '\x01') + "\xe6\x97\xa5\xe6\x9c\xac").find('\x01') == std::string::npos);
    std::string longLabel;
    for (int i = 0; i < 40; i++) longLabel += "\xe6\x97\xa5";  // 120 bytes
    const std::string lab = ui::ModeCatalog::cleanLabel(longLabel);
    assert(lab.size() == 63 && utf8::isValid(lab));
}

// ---------------------------------------------------------------- paths, seeding, file layer

void testFileLayer(const std::string &dir) {
    std::string err;
    const std::string names[] = {
        "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.conf", "sp ace.conf", "e\xcc\x81.conf", "\xf0\x9f\x98\x80.conf",
        "a&b#c;d=e[f]g%25h.conf", "it's (x86).conf"};
    for (const std::string &n : names) writeAll(dir + "/" + n, "x = " + n + "\n");
    std::vector<std::string> list;
    bool capped = false;
    assert(fs::listDir(dir, 0, &list, &capped));
    for (const std::string &n : names) {
        bool seen = false;
        for (const std::string &l : list) seen = seen || l == n;
        assert(seen);  // the name comes back byte for byte
        assert(fs::pathExists(dir + "/" + n) && readAll(dir + "/" + n) == "x = " + n + "\n");
    }
    assert(fs::pathExists(dir));
    assert(!fs::pathExists(dir + "/\xe6\x97\xa5\xe6\x9c\xac.conf"));
    // pure path rules: valid Unicode is fine; invalid UTF-8 and trailing dots/spaces are not
    assert(windowsPathHazard("C:/Pr\xc3\xb6gramme (x86)/\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x98\x80/dofscroll.exe") ==
           PathHazard::None);
    assert(windowsPathHazard("C:/Users/J\xc3\xbcrgen M\xc3\xbcller/AppData") == PathHazard::None);
    assert(windowsPathHazard("C:/bad/\xc3") == PathHazard::InvalidUtf8);
    assert(windowsPathHazard("C:/dir /x") == PathHazard::TrailingDotOrSpace);
    assert(windowsPathHazard("C:/dir./x") == PathHazard::TrailingDotOrSpace);
    assert(windowsPathHazard("C:/a b/c d") == PathHazard::None);
}

void testSeeding(const std::string &root) {
    const std::string ex = root + "/examples (v1)", home = root + "/cfg home";
    std::string err;
    assert(fs::mkdirsPrivate(ex + "/default/" + seedOsName(), &err));
    assert(fs::mkdirsPrivate(home, &err));
    writeAll(ex + "/default/" + seedOsName() + "/dofscroll.conf", "[global]\nMODE = normal\n\n[app:M\xc3\xbcller Tool]\nMODE = off\n");
    const std::string base = tu::useConfigBase(home);
    tu::setEnv("DOFSCROLL_EXAMPLES", ex);
    std::string log;
    assert(seedDefaults(&log));
    // the new layout: [global] in dofscroll.conf, the profile in its own file (UTF-8 name kept in the header)
    assert(readAll(base + "/dofscroll/dofscroll.conf") == "[global]\nMODE = normal\n");
    assert(readAll(base + "/dofscroll/apps/m\xc3\xbcller tool.conf") == "[app:M\xc3\xbcller Tool]\nMODE = off\n");
    assert(!fs::pathExists(base + "/dofscroll/modes"));  // modes are built in, never copied
    assert(log.find("cfg home") != std::string::npos && utf8::isValid(log));
    ProfileSet ps;
    std::vector<std::string> warn;
    std::vector<FileDiag> diag;
    assert(loadProfileTree(base + "/dofscroll", &ps, &diag));
    for (const FileDiag &d : diag) assert(d.warnings.empty() && !d.fatal);
    assert(ps.find(Section::APP, "m\xc3\xbcller tool") == 1);
    tu::unsetEnv("DOFSCROLL_EXAMPLES");
}

void testAutostartRenderers() {
    std::string out, err;
    assert(autostart::quoteRunValue("C:\\Users\\J\xc3\xbcrgen\\Pr\xc3\xb6gramme (x86)\\\xe6\x97\xa5\xe6\x9c\xac\\dofscroll.exe", &out, &err));
    assert(out.front() == '"' && out.back() == '"' && utf8::isValid(out));
    assert(!autostart::quoteRunValue("C:\\bad\\\xc3\\dofscroll.exe", &out, &err) && !err.empty());
    assert(autostart::plistPathOk("/Users/J\xc3\xbcrgen/A & B <1>/dofscroll"));
    assert(!autostart::plistPathOk("/Users/bad\xff/dofscroll"));
    const std::string plist = autostart::renderLaunchAgentPlist(
        "io.github.dofscroll.daemon", "/Users/J\xc3\xbcrgen/A & B <1> \"q\" 'a' \xf0\x9f\x98\x80/dofscroll", true);
    assert(utf8::isValid(plist));
    assert(plist.find("/Users/J\xc3\xbcrgen/A &amp; B &lt;1&gt; &quot;q&quot; &apos;a&apos; \xf0\x9f\x98\x80/dofscroll") !=
           std::string::npos);
    assert(plist.find("A & B") == std::string::npos);
}

}  // namespace

int main() {
    const std::string root = tu::makeTempDir("dofscroll-utf8");
    // spaces, parentheses, Latin-1, CJK, emoji, '&', '#', ';', '=', '[', ']', '%', quotes
    const std::string odd = root + "/Pr\xc3\xb6gramme (x86) \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x98\x80 a&b #x ;y =z [w] 100%25 'q'";
    std::string err;
    const bool made = fs::mkdirsPrivate(odd, &err);
    if (!made) std::fprintf(stderr, "cannot create %s: %s\n", odd.c_str(), err.c_str());
    assert(made);

    testUtf8Helpers();
    testNumbersAreLocaleFree();
    testProfileMatching();
    testLogSanitizer();
    testAutostartRenderers();
    testConfigRoundTrip(odd);
    testConfigSectionNames(odd);
    testConfigInvalidUtf8(odd);
    testModes(odd);
    testModeImportAndCatalog(odd);
    testFileLayer(odd);
    testSeeding(odd);

    tu::removeAll(root);
    std::puts("test_utf8_text: ok");
    return 0;
}
