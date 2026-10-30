// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "profile_files.hpp"

#include <algorithm>
#include <chrono>
#include <map>

#include "platform/fs.hpp"
#include "platform/path_rules.hpp"
#include "utf8.hpp"

namespace spn {

namespace {

const char *dirNameOf(Section::Kind k) { return k == Section::DEVICE ? "devices" : "apps"; }
std::string profileDirOf(const std::string &configDir, Section::Kind k) { return configDir + "/" + dirNameOf(k); }

std::string foldedKey(Section::Kind k, const std::string &name) {
    return std::string(k == Section::DEVICE ? "d:" : "a:") + utf8::lowerAscii(name);
}

std::string stampOf(const fs::FileInfo &fi) {
    return std::string(fi.isLink ? "L" : "F") + std::to_string(static_cast<long long>(fi.mtimeNs)) + ":" +
           std::to_string(static_cast<long long>(fi.ctimeNs)) + ":" +
           std::to_string(static_cast<long long>(fi.size)) + ":" + std::to_string(static_cast<unsigned long long>(fi.fileId));
}

bool isReservedWindowsName(const std::string &stem) {
    // The part before the first dot decides ("con.txt" is reserved too); the rule is the one of path_rules.hpp
    // (CONIN$, CONOUT$, the superscript digits, a space before the dot), so a slug is never unsafe on Windows.
    return isReservedDeviceName(stem);
}

// Bidirectional controls, zero-width and other invisible format characters: a file name with one of them reads as
// another name in a file manager (an RLO reverses the end of "apps/<RLO>rtl.conf"). They are dropped from slugs.
bool isInvisibleFormat(const std::string &s, size_t i, size_t n) {
    auto b = [&](size_t k) { return static_cast<unsigned char>(s[i + k]); };
    if (n == 2) return (b(0) == 0xC2 && b(1) == 0xAD) || (b(0) == 0xD8 && b(1) == 0x9C);          // U+00AD, U+061C
    if (n != 3) return false;
    if (b(0) == 0xE2 && b(1) == 0x80) return (b(2) >= 0x8B && b(2) <= 0x8F) || (b(2) >= 0xAA && b(2) <= 0xAE);  // U+200B-200F, 202A-202E
    if (b(0) == 0xE2 && b(1) == 0x81) return (b(2) >= 0xA0 && b(2) <= 0xA4) || (b(2) >= 0xA6 && b(2) <= 0xA9);  // U+2060-2064, 2066-2069
    if (b(0) == 0xEF && b(1) == 0xBB && b(2) == 0xBF) return true;                                              // U+FEFF
    return b(0) == 0xE1 && b(1) == 0xA0 && b(2) == 0x8E;                                                         // U+180E
}

// Replaces forbidden characters etc.; the input is the ASCII-folded name.
std::string sanitizeStem(const std::string &folded) {
    std::string out;
    for (size_t i = 0; i < folded.size();) {
        const unsigned char c = static_cast<unsigned char>(folded[i]);
        const size_t n = utf8::charLen(folded, i);
        if (n == 0) {  // invalid byte
            out += '_';
            i++;
            continue;
        }
        if (n == 1) {
            if (c < 0x20 || c == 0x7f || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' ||
                c == '<' || c == '>' || c == '|')
                out += '_';
            else
                out += static_cast<char>(c);
        } else if (isInvisibleFormat(folded, i, n)) {
            // dropped
        } else if (n == 2 && c == 0xC2 && static_cast<unsigned char>(folded[i + 1]) < 0xA0) {
            out += '_';  // C1 control character U+0080..U+009F
        } else {
            out.append(folded, i, n);
        }
        i += n;
    }
    size_t a = 0;
    while (a < out.size() && (out[a] == '.' || out[a] == ' ')) out[a++] = '_';
    size_t b = out.size();
    while (b > a && (out[b - 1] == '.' || out[b - 1] == ' ')) out[--b] = '_';
    return out;
}

std::string hashedStem(const std::string &name, const std::string &stem) {
    const std::string suffix = "-" + profileNameHash(name);
    std::string cut = utf8::truncate(stem, kMaxSlugBytes - suffix.size());
    while (!cut.empty() && (cut.back() == '.' || cut.back() == ' ')) cut.back() = '_';
    return cut + suffix;
}

std::string baseStem(const std::string &name) {
    std::string s = sanitizeStem(utf8::lowerAscii(name));
    if (s.empty()) s = "_";
    if (isReservedWindowsName(s)) {
        const size_t dot = s.find('.');
        s.insert(dot == std::string::npos ? s.size() : dot, "_");
    }
    return s;
}

bool isProfileFileName(const std::string &n) {
    if (n.size() <= 5 || n.compare(n.size() - 5, 5, ".conf") != 0 || n[0] == '.') return false;
    for (char c : n)
        if (static_cast<unsigned char>(c) < 0x20 || c == '/' || c == '\\') return false;
    return true;
}

// Sorted *.conf names of dir; false if the directory cannot be listed (missing).
bool listProfileNames(const std::string &dir, std::vector<std::string> *names, bool *capped) {
    std::vector<std::string> all;
    if (!fs::listDir(dir, kMaxProfileDirEntries, &all, capped)) return false;
    for (const std::string &n : all)
        if (isProfileFileName(n)) names->push_back(n);
    std::sort(names->begin(), names->end());
    return true;
}

struct Cand {
    FileDiag d;
    Section sec;
    bool ok = false;
};

void failCand(Cand *c, const std::string &why) {
    c->ok = false;
    c->d.fatal = true;
    c->d.warnings.push_back(c->d.path + ": " + why + " - profile ignored");
}

// Number of [device:NAME] / [app:NAME] header lines of a text (the parser merges headers of the same name).
size_t countProfileHeaders(const std::string &text) {
    size_t n = 0, pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        size_t a = pos, b = nl;
        pos = nl + 1;
        while (a < b && (text[a] == ' ' || text[a] == '\t' || text[a] == '\r')) a++;
        if (a >= b || text[a] != '[') continue;
        const std::string head = utf8::lowerAscii(text.substr(a + 1, std::min<size_t>(b - a - 1, 7)));
        if (head.compare(0, 7, "device:") == 0 || head.compare(0, 4, "app:") == 0) n++;
    }
    return n;
}

void parseCand(const std::string &text, Section::Kind dirKind, Cand *c) {
    ProfileSet ps;
    parseProfilesText(text, c->d.path, &ps, &c->d.warnings);
    if (ps.sections.size() == 2 && countProfileHeaders(text) > 1)
        c->d.warnings.push_back(c->d.path + ": the section header is repeated (a profile file holds exactly one section) - the keys are merged");
    if (ps.sections.size() == 1) {
        failCand(c, std::string("no [") + (dirKind == Section::DEVICE ? "device" : "app") + ":NAME] section");
        return;
    }
    if (ps.sections.size() > 2) {
        failCand(c, "several sections (a profile file holds exactly one)");
        return;
    }
    c->d.name = ps.sections[1].pattern;
    if (!ps.sections[0].kv.empty()) {
        failCand(c, "contains [global] keys");
        return;
    }
    if (ps.sections[1].kind != dirKind) {
        failCand(c, std::string("[") + (ps.sections[1].kind == Section::DEVICE ? "device" : "app") + ":...] header in " +
                        dirNameOf(dirKind) + "/");
        return;
    }
    c->sec = ps.sections[1];
    c->ok = true;
}

// false when the directory exists but cannot be listed or opened (the caller keeps what it has: an unreadable
// directory must not look like "no profiles"). A missing directory is no error.
// *budgetLeft: bytes of profile files that may still be read in this load (kMaxProfileTotalBytes over devices/ and apps/).
bool readProfileDir(const std::string &baseDir, Section::Kind kind, std::vector<Cand> *cands,
                    std::vector<FileDiag> *dirDiags, size_t *budgetLeft) {
    const std::string dir = profileDirOf(baseDir, kind);
    std::vector<std::string> names;
    bool capped = false;
    if (!listProfileNames(dir, &names, &capped)) {
        if (!fs::pathExists(dir)) return true;
        FileDiag d;
        d.origin = FileDiag::DIRECTORY;
        d.kind = kind;
        d.path = dir;
        d.warnings.push_back(dir + ": cannot be read (not a readable directory) - the profile files are not loaded");
        d.fatal = true;
        dirDiags->push_back(d);
        return false;
    }
    auto dirDiag = [&](const std::string &msg, bool fatal) {
        FileDiag d;
        d.origin = FileDiag::DIRECTORY;
        d.kind = kind;
        d.path = dir;
        d.warnings.push_back(dir + ": " + msg);
        d.fatal = fatal;
        dirDiags->push_back(d);
    };
    if (capped) dirDiag("too many entries - only the first " + std::to_string(kMaxProfileDirEntries) + " were inspected", false);
    if (names.size() > kMaxProfileFiles) {
        dirDiag("too many profile files (max. " + std::to_string(kMaxProfileFiles) + ") - the rest is ignored", false);
        names.resize(kMaxProfileFiles);
    }
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    fs::DirHandle dh;
    std::string derr;
    if (!dh.open(dir, &derr)) {
        dirDiag(derr, true);
        return false;
    }
    for (const std::string &n : names) {
        Cand c;
        c.d.origin = FileDiag::PROFILE_FILE;
        c.d.kind = kind;
        c.d.path = dir + "/" + n;
        c.d.stamp = profileFileStamp(c.d.path, false);  // before reading: a change while reading shows up later
        // The size decides before anything is read. A file over the per-file limit is not read either (it is
        // reported below), so it costs nothing.
        fs::FileInfo fi;
        if (fs::lstatPath(c.d.path, &fi) && fi.isFile && !fi.isLink && fi.size >= 0 &&
            static_cast<uint64_t>(fi.size) <= kMaxProfileFileBytes && static_cast<uint64_t>(fi.size) > *budgetLeft) {
            failCand(&c, "skipped: the profile files together would exceed the budget of " + std::to_string(kMaxProfileTotalBytes) +
                             " bytes (16 MiB; " + std::to_string(*budgetLeft) + " bytes left)");
            cands->push_back(std::move(c));
            continue;
        }
        std::string text;
        const fs::ReadResult rr = dh.readFileBounded(n, kMaxProfileFileBytes, opt, &text);
        switch (rr.status) {
        case fs::ReadStatus::Ok:
            *budgetLeft -= std::min(*budgetLeft, text.size());
            parseCand(text, kind, &c);
            break;
        case fs::ReadStatus::NotFound:
        case fs::ReadStatus::OpenFailed:
            failCand(&c, rr.sysError.empty() ? "cannot be opened (a link is refused)" : rr.sysError);
            break;
        case fs::ReadStatus::NotRegular:
            failCand(&c, "not a regular file");
            break;
        case fs::ReadStatus::NotOwned:
            failCand(&c, "not owned by the current user");
            break;
        case fs::ReadStatus::TooLargeBySize:
        case fs::ReadStatus::TooLargeWhileReading:
            failCand(&c, "file too large (max. " + std::to_string(kMaxProfileFileBytes) + " bytes)");
            break;
        case fs::ReadStatus::ReadFailed:
            failCand(&c, "unreadable");
            break;
        }
        cands->push_back(std::move(c));
    }
    return true;
}

// The profile directories belong to <dir>/dofscroll.conf only. A legacy location (spnav-scroll.conf in a generic
// config directory) has none: foreign *.conf files there would act as profiles.
bool hasProfileDirs(const std::string &globalFile) {
    const size_t p = fs::lastSeparator(globalFile);
    return globalFile.substr(p == std::string::npos ? 0 : p + 1) == "dofscroll.conf";
}

// Removes leftovers of an interrupted atomic write ("<name>.conf.tmp.<16 hex>") older than a day.
void cleanStaleTemps(const std::string &dir) {
    std::vector<std::string> all;
    bool capped = false;
    if (!fs::listDir(dir, kMaxProfileDirEntries, &all, &capped)) return;
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (const std::string &n : all) {
        const size_t at = n.rfind(".conf.tmp.");
        if (at == std::string::npos || at == 0 || n.size() != at + 10 + 16) continue;
        if (n.find_first_not_of("0123456789abcdef", at + 10) != std::string::npos) continue;
        fs::FileInfo fi;
        const std::string p = dir + "/" + n;
        if (!fs::lstatPath(p, &fi) || !fi.isFile || fi.isLink) continue;
        if (now - fi.mtimeNs > 24LL * 3600 * 1'000'000'000LL) fs::removeFile(p);
    }
}

// ---- writer helpers ---------------------------------------------------------------------------

enum class PathState { Absent, File, Link, Other };

PathState classify(const std::string &path) {
    fs::FileInfo fi;
    if (!fs::lstatPath(path, &fi)) return PathState::Absent;
    if (fi.isLink) return PathState::Link;
    return fi.isFile ? PathState::File : PathState::Other;
}

bool pathInProfileDir(const std::string &configDir, Section::Kind kind, const std::string &path) {
    const std::string prefix = profileDirOf(configDir, kind) + "/";
    if (path.size() <= prefix.size() + 5 || path.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string name = path.substr(prefix.size());
    return isProfileFileName(name);
}

bool ownerMatches(const std::string &path, Section::Kind kind, const std::string &name) {
    fs::ReadOptions opt;
    opt.refuseLinks = true;
    opt.requireOwner = true;
    std::string text;
    if (!fs::readFileBounded(path, kMaxProfileFileBytes, opt, &text).ok()) return false;
    Cand c;
    c.d.path = path;
    parseCand(text, kind, &c);
    return c.ok && foldedKey(kind, c.sec.pattern) == foldedKey(kind, name);
}

WriteResult mk(WriteStatus st, const std::string &path, const std::string &err) {
    WriteResult r;
    r.status = st;
    r.path = path;
    r.error = err;
    return r;
}

WriteResult saveProfileImpl(const std::string &configDir, const Section &sec, const FileDiag *known,
                            const std::string &comment, bool overwriteSame) {
    if (sec.kind == Section::GLOBAL) return mk(WriteStatus::Invalid, "", "not a profile section");
    std::string body, err;
    if (!renderProfileSection(sec, &body, &err)) return mk(WriteStatus::Invalid, "", err);
    std::string head;
    if (!comment.empty()) {
        if (comment.size() > 4096) return mk(WriteStatus::Invalid, "", "comment too long");
        size_t pos = 0;
        while (pos < comment.size()) {
            size_t nl = comment.find('\n', pos);
            if (nl == std::string::npos) nl = comment.size();
            const std::string line = utf8::trim(comment.substr(pos, nl - pos));
            if (!line.empty() && line[0] != '#' && line[0] != ';')
                return mk(WriteStatus::Invalid, "", "comment lines must start with '#'");
            pos = nl + 1;
        }
        head = comment;
        if (head.back() != '\n') head += '\n';
    }

    const std::string dir = profileDirOf(configDir, sec.kind);
    const std::string p2 = dir + "/" + profileSlugHashed(sec.pattern) + ".conf";
    // A plain slug that Windows would resolve to something else (a device name, ...) is never used: hashed name.
    const std::string plain = profileSlug(sec.pattern) + ".conf";
    const std::string p1 = windowsPathHazard(plain) == PathHazard::None ? dir + "/" + plain : p2;
    cleanStaleTemps(dir);
    const bool haveKnown = known && !known->path.empty();

    std::string target;
    // Keep a file the profile already lives in under the hashed name (no churn when the colliding profile is gone).
    const bool knownIsHashed = haveKnown && known->path == p2 && p1 != p2;
    const std::string cands[2] = {knownIsHashed ? p2 : p1, knownIsHashed ? p1 : p2};
    for (int i = 0; i < 2 && target.empty(); i++) {
        const std::string &p = cands[i];
        if (i == 1 && p == cands[0]) break;
        switch (classify(p)) {
        case PathState::Absent:
            target = p;
            break;
        case PathState::Link:
            return mk(WriteStatus::Refused, p, p + ": is a symbolic link - not written");
        case PathState::Other:
            return mk(WriteStatus::Refused, p, p + ": is not a regular file - not written");
        case PathState::File:
            if (haveKnown && known->path == p) {
                if (!known->stamp.empty() && profileFileStamp(p, false) != known->stamp)
                    return mk(WriteStatus::Conflict, p, p + ": changed since it was read - not overwritten");
                target = p;
            } else if (ownerMatches(p, sec.kind, sec.pattern)) {
                if (!overwriteSame)
                    return mk(WriteStatus::Conflict, p, p + ": a file of this profile exists already - not overwritten");
                target = p;
            }  // else: another profile owns this name, try the hashed one
            break;
        }
    }
    if (target.empty()) return mk(WriteStatus::Refused, p2, "no free file name for profile '" + sec.pattern + "'");

    if (!fs::writeFileReplace(target, head + body, &err)) return mk(WriteStatus::Failed, target, err);
    WriteResult r = mk(WriteStatus::Ok, target, "");
    r.stamp = profileFileStamp(target, false);

    // Rename or hand-renamed file: the old file goes only after the new one is safely there.
    if (haveKnown && known->path != target) {
        const std::string &old = known->path;
        if (!pathInProfileDir(configDir, sec.kind, old)) {
            r.warning = old + ": not a profile file of this configuration - left alone";
        } else {
            switch (classify(old)) {
            case PathState::Absent:
                break;
            case PathState::Link:
            case PathState::Other:
                r.warning = old + ": not a regular file - left in place";
                break;
            case PathState::File:
                if (!known->stamp.empty() && profileFileStamp(old, false) != known->stamp)
                    r.warning = old + ": changed since it was read - left in place";
                else if (!fs::removeFile(old))
                    r.warning = old + ": could not be removed - left in place";
                break;
            }
        }
    }
    return r;
}

}  // namespace

// ---- slug -------------------------------------------------------------------------------------

std::string profileNameHash(const std::string &name) {
    uint32_t h = 2166136261u;
    for (char c : utf8::lowerAscii(name)) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    static const char *hex = "0123456789abcdef";
    std::string out(8, '0');
    for (int i = 7; i >= 0; i--) {
        out[static_cast<size_t>(i)] = hex[h & 0xF];
        h >>= 4;
    }
    return out;
}

std::string profileSlug(const std::string &name) {
    const std::string s = baseStem(name);
    if (s.size() <= kMaxSlugBytes) return s;
    return hashedStem(name, s);
}

std::string profileSlugHashed(const std::string &name) { return hashedStem(name, baseStem(name)); }

// ---- stamps -----------------------------------------------------------------------------------

std::string profileFileStamp(const std::string &path, bool follow) {
    fs::FileInfo fi;
    if (!(follow ? fs::statPath(path, &fi) : fs::lstatPath(path, &fi))) return "";
    return stampOf(fi);
}

std::string configDirOf(const std::string &globalFile) {
    const size_t p = fs::lastSeparator(globalFile);
    if (p == std::string::npos) return ".";
    if (p == 0) return "/";
    return globalFile.substr(0, p);
}

std::string profileTreeStampAt(const std::string &globalFile) {
    std::string s = "tree|" + profileFileStamp(globalFile, true);
    const std::string base = configDirOf(globalFile);
    if (!hasProfileDirs(globalFile)) return s;
    for (Section::Kind k : {Section::DEVICE, Section::APP}) {
        const std::string dir = profileDirOf(base, k);
        std::vector<std::string> names;
        bool capped = false;
        if (!listProfileNames(dir, &names, &capped)) continue;
        s += std::string("|") + dirNameOf(k) + "#" + std::to_string(names.size());
        // Only the files the loader reads (the first kMaxProfileFiles in sort order) are stat'ed: a directory with
        // thousands of entries does not cost thousands of stat calls per second.
        for (size_t i = 0; i < names.size() && i < kMaxProfileFiles; i++)
            s += "|" + names[i] + ":" + profileFileStamp(dir + "/" + names[i], false);
    }
    return s;
}

std::string profileTreeStamp(const std::string &configDir) { return profileTreeStampAt(configDir + "/dofscroll.conf"); }

bool StampDebounce::settled(const std::string &current) {
    if (current == applied_) {
        seen_.clear();
        return false;
    }
    if (current == seen_) return true;
    seen_ = current;
    return false;
}

// ---- loading ----------------------------------------------------------------------------------

bool loadLegacyProfileFile(const std::string &file, ProfileSet *out, std::vector<std::string> *warnings) {
    return loadProfiles(file, out, warnings);
}

bool loadProfileTree(const std::string &configDir, ProfileSet *out, std::vector<FileDiag> *diag) {
    return loadProfileTreeAt(configDir + "/dofscroll.conf", out, diag);
}

bool loadProfileTreeAt(const std::string &globalFile, ProfileSet *out, std::vector<FileDiag> *diag) {
    std::vector<FileDiag> dg;
    FileDiag gd;
    gd.origin = FileDiag::GLOBAL_FILE;
    gd.path = globalFile;
    gd.stamp = profileFileStamp(globalFile, true);  // before reading
    ProfileSet g;
    if (!loadProfiles(globalFile, &g, &gd.warnings)) {
        gd.fatal = true;
        if (diag) {
            diag->clear();
            diag->push_back(gd);
        }
        return false;
    }
    const std::string base = configDirOf(globalFile);

    std::vector<Cand> cands;
    std::vector<FileDiag> dirDiags;
    bool dirsOk = true;
    if (hasProfileDirs(globalFile)) {
        size_t budgetLeft = kMaxProfileTotalBytes;
        dirsOk = readProfileDir(base, Section::DEVICE, &cands, &dirDiags, &budgetLeft) && dirsOk;
        dirsOk = readProfileDir(base, Section::APP, &cands, &dirDiags, &budgetLeft) && dirsOk;
    }
    if (!dirsOk) {
        if (diag) *diag = std::move(dirDiags);
        return false;
    }

    // Every profile exists once: the first file in sort order wins.
    std::map<std::string, size_t> winner;
    for (size_t i = 0; i < cands.size(); i++) {
        Cand &c = cands[i];
        if (!c.ok) continue;
        const std::string key = foldedKey(c.d.kind, c.d.name);
        auto it = winner.find(key);
        if (it == winner.end()) {
            winner[key] = i;
        } else {
            failCand(&c, "the profile '" + c.d.name + "' exists already in " + cands[it->second].d.path);
        }
    }

    // Entries to place: legacy sections first (file order), then files (name order). The final order inside a
    // kind is (PRIORITY ascending, legacy before files, then this collection order); devices before apps.
    struct Ent {
        Section sec;
        size_t dgIndex;
        int rank;
    };
    std::vector<Ent> ents;
    dg.push_back(gd);
    dg.back().section = 0;
    for (size_t i = 1; i < g.sections.size(); i++) {
        const Section &ls = g.sections[i];
        FileDiag d;
        d.origin = FileDiag::LEGACY_SECTION;
        d.kind = ls.kind;
        d.name = ls.pattern;
        d.path = globalFile;
        auto it = winner.find(foldedKey(ls.kind, ls.pattern));
        if (it != winner.end()) {
            d.fatal = true;
            d.warnings.push_back(globalFile + ": section [" + (ls.kind == Section::DEVICE ? "device:" : "app:") + ls.pattern +
                                 "] ignored, the file " + cands[it->second].d.path + " is used instead");
        } else {
            ents.push_back({ls, dg.size(), 0});
        }
        dg.push_back(std::move(d));
    }
    for (Cand &c : cands) {
        if (c.ok) {
            if (ents.size() + 1 >= kMaxSections)
                failCand(&c, "too many profiles (max. " + std::to_string(kMaxSections - 1) + ")");
            else
                ents.push_back({c.sec, dg.size(), 1});
        }
        dg.push_back(std::move(c.d));
    }
    std::stable_sort(ents.begin(), ents.end(), [](const Ent &x, const Ent &y) {
        if (x.sec.kind != y.sec.kind) return x.sec.kind < y.sec.kind;  // DEVICE before APP
        const int px = sectionPriority(x.sec), py = sectionPriority(y.sec);
        if (px != py) return px < py;
        return x.rank < y.rank;
    });
    ProfileSet res;
    res.sections[0] = g.sections[0];
    for (const Ent &e : ents) {
        const size_t idx = res.findOrAdd(e.sec.kind, e.sec.pattern);
        if (idx == ProfileSet::npos) {
            dg[e.dgIndex].fatal = true;
            dg[e.dgIndex].warnings.push_back("invalid profile name");
            continue;
        }
        res.sections[idx].kv = e.sec.kv;
        dg[e.dgIndex].section = idx;
    }
    for (FileDiag &d : dirDiags) dg.push_back(std::move(d));

    if (out) *out = std::move(res);
    if (diag) *diag = std::move(dg);
    return true;
}

// ---- writing ----------------------------------------------------------------------------------

void assignMigrationPriorities(ProfileSet *ps) {
    for (Section::Kind k : {Section::DEVICE, Section::APP}) {
        size_t count = 0;
        for (const Section &s : ps->sections)
            if (s.kind == k) count++;
        if (count < 2) continue;
        // 10, 20, ... but never above kMaxPriority: the step shrinks for more than 100 profiles of one kind.
        const int step = std::max(1, std::min(10, static_cast<int>(kMaxPriority / static_cast<long>(count))));
        int next = step;
        for (Section &s : ps->sections) {
            if (s.kind != k) continue;
            s.kv.erase(std::remove_if(s.kv.begin(), s.kv.end(), [](const KV &p) { return p.first == "PRIORITY"; }), s.kv.end());
            s.kv.insert(s.kv.begin(), KV("PRIORITY", std::to_string(next)));
            next += step;
        }
    }
}

WriteResult saveProfileFile(const std::string &configDir, const Section &sec, const FileDiag *known,
                            const std::string &leadingComment) {
    return saveProfileImpl(configDir, sec, known, leadingComment, false);
}

WriteResult deleteProfileFile(const std::string &configDir, Section::Kind kind, const std::string &path,
                              const std::string &expectedStamp) {
    if (kind == Section::GLOBAL || !pathInProfileDir(configDir, kind, path))
        return mk(WriteStatus::Refused, path, path + ": not a profile file of this configuration");
    switch (classify(path)) {
    case PathState::Absent:
        return mk(WriteStatus::Ok, path, "");
    case PathState::Link:
        return mk(WriteStatus::Refused, path, path + ": is a symbolic link - not removed");
    case PathState::Other:
        return mk(WriteStatus::Refused, path, path + ": is not a regular file - not removed");
    case PathState::File:
        break;
    }
    if (!expectedStamp.empty() && profileFileStamp(path, false) != expectedStamp)
        return mk(WriteStatus::Conflict, path, path + ": changed since it was read - not removed");
    if (!fs::removeFile(path)) return mk(WriteStatus::Failed, path, path + ": could not be removed");
    return mk(WriteStatus::Ok, path, "");
}

namespace {
// The checks of every write of dofscroll.conf: a regular file (or a link to one), unchanged since expectedStamp.
WriteResult checkGlobalTarget(const std::string &path, const std::string &expectedStamp) {
    fs::FileInfo li, si;
    if (fs::lstatPath(path, &li)) {
        if (fs::statPath(path, &si) && !si.isFile)
            return mk(WriteStatus::Refused, path, path + ": is not a regular file - not written");
        if (!li.isLink && !li.isFile) return mk(WriteStatus::Refused, path, path + ": is not a regular file - not written");
    }
    if (!expectedStamp.empty() && profileFileStamp(path, true) != expectedStamp)
        return mk(WriteStatus::Conflict, path, path + ": changed since it was read - not overwritten");
    return mk(WriteStatus::Ok, path, "");
}
}  // namespace

WriteResult saveGlobalFile(const std::string &configDir, const ProfileSet &globalAndLegacy,
                           const std::string &expectedStamp) {
    const std::string path = configDir + "/dofscroll.conf";
    WriteResult c = checkGlobalTarget(path, expectedStamp);
    if (!c.ok()) return c;
    std::string err;
    if (!saveProfiles(path, globalAndLegacy, &err)) return mk(WriteStatus::Failed, path, err);
    WriteResult r = mk(WriteStatus::Ok, path, "");
    r.stamp = profileFileStamp(path, true);
    return r;
}

WriteResult saveGlobalText(const std::string &configDir, const std::string &text, const std::string &expectedStamp) {
    const std::string path = configDir + "/dofscroll.conf";
    WriteResult c = checkGlobalTarget(path, expectedStamp);
    if (!c.ok()) return c;
    std::string err;
    if (!fs::writeFileReplace(path, text, &err)) return mk(WriteStatus::Failed, path, path + ": " + err);
    WriteResult r = mk(WriteStatus::Ok, path, "");
    r.stamp = profileFileStamp(path, true);
    return r;
}

ProfileSet legacyPart(const ProfileSet &ps, const std::vector<FileDiag> &diags) {
    ProfileSet r;
    r.sections[0] = ps.sections[0];
    for (const FileDiag &d : diags) {
        if (d.origin != FileDiag::LEGACY_SECTION || d.fatal || d.section == ProfileSet::npos || d.section >= ps.sections.size())
            continue;
        const Section &s = ps.sections[d.section];
        const size_t idx = r.findOrAdd(s.kind, s.pattern);
        if (idx != ProfileSet::npos) r.sections[idx].kv = s.kv;
    }
    return r;
}

bool writeProfileSetSplit(const std::string &configDir, const ProfileSet &ps, std::vector<FileDiag> *written,
                          std::string *err) {
    if (written) written->clear();
    for (size_t i = 1; i < ps.sections.size(); i++) {
        const WriteResult r = saveProfileImpl(configDir, ps.sections[i], nullptr, std::string(), true);
        if (!r.ok()) {
            if (err) *err = r.error;
            return false;
        }
        if (written) {
            FileDiag d;
            d.origin = FileDiag::PROFILE_FILE;
            d.kind = ps.sections[i].kind;
            d.name = ps.sections[i].pattern;
            d.path = r.path;
            d.stamp = r.stamp;
            d.section = i;
            written->push_back(std::move(d));
        }
    }
    ProfileSet g;
    g.sections[0] = ps.sections[0];
    const WriteResult r = saveGlobalFile(configDir, g, std::string());
    if (!r.ok()) {
        if (err) *err = r.error;
        return false;
    }
    return true;
}

}  // namespace spn
