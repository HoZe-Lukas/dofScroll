// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Tests for the single-instance paths of dofscroll-ui (src/ui/model/instance_paths.hpp).
#include <cassert>
#include <cstdio>
#include <string>

#include "ui/model/instance_paths.hpp"

using namespace spn::ui;

int main() {
    // runtime directory wins; the config directory is only the fallback
    assert(instanceDir("/run/user/1000", "/home/u/.config/dofscroll") == "/run/user/1000");
    assert(instanceDir("", "/home/u/.config/dofscroll") == "/home/u/.config/dofscroll");
    assert(instanceLockPath("/run/user/1000") == "/run/user/1000/dofscroll-ui.lock");
    assert(instanceRaisePath("/run/user/1000") == "/run/user/1000/dofscroll-ui.raise");
    // never the data directory, which the systemd unit mounts read-only
    assert(instanceLockPath(instanceDir("/run/user/1000", "/c")).find(".local/share") == std::string::npos);

    assert(classifyLockFailure(true) == LockOutcome::AlreadyRunning);
    assert(classifyLockFailure(false) == LockOutcome::Failure);
    assert(kExitLockFailure != 0 && kExitAlreadyRunning == 0);
    std::puts("test_ui_instance OK");
    return 0;
}
