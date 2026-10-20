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
    mkdir1(ex + "/buttons");
    put(ex + "/default/dofscroll.conf", "[global]\nA = 1\n");
    put(ex + "/modes/a.conf", "a\n");
    put(ex + "/buttons/latch.conf", "ACTION = toggle:nav\n");

    // only the configuration is seeded: the joystick and button modes are built in
    assert(spn::seedDefaults(&log));
    assert(!log.empty());
    assert(get(conf) == "[global]\nA = 1\n");
    assert(!exists(modes) && !exists(cfg + "/dofscroll/buttons") && countTmp(cfg + "/dofscroll") == 0);
#ifndef _WIN32
    struct stat st;
    assert(::stat(conf.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
#endif

    // idempotent, never overwrites
    put(conf, "mine\n");
    assert(!spn::seedDefaults(&log));
    assert(get(conf) == "mine\n" && !exists(modes));

    // a missing config is re-seeded, existing user directories are left alone
    spn::fs::removeFile(conf);
    mkdir1(modes);
    put(modes + "/a.conf", "mine\n");
    assert(spn::seedDefaults(&log));
    assert(get(conf) == "[global]\nA = 1\n" && get(modes + "/a.conf") == "mine\n" && !exists(modes + "/b.conf"));

    // a config at the previous location counts as an existing config
    spn::fs::removeFile(conf);
    mkdir1(cfg + "/spnav-scroll");
    put(cfg + "/spnav-scroll/spnav-scroll.conf", "old\n");
    assert(!spn::seedDefaults(&log));
    assert(!exists(conf));

    // an oversized example configuration is refused
    {
        tu::removeAll(cfg + "/dofscroll");
        tu::removeAll(cfg + "/spnav-scroll");
        put(ex + "/default/dofscroll.conf", std::string(70 * 1024, 'x'));
        assert(!spn::seedDefaults(&log) && !exists(conf));
    }

    tu::removeAll(root);
    std::puts("test_seed OK");
    return 0;
}
