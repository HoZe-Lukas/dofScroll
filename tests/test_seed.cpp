// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "platform/fs.hpp"
#include "seed.hpp"
#include "testutil.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

static bool exists(const std::string &p) { return spn::fs::pathExists(p); }
// Number of entries whose name contains ".tmp" (leftovers of an interrupted copy).
static int countTmp(const std::string &dir) {
    int n = 0;
    std::vector<std::string> names;
    bool capped = false;
    if (spn::fs::listDir(dir, 0, &names, &capped))
        for (const std::string &e : names)
            if (e.find(".tmp") != std::string::npos) ++n;
    return n;
}
static void put(const std::string &p, const std::string &c) { std::ofstream(p, std::ios::binary) << c; }
static std::string get(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
static void mkdir1(const std::string &p) { std::filesystem::create_directory(p); }

int main() {
    const std::string root = tu::makeTempDir("dofscroll-seed"), ex = root + "/ex";
    std::filesystem::create_directories(root + "/cfg");
    mkdir1(ex);
    // Linux: $XDG_CONFIG_HOME, macOS: $HOME/Library/Application Support, Windows: %APPDATA%
    const std::string cfg = tu::useConfigBase(root + "/cfg");
    tu::setEnv("DOFSCROLL_EXAMPLES", ex);
    const std::string conf = cfg + "/dofscroll/dofscroll.conf", modes = cfg + "/dofscroll/modes";
    std::string log;

    // examples directory without content: nothing to do
    assert(!spn::seedDefaults(&log));
    assert(!exists(conf));

    mkdir1(ex + "/default");
    mkdir1(ex + "/modes");
    put(ex + "/default/dofscroll.conf", "[global]\nA = 1\n");
    put(ex + "/modes/a.conf", "a\n");
    put(ex + "/modes/b.conf", "b\n");
    put(ex + "/modes/skip.txt", "x\n");
    put(ex + "/modes/big.conf", std::string(70 * 1024, 'x'));
    put(root + "/secret", "secret\n");
    assert(::symlink((root + "/secret").c_str(), (ex + "/modes/link.conf").c_str()) == 0);

    assert(spn::seedDefaults(&log));
    assert(!log.empty());
    assert(get(conf) == "[global]\nA = 1\n");
    assert(get(modes + "/a.conf") == "a\n" && get(modes + "/b.conf") == "b\n");
    assert(!exists(modes + "/skip.txt") && !exists(modes + "/big.conf") && !exists(modes + "/link.conf"));
#ifndef _WIN32
    struct stat st;
    assert(::stat(conf.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(::stat(modes.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);
#endif

    // idempotent, never overwrites
    put(conf, "mine\n");
    put(modes + "/a.conf", "mine\n");
    assert(!spn::seedDefaults(&log));
    assert(get(conf) == "mine\n" && get(modes + "/a.conf") == "mine\n");

    // an existing modes dir blocks mode seeding, a missing config is re-seeded
    spn::fs::removeFile(conf);
    put(ex + "/modes/c.conf", "c\n");
    assert(spn::seedDefaults(&log));
    assert(get(conf) == "[global]\nA = 1\n");
    assert(get(modes + "/a.conf") == "mine\n" && !exists(modes + "/c.conf"));

    // a config at the previous location counts as an existing config
    spn::fs::removeFile(conf);
    mkdir1(cfg + "/spnav-scroll");
    put(cfg + "/spnav-scroll/spnav-scroll.conf", "old\n");
    assert(!spn::seedDefaults(&log));
    assert(!exists(conf));

    // interrupted earlier run: a stale temp directory is cleaned up and the complete set is published
    {
        tu::removeAll(cfg + "/dofscroll");
        tu::removeAll(cfg + "/spnav-scroll");
        mkdir1(cfg + "/dofscroll");
        const std::string stale = cfg + "/dofscroll/modes.tmp.99999";
        mkdir1(stale);
        put(stale + "/a.conf", "partial\n");
        assert(!exists(modes));
        assert(spn::seedDefaults(&log));
        assert(!exists(stale));  // stale temp directory removed
        assert(get(modes + "/a.conf") == "a\n" && get(modes + "/b.conf") == "b\n");
        // no temporary leftovers next to the published files
        assert(countTmp(cfg + "/dofscroll") == 0 && countTmp(modes) == 0);
    }

    tu::removeAll(root);
    std::puts("test_seed OK");
    return 0;
}
