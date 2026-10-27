// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Migration to the split layout, see profile_migrate.hpp.
#include "profile_migrate.hpp"

#include <algorithm>
#include <map>
#include <set>

#include "config.hpp"
#include "platform/fs.hpp"
#include "profile_files.hpp"
#include "utf8.hpp"

namespace spn {
namespace {

constexpr size_t kMaxWarningsKept = 50;
constexpr size_t kMaxProbes = 48;

// A profile as the loader resolves it: (kind, folded name).
std::string keyOf(Section::Kind k, const std::string &name) {
    return std::string(k == Section::DEVICE ? "d:" : "a:") + utf8::lowerAscii(name);
}

// Same rule as the parser (config.cpp headerClose): the first ']' followed only by white space or a comment.
size_t headerClose(const std::string &line) {
    size_t first = std::string::npos;
    for (size_t i = 1; i < line.size(); i++) {
        if (line[i] != ']') continue;
        if (first == std::string::npos) first = i;
        size_t j = i + 1;
        while (j < line.size() && utf8::isSpace(line[j])) j++;
        if (j == line.size() || line[j] == '#' || line[j] == ';') return i;
    }
    return first;
}

std::vector<std::string> splitLines(const std::string &data) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) nl = data.size();
        lines.push_back(data.substr(pos, nl - pos));
        pos = nl + 1;
    }
    if (!lines.empty() && lines[0].compare(0, 3, "\xEF\xBB\xBF") == 0) lines[0].erase(0, 3);
    for (std::string &l : lines)
        if (!l.empty() && l.back() == '\r') l.pop_back();
    return lines;
}

// The old file split into the text that stays in dofscroll.conf and the leading comment of every profile.
struct TextSplit {
    std::string globalText;                        // original text without the profile sections
    std::map<std::string, std::string> comments;   // keyOf -> comment lines directly above the header
    bool clean = true;                             // false: a profile has two blocks, the text cannot be cut safely
};

TextSplit splitText(const std::string &data, const ProfileSet &parsed) {
    TextSplit r;
    const std::vector<std::string> lines = splitLines(data);
    struct Hdr {
        size_t line = 0, commentStart = 0;
        bool profile = false;
        Section::Kind kind = Section::GLOBAL;
        std::string name;
    };
    std::vector<Hdr> hs;
    size_t cstart = std::string::npos;  // first line of the comment run directly above the current line
    for (size_t i = 0; i < lines.size(); i++) {
        const std::string t = utf8::trim(lines[i]);
        if (!t.empty() && (t[0] == '#' || t[0] == ';')) {
            if (cstart == std::string::npos) cstart = i;
            continue;
        }
        if (!t.empty() && t[0] == '[') {
            Hdr h;
            h.line = i;
            h.commentStart = cstart == std::string::npos ? i : cstart;
            const size_t close = headerClose(t);
            if (close != std::string::npos) {
                const std::string name = utf8::trim(t.substr(1, close - 1));
                const std::string lname = utf8::lowerAscii(name);
                if (lname.compare(0, 7, "device:") == 0 || lname.compare(0, 4, "app:") == 0) {
                    h.kind = lname[0] == 'd' ? Section::DEVICE : Section::APP;
                    h.name = utf8::trim(name.substr(name.find(':') + 1));
                    // The parser must have accepted this header (valid pattern, not over the section limit).
                    h.profile = !h.name.empty() && parsed.find(h.kind, h.name) != ProfileSet::npos;
                }
            }
            hs.push_back(h);
        }
        cstart = std::string::npos;
    }
    // Lines [from, to) belong to the block of header k; the comment run above the next header belongs to that one.
    std::vector<bool> moved(lines.size(), false);
    std::set<std::string> seen;
    for (size_t k = 0; k < hs.size(); k++) {
        if (!hs[k].profile) continue;
        const size_t to = k + 1 < hs.size() ? hs[k + 1].commentStart : lines.size();
        const std::string key = keyOf(hs[k].kind, hs[k].name);
        if (!seen.insert(key).second) {
            r.clean = false;  // second block of the same profile: its lines are merged by the parser
            continue;
        }
        std::string comment;
        for (size_t i = hs[k].commentStart; i < hs[k].line; i++) {
            std::string c = utf8::trim(lines[i]);
            if (!c.empty() && c[0] == ';') c[0] = '#';
            comment += c + "\n";
        }
        while (comment.size() > 4000) comment.erase(0, comment.find('\n') + 1);  // writer limit 4096: keep the last lines
        if (!comment.empty()) r.comments[key] = comment;
        for (size_t i = hs[k].commentStart; i < to; i++) moved[i] = true;
    }
    // A second block of a profile is merged into the first by the parser: moving only the first block would
    // leave keys behind, so then the caller falls back to a rendered [global].
    std::string text;
    size_t blank = 0;
    for (size_t i = 0; i < lines.size(); i++) {
        if (moved[i]) continue;
        if (utf8::trim(lines[i]).empty()) {
            blank++;
            continue;
        }
        if (!text.empty() && blank > 0) text += "\n";
        blank = 0;
        text += lines[i] + "\n";
    }
    r.globalText = text;
    return r;
}

struct Step {
    const MigrateOptions &opt;
    bool crashed = false;
    // true: carry on. Crash: sets `crashed`, caller returns without clean-up. Fail: returns false, caller cleans up.
    bool go(const char *name, std::string *err) {
        if (!opt.fault) return true;
        const MigrateFault f = opt.fault(name);
        if (f == MigrateFault::None) return true;
        if (f == MigrateFault::Crash) crashed = true;
        *err = std::string("simulated failure at ") + name;
        return false;
    }
};

bool sameKv(const std::vector<KV> &a, const std::vector<KV> &b) { return a == b; }

std::vector<KV> withoutPriority(std::vector<KV> kv) {
    kv.erase(std::remove_if(kv.begin(), kv.end(), [](const KV &p) { return p.first == "PRIORITY"; }), kv.end());
    return kv;
}

// Probe names for the resolve comparison: "", every pattern, and pairs of patterns.
std::vector<std::string> probesFor(const ProfileSet &ps, Section::Kind kind) {
    std::vector<std::string> pats, out{std::string()};
    for (const Section &s : ps.sections)
        if (s.kind == kind) pats.push_back(s.pattern);
    for (const std::string &p : pats)
        if (out.size() < kMaxProbes) out.push_back(p);
    for (size_t i = 0; i < pats.size() && out.size() < kMaxProbes; i++)
        for (size_t j = i + 1; j < pats.size() && out.size() < kMaxProbes; j++) out.push_back(pats[i] + " " + pats[j]);
    return out;
}

bool sameResolve(const ProfileSet &a, const ProfileSet &b) {
    const std::vector<std::string> dev = probesFor(a, Section::DEVICE), app = probesFor(a, Section::APP);
    for (const std::string &d : dev)
        for (const std::string &p : app)
            if (allKeys(a.resolve(d, p)) != allKeys(b.resolve(d, p))) return false;
    return true;
}

// Compares two loaded trees section by section (same order, kind, name, keys).
bool sameTree(const ProfileSet &a, const ProfileSet &b) {
    if (a.sections.size() != b.sections.size()) return false;
    for (size_t i = 0; i < a.sections.size(); i++) {
        const Section &x = a.sections[i], &y = b.sections[i];
        if (x.kind != y.kind || utf8::lowerAscii(x.pattern) != utf8::lowerAscii(y.pattern) || !sameKv(x.kv, y.kv)) return false;
    }
    return true;
}

void removeWritten(const std::string &configDir, const std::vector<FileDiag> &written, const std::vector<bool> &createdDir) {
    for (const FileDiag &d : written) deleteProfileFile(configDir, d.kind, d.path, d.stamp);
    if (createdDir[0]) fs::removeDir(configDir + "/devices");
    if (createdDir[1]) fs::removeDir(configDir + "/apps");
}

}  // namespace

MigrateResult migrateToSplit(const std::string &configDir, const MigrateOptions &opt) {
    MigrateResult res;
    const std::string global = configDir + "/dofscroll.conf";
    auto fail = [&](const std::string &msg) {
        res.status = MigrateStatus::Failed;
        res.error = msg;
        return res;
    };

    // 0. preconditions
    fs::FileInfo li;
    if (!fs::lstatPath(global, &li)) return res;
    if (li.isLink) {
        res.status = MigrateStatus::SkippedSymlink;
        res.message = global + " is a symbolic link: the profiles stay in this single file";
        return res;
    }
    if (!li.isFile) return fail(global + ": not a regular file - not migrated");
    std::string data;
    const fs::ReadResult rr = fs::readFileBounded(global, kMaxConfigBytes, fs::ReadOptions(), &data);
    if (!rr.ok()) {
        return fail(global + ": " + (rr.status == fs::ReadStatus::TooLargeBySize || rr.status == fs::ReadStatus::TooLargeWhileReading
                                         ? "file too large (max. " + std::to_string(kMaxConfigBytes) + " bytes)"
                                         : rr.sysError.empty() ? std::string("cannot be read") : rr.sysError) +
                    " - not migrated");
    }
    const std::string stamp0 = profileFileStamp(global, true);
    ProfileSet legacy;
    std::vector<std::string> parseWarnings;
    parseProfilesText(data, global, &legacy, &parseWarnings);
    if (legacy.sections.size() <= 1) return res;  // nothing to split

    ProfileSet tree0;
    std::vector<FileDiag> diag0;
    if (!loadProfileTree(configDir, &tree0, &diag0)) return fail(global + ": cannot be read as a configuration - not migrated");
    std::set<std::string> filed;  // profiles that already have a (valid) file
    for (const FileDiag &d : diag0)
        if (d.origin == FileDiag::PROFILE_FILE && !d.fatal) filed.insert(keyOf(d.kind, d.name));

    for (size_t i = 0; i < parseWarnings.size() && i < kMaxWarningsKept; i++) res.warnings.push_back(parseWarnings[i]);

    // The profiles in resolution order: kind, PRIORITY ascending, file order (what loadProfileTree does for a
    // pure single file); then PRIORITY 10, 20, ... per kind.
    std::vector<Section> order(legacy.sections.begin() + 1, legacy.sections.end());
    std::stable_sort(order.begin(), order.end(), [](const Section &x, const Section &y) {
        if (x.kind != y.kind) return x.kind < y.kind;
        return sectionPriority(x) < sectionPriority(y);
    });
    ProfileSet ref;  // the original, in resolution order
    ref.sections[0] = legacy.sections[0];
    for (const Section &s : order) {
        const size_t i = ref.findOrAdd(s.kind, s.pattern);
        if (i != ProfileSet::npos) ref.sections[i].kv = s.kv;
    }
    ProfileSet prio = ref;
    assignMigrationPriorities(&prio);

    const TextSplit split = splitText(data, legacy);

    Step step{opt};
    std::string err;

    // 1. backup
    if (opt.backup) {
        if (!step.go("backup", &err)) {
            if (step.crashed) return fail(err);
            return fail(err);
        }
        const std::string bdir = configDir + "/backup";
        if (!fs::mkdirsPrivate(bdir, &err)) return fail(bdir + ": cannot be created (" + err + ") - not migrated");
        std::string chosen;
        for (int n = 0; n < 100 && chosen.empty(); n++) {
            const std::string p = bdir + "/dofscroll.conf.pre-split" + (n == 0 ? std::string() : "." + std::to_string(n));
            if (!fs::pathExists(p)) {
                if (!fs::createFileExclusive(p, data)) return fail(p + ": cannot be written - not migrated");
                chosen = p;
                break;
            }
            std::string have;
            fs::ReadOptions ro;
            ro.refuseLinks = true;
            if (fs::readFileBounded(p, kMaxConfigBytes, ro, &have).ok() && have == data) chosen = p;  // same bytes: reuse
        }
        if (chosen.empty()) return fail(bdir + ": too many backups - not migrated");
        std::string back;
        fs::ReadOptions ro;
        ro.refuseLinks = true;
        if (!fs::readFileBounded(chosen, kMaxConfigBytes, ro, &back).ok() || back != data)
            return fail(chosen + ": the backup does not match the original - not migrated");
        res.backupPath = chosen;
    }

    // 2. one file per profile that has none yet
    const std::vector<bool> createdDir = {!fs::pathExists(configDir + "/devices"), !fs::pathExists(configDir + "/apps")};
    std::vector<FileDiag> written;
    auto abortWith = [&](const std::string &why) {
        removeWritten(configDir, written, createdDir);
        res.filesWritten.clear();
        res.profilesMoved = 0;
        return fail(why);
    };
    for (size_t i = 1; i < prio.sections.size(); i++) {
        const Section &s = prio.sections[i];
        if (filed.count(keyOf(s.kind, s.pattern))) {
            res.profilesAlreadyFiled++;
            continue;
        }
        if (!step.go("profile-file", &err)) return step.crashed ? fail(err) : abortWith(err);
        const auto c = split.comments.find(keyOf(s.kind, s.pattern));
        const WriteResult w = saveProfileFile(configDir, s, nullptr, c == split.comments.end() ? std::string() : c->second);
        if (!w.ok()) return abortWith(w.error.empty() ? "a profile file could not be written" : w.error);
        FileDiag d;
        d.kind = s.kind;
        d.name = s.pattern;
        d.path = w.path;
        d.stamp = w.stamp;
        written.push_back(d);
        res.filesWritten.push_back(w.path);
    }
    res.profilesMoved = written.size();

    // 3. read back and compare
    if (!step.go("readback", &err)) return step.crashed ? fail(err) : abortWith(err);
    ProfileSet tree1;
    std::vector<FileDiag> diag1;
    if (!loadProfileTree(configDir, &tree1, &diag1)) return abortWith("read-back failed: dofscroll.conf cannot be read");
    {
        std::set<std::string> mine;
        for (const FileDiag &d : written) mine.insert(d.path);
        for (const FileDiag &d : diag1)
            if (d.origin == FileDiag::PROFILE_FILE && mine.count(d.path) && (d.fatal || !d.warnings.empty()))
                return abortWith("read-back failed: " + (d.warnings.empty() ? d.path : d.warnings.front()));
    }
    if (!sameKv(tree1.sections[0].kv, legacy.sections[0].kv)) return abortWith("read-back failed: [global] differs");
    std::set<std::string> expectedKeys(filed);
    for (size_t i = 1; i < ref.sections.size(); i++) {
        const Section &o = ref.sections[i], &p = prio.sections[i];
        const std::string k = keyOf(o.kind, o.pattern);
        expectedKeys.insert(k);
        const size_t at = tree1.find(o.kind, o.pattern);
        if (at == ProfileSet::npos) return abortWith("read-back failed: profile '" + o.pattern + "' is missing");
        if (filed.count(k)) continue;  // the existing file wins, it is not ours to compare
        if (!sameKv(tree1.sections[at].kv, p.kv) || !sameKv(withoutPriority(tree1.sections[at].kv), withoutPriority(o.kv)))
            return abortWith("read-back failed: profile '" + o.pattern + "' differs");
    }
    if (tree1.sections.size() != expectedKeys.size() + 1) return abortWith("read-back failed: unexpected profile count");
    if (filed.empty() && !sameResolve(ref, tree1)) return abortWith("read-back failed: the profiles resolve differently");

    // 4. replace dofscroll.conf by its [global] part
    if (!step.go("replace-global", &err)) return step.crashed ? fail(err) : abortWith(err);
    if (profileFileStamp(global, true) != stamp0) return abortWith(global + ": changed during the migration - not migrated");
    ProfileSet globalOnly;
    globalOnly.sections[0] = legacy.sections[0];
    bool keepText = split.clean;
    if (keepText) {
        ProfileSet chk;
        parseProfilesText(split.globalText, global, &chk, nullptr);
        keepText = chk.sections.size() == 1 && sameKv(chk.sections[0].kv, legacy.sections[0].kv);
    }
    if (keepText) {
        std::string text = split.globalText;
        if (text.empty()) text = "[global]\n";
        if (!fs::writeFileReplace(global, text, &err)) return abortWith(global + ": " + err);
    } else {
        const WriteResult g = saveGlobalFile(configDir, globalOnly, stamp0);
        if (!g.ok()) return abortWith(g.error);
        res.warnings.push_back(global + ": comments and unknown sections were not kept, see the backup");
    }

    // Final check; on a difference the old file comes back.
    auto restore = [&](const std::string &why) {
        std::string e2;
        fs::writeFileReplace(global, data, &e2);
        return abortWith(why);
    };
    if (!step.go("final-check", &err)) return step.crashed ? fail(err) : restore(err);
    ProfileSet tree2;
    std::vector<FileDiag> diag2;
    if (!loadProfileTree(configDir, &tree2, &diag2) || !sameTree(tree1, tree2))
        return restore("final check failed: the new layout does not read back identically");

    res.status = MigrateStatus::Migrated;
    res.message = "Your profiles are now one file each" + (res.backupPath.empty() ? std::string() : ", backup in " + res.backupPath);
    return res;
}

}  // namespace spn
