// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Helper program lookup in fixed directories (never PATH).
#undef NDEBUG
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "platform/exec_lookup.hpp"

using namespace spn;

static void touch(const std::string &path, mode_t mode) {
    std::ofstream(path) << "#!/bin/sh\n";
    chmod(path.c_str(), mode);
}

int main() {
    char tmpl[] = "/tmp/dofscroll-exec-XXXXXX";
    const char *root = mkdtemp(tmpl);
    assert(root);
    const std::string a = std::string(root) + "/a", b = std::string(root) + "/b";
    assert(mkdir(a.c_str(), 0700) == 0 && mkdir(b.c_str(), 0700) == 0);

    touch(a + "/tool", 0644);   // not executable
    touch(b + "/tool", 0755);
    assert(mkdir((a + "/dirtool").c_str(), 0700) == 0);  // a directory is not a program
    touch(a + "/other", 0755);

    // first directory that has an executable regular file wins; a non-executable file is skipped
    assert(findExecutableIn("tool", {a, b}) == b + "/tool");
    assert(findExecutableIn("other", {a, b}) == a + "/other");
    assert(findExecutableIn("other", {b, a}) == a + "/other");
    assert(findExecutableIn("dirtool", {a, b}).empty());
    assert(findExecutableIn("missing", {a, b}).empty());
    // a trailing slash in the directory is fine
    assert(findExecutableIn("tool", {b + "/"}) == b + "/tool");

    // relative and empty entries are never used, whatever is in the working directory
    assert(chdir(b.c_str()) == 0);
    assert(findExecutableIn("tool", {"", ".", "bin", "../b"}).empty());
    assert(findExecutableIn("tool", {"."}).empty());

    // the name must be a plain file name
    assert(findExecutableIn("", {b}).empty());
    assert(findExecutableIn(".", {b}).empty() && findExecutableIn("..", {b}).empty());
    assert(findExecutableIn("../b/tool", {a, b}).empty());
    assert(findExecutableIn(b + "/tool", {"/"}).empty());

    // PATH is not consulted: only the given directories count
    setenv("PATH", b.c_str(), 1);
    assert(findExecutableIn("tool", trustedBinDirs()).empty());  // b is on PATH, but not a trusted directory
    assert(trustedBinDirs() == (std::vector<std::string>{"/usr/bin", "/usr/local/bin", "/bin"}));
    assert(findExecutableIn("sh", trustedBinDirs()).find("/bin/sh") != std::string::npos);

    int rc = system(("rm -rf '" + std::string(root) + "'").c_str());
    (void)rc;
    return 0;
}
