// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tests for RuntimeDir (src/platform/linux/runtime_files.*): directory checks on the descriptor,
// fixed file names, no link following, status writer and suspend probe through the directory fd.
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "common.hpp"
#include "platform/fs.hpp"
#include "platform/linux/runtime_files.hpp"
#include "testutil.hpp"

using namespace spn;

static std::string slurp(const std::string &p) {
    std::string t;
    fs::readFileBounded(p, 1 << 20, fs::ReadOptions(), &t);
    return t;
}

int main() {
    const std::string dir = tu::makeTempDir("dofscroll-rtdir");
    assert(runtimeDirTrusted(dir));
    assert(!runtimeDirTrusted(""));
    assert(!runtimeDirTrusted("relative/dir"));
    assert(!runtimeDirTrusted(dir + "/../" + "x"));
    assert(!runtimeDirTrusted(dir + "/missing"));

    // a symlinked directory is not followed; a group-writable one is not trusted
    assert(::symlink(dir.c_str(), (dir + "-link").c_str()) == 0);
    assert(!runtimeDirTrusted(dir + "-link"));
    ::unlink((dir + "-link").c_str());
    assert(::mkdir((dir + "/open").c_str(), 0777) == 0);
    ::chmod((dir + "/open").c_str(), 0777);
    assert(!runtimeDirTrusted(dir + "/open"));

    {
        RuntimeDir rd(dir);
        assert(rd.ok());
        const int fd = rd.open(RuntimeFile::LogFile, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
        assert(fd >= 0);
        ::close(fd);
        struct stat st;
        assert(rd.lstat(RuntimeFile::LogFile, &st) && S_ISREG(st.st_mode) && (st.st_mode & 077) == 0);
        assert(tu::exists(dir + "/dofscroll.log"));
        assert(rd.unlink(RuntimeFile::LogFile) && !tu::exists(dir + "/dofscroll.log"));
        // a link at the name is refused by O_NOFOLLOW
        assert(::symlink((dir + "/target").c_str(), (dir + "/dofscroll-suspend").c_str()) == 0);
        assert(rd.open(RuntimeFile::SuspendFile, O_WRONLY | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600) < 0);
        assert(!tu::exists(dir + "/target"));
        rd.unlink(RuntimeFile::SuspendFile);
    }

    // status writer: atomic file under the fixed name, no temp leftovers, removed on request
    {
        StatusWriter w(dir);
        w.update("profile=global\n", 1);
        const std::string path = dir + "/dofscroll-status";
        assert(slurp(path).rfind("profile=global\n", 0) == 0 && slurp(path).find("updated=") != std::string::npos);
        std::vector<std::string> names;
        bool capped = false;
        assert(fs::listDir(dir, 0, &names, &capped));
        for (const std::string &n : names) assert(n.find(".tmp") == std::string::npos);
        w.remove();
        assert(!tu::exists(path));
    }
    // suspend probe: fresh user-only file counts
    {
        SuspendProbe probe(dir);
        assert(!probe());
        assert(fs::touchFilePrivate(dir + "/dofscroll-suspend") && probe());
        fs::removeFile(dir + "/dofscroll-suspend");
        assert(!probe());
    }
    // untrusted directory: everything stays off
    {
        StatusWriter w(dir + "/open");
        w.update("x=1\n", 1);
        assert(!tu::exists(dir + "/open/dofscroll-status"));
        assert(!SuspendProbe(dir + "/open")());
    }
    tu::removeAll(dir);
    std::puts("test_runtime_dir: all ok");
    return 0;
}
