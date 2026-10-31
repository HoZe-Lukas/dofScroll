// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Reload request for a daemon that does not run under dofscroll.service (issue #338): the daemon writes its process id
// into the status file; the interface sends SIGHUP to that process when it is a dofscroll process of the same user.
#undef NDEBUG
#include <cassert>
#include <filesystem>
#include <fstream>
#include <string>

#include "testutil.hpp"

#if defined(__linux__)
#include <sys/stat.h>

#include "platform/linux/reload_fallback.hpp"

using namespace spn;

static void put(const std::string &path, const std::string &text, mode_t mode = 0600) {
    std::ofstream(path, std::ios::binary) << text;
    ::chmod(path.c_str(), mode);
}

int main() {
    // the pid line of the status file
    assert(statusFilePid("profile=x\npid=4242\nmode=normal\n") == 4242);
    assert(statusFilePid("pid=7") == 7);
    assert(statusFilePid("profile=x\n") == 0);
    assert(statusFilePid("pid=abc\n") == 0 && statusFilePid("pid=-5\n") == 0 && statusFilePid("pid=0\n") == 0);
    assert(statusFilePid("pid=99999999999999999999\n") == 0);
    assert(statusFilePid("xpid=5\n") == 0);

    const std::string rt = tu::makeTempDir("dofscroll-reloadfb");
    const std::string proc = tu::makeTempDir("dofscroll-reloadfb-proc");
    std::filesystem::create_directories(proc + "/4242");
    put(proc + "/4242/comm", "dofscroll\n");
    std::filesystem::create_directories(proc + "/4300");
    put(proc + "/4300/comm", "bash\n");
    long sent = 0;
    auto hup = [&](long pid) {
        sent = pid;
        return 0;
    };
    std::string err;

    assert(!reloadByStatusPid(rt, proc, hup, &err) && sent == 0);  // no status file: nothing to signal
    put(rt + "/dofscroll-status", "profile=x\npid=4242\nupdated=1\n");
    assert(reloadByStatusPid(rt, proc, hup, &err) && sent == 4242);
    // another program under that pid (the daemon is gone and the number was reused): never signalled
    sent = 0;
    put(rt + "/dofscroll-status", "pid=4300\n");
    assert(!reloadByStatusPid(rt, proc, hup, &err) && sent == 0 && !err.empty());
    // no such process
    put(rt + "/dofscroll-status", "pid=4999\n");
    assert(!reloadByStatusPid(rt, proc, hup, &err) && sent == 0);
    // a status file that others can write is not trusted
    put(rt + "/dofscroll-status", "pid=4242\n", 0666);
    assert(!reloadByStatusPid(rt, proc, hup, &err) && sent == 0);
    // a link in place of the status file is not followed
    std::filesystem::remove(rt + "/dofscroll-status");
    put(rt + "/real", "pid=4242\n");
    std::filesystem::create_symlink(rt + "/real", rt + "/dofscroll-status");
    assert(!reloadByStatusPid(rt, proc, hup, &err) && sent == 0);
    // an empty runtime directory (XDG_RUNTIME_DIR unset)
    assert(!reloadByStatusPid("", proc, hup, &err) && sent == 0);
    tu::removeAll(rt);
    tu::removeAll(proc);
    return 0;
}
#else
int main() { return 0; }
#endif
