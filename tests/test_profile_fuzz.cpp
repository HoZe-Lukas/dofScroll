// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Fuzz driver for the profile loader (src/config.cpp, src/profile_files.cpp) and the migration to the split
// layout (src/profile_migrate.cpp). It feeds random and mutated configuration trees (dofscroll.conf, devices/,
// apps/) to loadProfileTree, loadProfileTreeAt, loadLegacyProfileFile, parseProfilesText, the stamps, the
// writers and migrateToSplit. The checks are the invariants the headers promise; the real aim is a clean run
// under -fsanitize=address,undefined (issue #243):
//
//   cmake -S . -B build-asan -DDOFSCROLL_SANITIZE=ON && cmake --build build-asan --target test_profile_fuzz
//   build-asan/tests/test_profile_fuzz --iterations 20000 --seed 1       (a longer run, by hand or in CI)
//
// Without arguments it runs a short, deterministic pass (ctest, also in the sanitizer job). Options:
//   --iterations N   number of generated trees (default 40, or $DOFSCROLL_FUZZ_ITERATIONS)
//   --seed S         base seed (default 1, or $DOFSCROLL_FUZZ_SEED); iteration K always uses the same tree
//   --only K         run only iteration K (replay of a crash: the failing K is printed before it runs when
//                    --verbose is given, and by every failed check)
//   --corpus DIR     seed files: every *.conf below DIR (default examples/ and tests/data/)
//   --verbose        print the iteration number before each run
// Host independent: temp directories only.
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "config.hpp"
#include "utf8.hpp"
#include "profile_files.hpp"
#include "profile_migrate.hpp"
#include "testutil.hpp"

using namespace spn;
namespace stdfs = std::filesystem;

namespace {

long g_iter = 0;
long g_loadedSections = 0, g_migrated = 0, g_migrateFailed = 0;  // statistics: proof that the inputs reach the code
uint64_t g_seed = 1;

#define CHECK(cond)                                                                                         \
    do {                                                                                                    \
        if (!(cond)) {                                                                                      \
            std::fprintf(stderr, "test_profile_fuzz: check failed: %s (%s:%d), seed %llu iteration %ld\n",  \
                         #cond, __FILE__, __LINE__, static_cast<unsigned long long>(g_seed), g_iter);       \
            std::abort();                                                                                   \
        }                                                                                                   \
    } while (0)

// xorshift64*: small, deterministic on every platform.
struct Rng {
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { if (!s) s = 1; next(); next(); }
    uint64_t next() {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    size_t below(size_t n) { return n ? static_cast<size_t>(next() % n) : 0; }
    bool chance(unsigned percent) { return below(100) < percent; }
};

// Builds a path from a (possibly fuzzed) name. On Windows a path is wide characters: a name that is not valid
// UTF-8 cannot be converted and std::filesystem throws (MinGW libstdc++: "Illegal byte sequence"), so there
// such a name means "not creatable here" and the caller skips the file. On Linux every byte string is a name.
bool toPath(const std::string &name, stdfs::path *out) {
    try {
#ifdef _WIN32
        if (!spn::utf8::isValid(name)) return false;
        *out = stdfs::u8path(name);
#else
        *out = stdfs::path(name);
#endif
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

std::string readAll(const stdfs::path &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

void put(const std::string &p, const std::string &c) {
    stdfs::path path;
    if (!toPath(p, &path)) return;  // name not creatable here: skip, never abort
    std::error_code ec;
    stdfs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    f << c;
}

// Structural tokens of the format and values at the edges of the parsers.
const char *const kDict[] = {
    "[global]\n", "[device:", "[app:", "[device:*]\n", "[app:fire fox]\n", "]\n", "]", "[", "\n", "\r\n", "\r",
    " = ", "=", "#", ";", "\\", "\"", "'", "\t", "\xEF\xBB\xBF", "\xFF\xFE", "\xC3\x28", "\xE2\x82", "\xF0\x9F\x98\x80",
    "\xC0\xAF", "\xED\xA0\x80", "..", "../", "/", "CON", "NUL", "aux.txt", "PRIORITY = ", "PRIORITY = 99999999999999999999\n",
    "PRIORITY = -1000\n", "PRIORITY = nan\n", "ZOOM_SENS = ", "ZOOM_SENS = 1e999\n", "ZOOM_SENS = -0\n", "MODE = ",
    "BUTTON_1 = ", "BUTTON_32 = ", "BUTTON_33 = ", "BUTTON_0 = ", "BUTTON_99999999999 = ", "V_ACTION = ", "LIFT_",
    "CURVE = ", "MIX = ", "step:", "key:", "mouse:", "nav", "0:0, 1:1, 2:4", "1e308", "-1e308", "nan", "inf",
    "4294967296", "-2147483649", "0x7fffffff", "9223372036854775808", "NaN", "true", "false", "on", "off",
    "ZOOM_LATERAL = ", "TICK_HZ = 0\n", "TICK_HZ = 100000\n",
};

std::string randomBytes(Rng &r, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; i++) s += static_cast<char>(r.below(256));
    return s;
}

// A well-formed profile section that the generator builds from scratch (not only from the corpus).
std::string randomSection(Rng &r, const std::vector<std::string> &words) {
    std::string s;
    const size_t kind = r.below(3);
    s += kind == 0 ? "[device:" : "[app:";
    const size_t nameLen = r.below(4) == 0 ? r.below(6000) : 1 + r.below(24);
    for (size_t i = 0; i < nameLen; i++) {
        const unsigned c = static_cast<unsigned>(r.below(100));
        s += c < 70 ? static_cast<char>('a' + r.below(26)) : c < 80 ? ' ' : c < 90 ? static_cast<char>(r.below(256)) : "./:*?<>|"[r.below(8)];
    }
    s += "]\n";
    const size_t keys = r.below(40);
    for (size_t i = 0; i < keys; i++) {
        s += words.empty() ? std::string("ZOOM_SENS") : words[r.below(words.size())];
        s += " = ";
        s += r.chance(60) ? std::to_string(static_cast<long>(r.below(2000)) - 1000) : words.empty() ? std::string("x") : words[r.below(words.size())];
        s += "\n";
    }
    return s;
}

std::string mutate(Rng &r, std::string s, const std::vector<std::string> &corpus, const std::vector<std::string> &words) {
    const size_t rounds = 1 + r.below(6);
    for (size_t k = 0; k < rounds; k++) {
        switch (r.below(10)) {
        case 0:  // flip one bit
            if (!s.empty()) s[r.below(s.size())] ^= static_cast<char>(1u << r.below(8));
            break;
        case 1:  // overwrite a byte with an interesting one
            if (!s.empty()) {
                static const char kBytes[] = {'\0', '\n', '\r', '[', ']', '#', ';', '=', ' ', '\t', '\\', '"', '\xFF', '\x80', '\x7F'};
                s[r.below(s.size())] = kBytes[r.below(sizeof kBytes)];
            }
            break;
        case 2:  // insert a dictionary token
            s.insert(r.below(s.size() + 1), kDict[r.below(sizeof kDict / sizeof *kDict)]);
            break;
        case 3:  // delete a range
            if (!s.empty()) {
                const size_t a = r.below(s.size());
                s.erase(a, r.below(64));
            }
            break;
        case 4:  // duplicate a range
            if (!s.empty()) {
                const size_t a = r.below(s.size());
                const std::string part = s.substr(a, r.below(200));
                s.insert(r.below(s.size() + 1), part);
            }
            break;
        case 5:  // truncate
            s.resize(r.below(s.size() + 1));
            break;
        case 6:  // splice in a piece of another corpus file
            if (!corpus.empty()) {
                const std::string &o = corpus[r.below(corpus.size())];
                if (!o.empty()) {
                    const size_t a = r.below(o.size());
                    s.insert(r.below(s.size() + 1), o.substr(a, r.below(400)));
                }
            }
            break;
        case 7:  // a new well-formed section
            s += "\n" + randomSection(r, words);
            break;
        case 8:  // random bytes
            s.insert(r.below(s.size() + 1), randomBytes(r, 1 + r.below(32)));
            break;
        default:  // a very long line, or many lines (limits: kMaxLineBytes, kMaxSections, kMaxKeysPerSection)
            if (r.chance(50))
                s += "\nZOOM_SENS = " + std::string(8000 + r.below(500), '9') + "\n";
            else
                for (size_t i = 0, n = 200 + r.below(400); i < n; i++) s += "[app:p" + std::to_string(i) + "]\nPRIORITY = " + std::to_string(i % 7) + "\n";
            break;
        }
        if (s.size() > (1u << 20) + 4096) s.resize(1u << 20);  // around kMaxConfigBytes, never far beyond
    }
    return s;
}

std::string pickText(Rng &r, const std::vector<std::string> &corpus, const std::vector<std::string> &words, bool section) {
    std::string base;
    if (!corpus.empty() && r.chance(70)) base = corpus[r.below(corpus.size())];
    else base = section ? randomSection(r, words) : "[global]\n" + randomSection(r, words) + randomSection(r, words);
    return r.chance(15) ? base : mutate(r, base, corpus, words);
}

// One random tree under `dir`: dofscroll.conf plus devices/ and apps/ with a few (possibly odd) entries.
void buildTree(Rng &r, const std::string &dir, const std::vector<std::string> &corpus, const std::vector<std::string> &words) {
    {
        stdfs::path d;
        if (toPath(dir, &d)) stdfs::create_directories(d);
    }
    if (!r.chance(8)) put(dir + "/dofscroll.conf", pickText(r, corpus, words, false));
    for (const char *sub : {"devices", "apps"}) {
        if (r.chance(25)) continue;
        const size_t n = r.below(r.chance(5) ? 300 : 6);  // sometimes over kMaxProfileFiles
        for (size_t i = 0; i < n; i++) {
            std::string name = r.chance(70) ? "p" + std::to_string(i) : randomBytes(r, 1 + r.below(12));
            for (char &c : name)
                if (c == '/' || c == '\0' || c == '\\') c = '_';
            if (r.chance(5)) name += "-" + std::to_string(r.below(9));
            const std::string file = dir + "/" + sub + "/" + name + (r.chance(90) ? ".conf" : ".txt");
            std::error_code ec;
            stdfs::path filePath;
            const bool creatable = toPath(file, &filePath);
            if (r.chance(4)) {
                if (creatable) stdfs::create_directories(filePath, ec);  // a directory with a profile file name
            } else if (r.chance(4)) {
                if (creatable) {
                    stdfs::create_directories(filePath.parent_path(), ec);
                    stdfs::create_symlink(r.chance(50) ? "../dofscroll.conf" : "nonexistent-target", filePath, ec);
                }
            } else {
                std::string t = pickText(r, corpus, words, true);
                if (r.chance(20)) t = std::string(sub) == "apps" ? "[device:x]\n" + t : "[app:x]\n" + t;  // wrong kind
                put(file, t);
            }
        }
    }
    if (r.chance(3)) {  // devices as a plain file
        std::error_code ec;
        stdfs::path devs;
        if (toPath(dir + "/devices", &devs)) stdfs::remove_all(devs, ec);
        put(dir + "/devices", "not a directory");
    }
}

// Reads everything and touches every result so that the sanitizers see all of it.
void exerciseLoaders(const std::string &dir) {
    ProfileSet ps;
    std::vector<FileDiag> diag;
    const bool ok = loadProfileTree(dir, &ps, &diag);
    CHECK(!ps.sections.empty() && ps.sections[0].kind == Section::GLOBAL);
    CHECK(ps.sections.size() <= kMaxSections);
    g_loadedSections += static_cast<long>(ps.sections.size());
    if (ok) {
        CHECK(!diag.empty() && diag[0].origin == FileDiag::GLOBAL_FILE);
        for (const FileDiag &d : diag)
            if (d.section != ProfileSet::npos) CHECK(d.section < ps.sections.size());
    }
    for (size_t i = 0; i < ps.sections.size(); i++) (void)ps.sectionSettings(i);
    (void)ps.resolve("spacemouse pro", "fire fox");
    (void)ps.resolve("", "");
    for (const Section &s : ps.sections) {
        std::string text, err;
        if (s.kind != Section::GLOBAL) (void)renderProfileSection(s, &text, &err);
    }
    ProfileSet at;
    std::vector<FileDiag> diag2;
    (void)loadProfileTreeAt(dir + "/dofscroll.conf", &at, &diag2);
    CHECK(at.sections.size() == ps.sections.size() || !ok);  // the same tree, read through the other entry point
    ProfileSet legacy;
    std::vector<std::string> warn;
    (void)loadLegacyProfileFile(dir + "/dofscroll.conf", &legacy, &warn);
    CHECK(!legacy.sections.empty());
    (void)loadProfiles(dir + "/dofscroll.conf", &legacy, &warn);
    (void)profileTreeStamp(dir);
    (void)profileFileStamp(dir + "/dofscroll.conf", true);
}

// The text parser alone, on a single buffer (no file system in between).
void exerciseParser(const std::string &text) {
    ProfileSet ps;
    std::vector<std::string> warn;
    (void)parseProfilesText(text, "fuzz.conf", &ps, &warn);
    CHECK(!ps.sections.empty() && ps.sections[0].kind == Section::GLOBAL && ps.sections.size() <= kMaxSections);
    for (size_t i = 0; i < ps.sections.size(); i++) (void)ps.sectionSettings(i);
    // what is parsed can be written and read again without a crash
    const std::string d = tu::makeTempDir("dsfuzz-rt");
    std::string err;
    if (saveProfiles(d + "/rt.conf", ps, &err)) {
        ProfileSet back;
        (void)loadProfiles(d + "/rt.conf", &back, nullptr);
        CHECK(!back.sections.empty());
    }
    tu::removeAll(d);
}

// The migration: it never leaves a changed dofscroll.conf behind a failure, and a second run does nothing.
void exerciseMigration(const std::string &dir) {
    stdfs::path conf;
    if (!toPath(dir + "/dofscroll.conf", &conf)) return;
    std::error_code ec;
    const bool regular = stdfs::is_regular_file(stdfs::symlink_status(conf, ec));
    const std::string before = regular ? readAll(conf) : std::string();
    const MigrateResult r = migrateToSplit(dir);
    if (r.status == MigrateStatus::Failed) {
        g_migrateFailed++;
        CHECK(!r.error.empty());
        if (regular) CHECK(readAll(conf) == before);  // "nothing was changed in dofscroll.conf"
        return;
    }
    if (r.status == MigrateStatus::Migrated) {
        g_migrated++;
        CHECK(!r.backupPath.empty());
        const MigrateResult again = migrateToSplit(dir);
        CHECK(again.status == MigrateStatus::NothingToDo);  // idempotent
    }
    exerciseLoaders(dir);  // and the migrated tree loads
}

// Writers on a loaded set: split writing and single profile files.
void exerciseWriters(const std::string &dir, Rng &r) {
    ProfileSet ps;
    std::vector<FileDiag> diag;
    if (!loadProfileTree(dir, &ps, &diag)) return;
    const std::string out = tu::makeTempDir("dsfuzz-w");
    std::vector<FileDiag> written;
    std::string err;
    if (writeProfileSetSplit(out, ps, &written, &err)) {
        ProfileSet back;
        CHECK(loadProfileTree(out, &back, nullptr));
        CHECK(back.sections.size() == ps.sections.size());
    }
    if (ps.sections.size() > 1) {
        const Section &s = ps.sections[1 + r.below(ps.sections.size() - 1)];
        const WriteResult w = saveProfileFile(out, s, nullptr);
        if (w.ok()) (void)deleteProfileFile(out, s.kind, w.path, w.stamp);
    }
    tu::removeAll(out);
}

void collectCorpus(const std::string &root, std::vector<std::string> *corpus) {
    std::error_code ec;
    if (!stdfs::is_directory(root, ec)) return;
    for (stdfs::recursive_directory_iterator it(root, stdfs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec) || it->path().extension() != ".conf") continue;
        if (stdfs::file_size(it->path(), ec) > 200000) continue;
        corpus->push_back(readAll(it->path()));
    }
}

}  // namespace

int main(int argc, char **argv) {
    long iterations = 40, only = -1;
    bool verbose = false;
    if (const char *e = std::getenv("DOFSCROLL_FUZZ_ITERATIONS")) iterations = std::atol(e);
    if (const char *e = std::getenv("DOFSCROLL_FUZZ_SEED")) g_seed = std::strtoull(e, nullptr, 10);
    std::vector<std::string> roots;
    for (int i = 1; i < argc; i++) {
        const std::string a = argv[i];
        const bool more = i + 1 < argc;
        if (a == "--iterations" && more) iterations = std::atol(argv[++i]);
        else if (a == "--seed" && more) g_seed = std::strtoull(argv[++i], nullptr, 10);
        else if (a == "--only" && more) only = std::atol(argv[++i]);
        else if (a == "--corpus" && more) roots.push_back(argv[++i]);
        else if (a == "--verbose") verbose = true;
        else {
            std::fprintf(stderr, "usage: %s [--iterations N] [--seed S] [--only K] [--corpus DIR] [--verbose]\n", argv[0]);
            return 2;
        }
    }
    if (roots.empty()) roots = {"examples", "tests/data"};

    std::vector<std::string> corpus, words;
    for (const std::string &root : roots) collectCorpus(root, &corpus);
    // words for the generator: the tokens of the corpus (keys and values), so that real keys are hit often
    for (const std::string &t : corpus) {
        size_t i = 0;
        while (i < t.size() && words.size() < 4000) {
            while (i < t.size() && std::strchr(" \t\r\n=#;[]:,", t[i])) i++;
            size_t j = i;
            while (j < t.size() && !std::strchr(" \t\r\n=#;[]:,", t[j])) j++;
            if (j > i && j - i < 40) words.push_back(t.substr(i, j - i));
            i = j;
        }
    }
    if (words.empty()) words = {"ZOOM_SENS", "PRIORITY", "BUTTON_1", "MODE", "1", "2.5", "key:a"};
    std::printf("test_profile_fuzz: %zu corpus files, seed %llu, iterations %ld\n", corpus.size(),
                static_cast<unsigned long long>(g_seed), iterations);

    for (long k = 0; k < iterations; k++) {
        if (only >= 0 && k != only) continue;
        g_iter = k;
        if (verbose) std::fprintf(stderr, "iteration %ld\n", k);
        Rng r(g_seed * 1000003ull + static_cast<uint64_t>(k));
        const std::string dir = tu::makeTempDir("dsfuzz");
        buildTree(r, dir + "/cfg", corpus, words);
        exerciseParser(pickText(r, corpus, words, false));
        exerciseLoaders(dir + "/cfg");
        exerciseWriters(dir + "/cfg", r);
        exerciseMigration(dir + "/cfg");
        tu::removeAll(dir);
    }
    std::printf("test_profile_fuzz: %ld sections loaded, %ld migrations, %ld refused or failed\n", g_loadedSections, g_migrated, g_migrateFailed);
    std::printf("test_profile_fuzz: ok\n");
    return 0;
}
