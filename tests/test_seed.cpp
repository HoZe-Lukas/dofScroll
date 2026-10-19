// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "seed.hpp"

static bool exists(const std::string &p) {
    struct stat st;
    return ::lstat(p.c_str(), &st) == 0;
}
static void put(const std::string &p, const std::string &c) { std::ofstream(p) << c; }
static std::string get(const std::string &p) {
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

int main() {
    char tmpl[] = "/tmp/dofscroll-seed-XXXXXX";
    const char *made = mkdtemp(tmpl);
    assert(made);
    const std::string root = made, cfg = root + "/cfg", ex = root + "/ex";
    ::mkdir(cfg.c_str(), 0700);
    ::mkdir(ex.c_str(), 0700);
    ::setenv("XDG_CONFIG_HOME", cfg.c_str(), 1);
    ::setenv("DOFSCROLL_EXAMPLES", ex.c_str(), 1);
    const std::string conf = cfg + "/dofscroll/dofscroll.conf", modes = cfg + "/dofscroll/modes";
    std::string log;

    // examples directory without content: nothing to do
    assert(!spn::seedDefaults(&log));
    assert(!exists(conf));

    ::mkdir((ex + "/default").c_str(), 0700);
    ::mkdir((ex + "/modes").c_str(), 0700);
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
    struct stat st;
    assert(::stat(conf.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);
    assert(::stat(modes.c_str(), &st) == 0 && (st.st_mode & 0777) == 0700);

    // idempotent, never overwrites
    put(conf, "mine\n");
    put(modes + "/a.conf", "mine\n");
    assert(!spn::seedDefaults(&log));
    assert(get(conf) == "mine\n" && get(modes + "/a.conf") == "mine\n");

    // an existing modes dir blocks mode seeding, a missing config is re-seeded
    ::unlink(conf.c_str());
    put(ex + "/modes/c.conf", "c\n");
    assert(spn::seedDefaults(&log));
    assert(get(conf) == "[global]\nA = 1\n");
    assert(get(modes + "/a.conf") == "mine\n" && !exists(modes + "/c.conf"));

    // a config at the previous location counts as an existing config
    ::unlink(conf.c_str());
    ::mkdir((cfg + "/spnav-scroll").c_str(), 0700);
    put(cfg + "/spnav-scroll/spnav-scroll.conf", "old\n");
    assert(!spn::seedDefaults(&log));
    assert(!exists(conf));

    const std::string cmd = "rm -rf '" + root + "'";
    assert(std::system(cmd.c_str()) == 0);
    std::puts("test_seed OK");
    return 0;
}
