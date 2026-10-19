# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
# Script mode: cmake -DSRC=<repo> -DBIN=<build dir> -P cmake/InstallUserLinks.cmake
# Only prints the symlink installation commands (readme.md); runs nothing with sudo.
message("# Symlink installation (readme.md); run the root part yourself:")
message("sudo ln -sf \"${BIN}/dofscroll\" /usr/local/bin/dofscroll")
message("sudo ln -sf \"${BIN}/dofscroll-tray\" /usr/local/bin/dofscroll-tray")
message("sudo mkdir -p /usr/local/share/dofscroll-tray/icons")
message("sudo ln -sf \"${SRC}/icons/dofscroll-tray.svg\" /usr/local/share/dofscroll-tray/icons/dofscroll-tray.svg")
message("sudo systemctl enable --now spacenavd")
message("# as a normal user:")
message("mkdir -p ~/.config/systemd/user")
message("ln -sf \"${BIN}/systemd-dev/dofscroll.service\" ~/.config/systemd/user/dofscroll.service")
message("ln -sf \"${BIN}/systemd-dev/dofscroll-tray.service\" ~/.config/systemd/user/dofscroll-tray.service")
message("systemctl --user daemon-reload")
message("systemctl --user enable --now dofscroll.service dofscroll-tray.service")
