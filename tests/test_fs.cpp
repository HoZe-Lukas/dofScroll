// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tests of the file-system layer (src/platform/fs.hpp) on the current OS: bounded reads, link
// refusal, atomic replace, private directories, listing. The same file runs on Linux, macOS and
// Windows; assertions that need symlinks are skipped where the OS (or the CI user) cannot make them.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "platform/fs.hpp"
#include "testutil.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace spn;
namespace stdfs = std::filesystem;

static void put(const std::string &p, const std::string &c) {
    std::ofstream f(p, std::ios::binary);
    f << c;
}

static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// true if a symlink could be created (Windows needs a privilege or developer mode)
static bool makeSymlink(const std::string &target, const std::string &link) {
    std::error_code ec;
    stdfs::create_symlink(target, link, ec);
    return !ec;
}

static void testStat(const std::string &d) {
    put(d + "/a.txt", "hello");
    fs::FileInfo i;
    assert(fs::statPath(d + "/a.txt", &i) && i.isFile && !i.isDir && i.size == 5 && i.mtimeNs > 0 && i.fileId != 0);
    assert(fs::statPath(d, &i) && i.isDir && !i.isFile);
    assert(!fs::statPath(d + "/missing", &i) && !fs::pathExists(d + "/missing"));
    assert(fs::pathExists(d + "/a.txt") && fs::pathExists(d));
    // a replaced file gets a new id or time/size (what the daemon's reload detection relies on)
    fs::FileInfo before;
    assert(fs::lstatPath(d + "/a.txt", &before));
    std::string err;
    assert(fs::writeFileReplace(d + "/a.txt", "hello world", &err));
    fs::FileInfo after;
    assert(fs::lstatPath(d + "/a.txt", &after) && after.size == 11);
}

static void testRead(const std::string &d) {
    std::string out;
    fs::ReadOptions opt;
    put(d + "/r.txt", "abc");
    fs::ReadResult r = fs::readFileBounded(d + "/r.txt", 100, opt, &out);
    assert(r.ok() && out == "abc");
    // exactly at the cap is fine, one over is not
    r = fs::readFileBounded(d + "/r.txt", 3, opt, &out);
    assert(r.ok() && out == "abc");
    r = fs::readFileBounded(d + "/r.txt", 2, opt, &out);
    assert(r.status == fs::ReadStatus::TooLargeBySize);
    // empty file
    put(d + "/empty.txt", "");
    r = fs::readFileBounded(d + "/empty.txt", 10, opt, &out);
    assert(r.ok() && out.empty());
    // missing and directory
    r = fs::readFileBounded(d + "/nope.txt", 10, opt, &out);
    assert(r.status == fs::ReadStatus::NotFound);
    r = fs::readFileBounded(d + "/nope/dir/x.txt", 10, opt, &out);
    assert(r.status == fs::ReadStatus::NotFound);
    r = fs::readFileBounded(d, 10, opt, &out);
    assert(r.status == fs::ReadStatus::NotRegular);
    // own file passes the owner check
    opt.requireOwner = true;
    r = fs::readFileBounded(d + "/r.txt", 100, opt, &out);
    assert(r.ok() && out == "abc");
    opt.requireOwner = false;

    // links are refused with refuseLinks, followed without it
    if (makeSymlink("r.txt", d + "/link.txt")) {
        opt.refuseLinks = true;
        r = fs::readFileBounded(d + "/link.txt", 100, opt, &out);
        assert(!r.ok());
        opt.refuseLinks = false;
        r = fs::readFileBounded(d + "/link.txt", 100, opt, &out);
        assert(r.ok() && out == "abc");
        fs::FileInfo li, si;
        assert(fs::lstatPath(d + "/link.txt", &li) && li.isLink && !li.isFile);
        assert(fs::statPath(d + "/link.txt", &si) && si.isFile && !si.isLink);
        // dangling: stat fails, lstat sees the link
        assert(makeSymlink("does-not-exist", d + "/dangling.txt"));
        assert(!fs::statPath(d + "/dangling.txt", &si));
        assert(fs::lstatPath(d + "/dangling.txt", &li) && li.isLink);
    } else {
        std::puts("test_fs: symlink tests skipped (cannot create symlinks here)");
    }

    // DirHandle reads names inside the directory
    fs::DirHandle dh;
    std::string derr;
    assert(dh.open(d, &derr));
    r = dh.readFileBounded("r.txt", 100, fs::ReadOptions(), &out);
    assert(r.ok() && out == "abc");
    r = dh.readFileBounded("nope.txt", 100, fs::ReadOptions(), &out);
    assert(r.status == fs::ReadStatus::NotFound);
    fs::DirHandle bad;
    assert(!bad.open(d + "/nope", &derr) && !derr.empty());
}

static void testCreateExclusive(const std::string &d) {
    assert(fs::createFileExclusive(d + "/x.conf", "one"));
    assert(get(d + "/x.conf") == "one");
    assert(!fs::createFileExclusive(d + "/x.conf", "two"));  // never overwrites
    assert(get(d + "/x.conf") == "one");
    assert(!fs::createFileExclusive(d + "/no/such/dir/x.conf", "x"));
    if (makeSymlink("elsewhere.txt", d + "/dangling-target.conf"))
        assert(!fs::createFileExclusive(d + "/dangling-target.conf", "x"));  // no following of links
    assert(!tu::exists(d + "/elsewhere.txt"));
}

static void testReplace(const std::string &d) {
    std::string err;
    // new file inside new directories
    assert(fs::writeFileReplace(d + "/new/sub/f.conf", "v1", &err));
    assert(get(d + "/new/sub/f.conf") == "v1");
    // replace keeps no temp files behind
    assert(fs::writeFileReplace(d + "/new/sub/f.conf", "v2", &err));
    assert(get(d + "/new/sub/f.conf") == "v2");
    std::vector<std::string> names;
    bool capped = false;
    assert(fs::listDir(d + "/new/sub", 0, &names, &capped));
    size_t files = 0;
    for (const std::string &n : names)
        if (n != "." && n != "..") {
            assert(n == "f.conf");
            files++;
        }
    assert(files == 1);
    // directory as target is rejected, nothing is left behind
    assert(fs::mkdirPrivate(d + "/isdir", nullptr) == fs::MkdirResult::Created);
    assert(!fs::writeFileReplace(d + "/isdir", "x", &err) && !err.empty());
    // invalid names
    assert(!fs::writeFileReplace(d + "/", "x", &err));
    // a link at the target survives and the content lands in the real file
    put(d + "/real.conf", "old");
    if (makeSymlink("real.conf", d + "/l.conf")) {
        assert(fs::writeFileReplace(d + "/l.conf", "viaLink", &err));
        fs::FileInfo li;
        assert(fs::lstatPath(d + "/l.conf", &li) && li.isLink);
        assert(get(d + "/real.conf") == "viaLink");
    }
}

static void testMkdirs(const std::string &d) {
    std::string err;
    assert(fs::mkdirsPrivate(d + "/m/n/o", &err));
    fs::FileInfo i;
    assert(fs::statPath(d + "/m/n/o", &i) && i.isDir);
    assert(fs::mkdirsPrivate(d + "/m/n/o", &err));  // again: fine
    assert(fs::mkdirPrivate(d + "/m", nullptr) == fs::MkdirResult::Exists);
    std::string why;
    assert(fs::mkdirPrivate(d + "/q/r", &why) == fs::MkdirResult::Failed && !why.empty());  // parent missing
    put(d + "/file", "x");
    assert(!fs::mkdirsPrivate(d + "/file/sub", &err) && !err.empty());
#ifndef _WIN32
    struct stat st;
    assert(::stat((d + "/m/n/o").c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
#endif
}

static void testList(const std::string &d) {
    const std::string l = d + "/list";
    assert(fs::mkdirPrivate(l, nullptr) == fs::MkdirResult::Created);
    for (int i = 0; i < 10; i++) put(l + "/f" + std::to_string(i) + ".conf", "x");
    std::vector<std::string> names;
    bool capped = true;
    assert(fs::listDir(l, 0, &names, &capped) && !capped);
    size_t n = 0;
    for (const std::string &e : names)
        if (e != "." && e != "..") n++;
    assert(n == 10);
    names.clear();
    assert(fs::listDir(l, 4, &names, &capped) && capped && names.size() == 4);
    names.clear();
    assert(fs::listDir(l, 100, &names, &capped) && !capped);
    names.clear();
    assert(!fs::listDir(l + "/missing", 0, &names, &capped));
}

static void testMisc() {
    char a[17], b[17];
    fs::randomHex16(a);
    fs::randomHex16(b);
    assert(std::string(a).size() == 16 && std::string(a).find_first_not_of("0123456789abcdef") == std::string::npos);
    assert(std::string(a) != std::string(b));  // 2^-64 flake chance
    const std::string exe = fs::executablePath();
    assert(!exe.empty() && exe.find('\\') == std::string::npos);
    assert(tu::exists(exe));
    assert(fs::lastSeparator("a/b") == 1 && fs::lastSeparator("ab") == std::string::npos);
#ifdef _WIN32
    assert(fs::lastSeparator("a\\b") == 1);
#endif
    const std::string base = fs::userConfigBase();
    assert(!base.empty() && base.find('\\') == std::string::npos);
}

#ifdef _WIN32
// Windows path hazards (rules: path_rules.hpp): every entry point refuses them without touching
// the file system.
static void testWindowsHazards(const std::string &d) {
    std::string err, out;
    fs::FileInfo i;
    put(d + "/plain.txt", "x");
    const std::string bad[] = {d + "/con",          d + "/NUL.txt",       d + "/com1.conf",
                               d + "/plain.txt:s",  d + "/plain.txt::$DATA", d + "/name.",
                               d + "/name ",        "\\\\?\\" + d,   "\\\\.\\COM1",
                               "C:relative",        d + "/a*b",           d + "/a?b"};
    for (const std::string &p : bad) {
        err.clear();
        assert(!fs::statPath(p, &i) && !fs::lstatPath(p, &i) && !fs::pathExists(p));
        const fs::ReadResult r = fs::readFileBounded(p, 100, fs::ReadOptions(), &out);
        assert(r.status == fs::ReadStatus::OpenFailed && r.sysError.find("unsafe path") != std::string::npos);
        assert(!fs::createFileExclusive(p, "x"));
        assert(!fs::writeFileReplace(p, "x", &err) && err.find("unsafe path") != std::string::npos);
        assert(fs::mkdirPrivate(p, &err) == fs::MkdirResult::Failed);
        assert(!fs::mkdirsPrivate(p, &err) && !fs::removeFile(p) && !fs::removeDir(p));
        std::vector<std::string> names;
        bool capped = false;
        assert(!fs::listDir(p, 0, &names, &capped));
        fs::DirHandle dh;
        assert(!dh.open(p, &err));
        assert(!fs::renamePath(p, d + "/x") && !fs::renamePath(d + "/plain.txt", p));
    }
    assert(tu::exists(d + "/plain.txt"));  // the stream/device names never reached the file system
    // names given to a DirHandle must be single, safe components
    fs::DirHandle dh;
    assert(dh.open(d, &err));
    assert(dh.readFileBounded("con", 10, fs::ReadOptions(), &out).status == fs::ReadStatus::OpenFailed);
    assert(dh.readFileBounded("plain.txt:s", 10, fs::ReadOptions(), &out).status == fs::ReadStatus::OpenFailed);
    assert(dh.readFileBounded("sub/plain.txt", 10, fs::ReadOptions(), &out).status == fs::ReadStatus::OpenFailed);
    assert(dh.readFileBounded("plain.txt", 10, fs::ReadOptions(), &out).ok());
    // a normal "dotted" name that is NOT reserved still works
    assert(fs::writeFileReplace(d + "/console.conf", "ok", &err) && get(d + "/console.conf") == "ok");
    assert(fs::writeFileReplace(d + "/a.con", "ok", &err));
}
#endif

int main() {
    const std::string d = tu::makeTempDir("spn_fs");
    testStat(d);
    testRead(d);
    testCreateExclusive(d);
    testReplace(d);
    testMkdirs(d);
    testList(d);
    testMisc();
#ifdef _WIN32
    testWindowsHazards(d);
#endif
    tu::removeAll(d);
    std::puts("test_fs: ok");
    return 0;
}
