<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Installation

Before you install, check [Requirements](Requirements) (supported systems, drivers, permissions per OS). Full guide: [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md). This page is the short version.

## Linux from a GitHub release

1. Open the [Releases page](https://github.com/HoZe-Lukas/dofScroll/releases). Versions `0.x` and tags with a suffix are marked pre-release and do not appear under `/releases/latest`.
2. Download the package for your system plus `SHA256SUMS.txt`.
3. Verify: `sha256sum -c --ignore-missing SHA256SUMS.txt` (this protects against a damaged download only, not against a compromised release).
4. Install:

| Format | Command | Notes |
|---|---|---|
| RPM (built on Rocky Linux 9) | `sudo dnf install -y epel-release`, then `sudo dnf install ./dofscroll-*.rpm` | `spacenavd` comes from EPEL on RHEL 9 and its clones |
| DEB (built on Debian 12) | `sudo apt install ./dofscroll_*.deb` | Whether `spacenavd` is in your release's repositories is unverified |
| TGZ | `tar xzf dofscroll-*-Linux.tar.gz`, then `sudo cp -a dofscroll-*-Linux/usr/. /usr/` | You install the run-time dependencies yourself |

The RPM and DEB files have not been installed on a real machine yet. A package installs `/usr/bin/dofscroll` and `/usr/bin/dofscroll-ui` (only when Qt6 was available at build time; otherwise the package has the daemon only), a menu entry, the user units in `/usr/lib/systemd/user`, examples in `/usr/share/dofscroll/examples/`, and the uinput udev rule as an inactive example under `/usr/share/dofscroll/udev/`.

## First-time setup (Linux)

1. `sudo systemctl enable --now spacenavd`
2. `systemctl --user daemon-reload`, then check `systemctl --user list-unit-files 'dofscroll*'`
3. `systemctl --user enable --now dofscroll.service dofscroll-ui.service`. The interface's "Start at login" switch does the same through one interface on all systems.
4. Wayland only (or `BACKEND = uinput`): activate the udev rule yourself, it is not active by default.
   `sudo cp /usr/share/dofscroll/udev/70-dofscroll-uinput.rules /etc/udev/rules.d/`, then `sudo udevadm control --reload && sudo udevadm trigger`, then log out and in. Do not join the `input` group, see [Security](Security).

On first start the tray creates `~/.config/dofscroll/dofscroll.conf` if it does not exist (it never overwrites anything); the example joystick modes are built into the program and need no copying. Without the tray, copy the config by hand, see [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md).

## Build from source

```sh
# RHEL 9 family: sudo dnf install -y epel-release
# then: sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config spacenavd libspnav-devel \
#   libX11-devel libXtst-devel gettext
cmake -S . -B build
cmake --build build --target check-deps
cmake --build build -j4
ctest --test-dir build --output-on-failure
sudo cmake --install build --prefix /usr
```

CMake 3.16 or newer. Debian-family package names are in the full guide (marked unverified there). Options: `-DDOFSCROLL_HARDEN=OFF`, `-DDOFSCROLL_SANITIZE=ON`, `-DDOFSCROLL_WERROR=ON`. With the default prefix `/usr/local` systemd does not read the installed units; use `--prefix /usr` or `cmake --build build --target install-user-links`.

## Windows (unsigned, unverified on hardware)

CI builds an installer (`dofscroll-<version>-windows-x64-setup.exe`) and a portable ZIP. They have **not been run with a real SpaceMouse**, and they are not code-signed.

1. Download, verify the checksum as above, run the installer. No administrator rights are needed (default folder `%LOCALAPPDATA%\Programs\dofscroll`).
2. SmartScreen shows "Windows protected your PC": click **More info**, then **Run anyway**.
3. Optional component "Start the daemon at logon" (off by default). The Start menu entry opens the settings window (`dofscroll-ui.exe`); `dofscroll.exe` is the daemon without a window.
4. Config: `%APPDATA%\dofscroll\dofscroll.conf` (same format as on Linux). Log: `%LOCALAPPDATA%\dofscroll\dofscroll.log`.
5. The official 3Dconnexion driver can stay installed and running; dofscroll is designed to work next to it (not verified on hardware yet). Please run the 5 minute test `powershell -ExecutionPolicy Bypass -File scripts\test-driver-coexistence.ps1` from the install folder and attach its file to issue #36, see [Requirements](Requirements). The diagnostic tool `dofscroll-hidprobe.exe` is next to `dofscroll.exe`.

Uninstall: Settings > Apps > dofscroll; your configuration stays. Details: [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md), [port-windows.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-windows.md), [port-windows-checklist.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-windows-checklist.md).

## macOS (unsigned, unverified on hardware)

CI builds `dofscroll-<version>-macos-arm64.dmg` (Apple silicon). It is signed ad hoc only and **not notarized**, and it has **not been run with a real SpaceMouse**.

1. Open the DMG and drag `dofscroll.app` to Applications.
2. Gatekeeper blocks the first start: right-click the app > **Open** > **Open**. On macOS 15 and later use System Settings > Privacy & Security > **Open Anyway**. Alternative: `xattr -dr com.apple.quarantine /Applications/dofscroll.app`.
3. Grant **Input Monitoring** and **Accessibility** to the daemon inside the app (`Contents/Resources/daemon/dofscroll`), then restart it.
4. Autostart: the interface's "Start at login" switch, or the LaunchAgent script from the image (`README-first-run.txt`).

Details: [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md), [port-macos.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-macos.md), [packaging/macos/README.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/packaging/macos/README.md).

## Upgrade and uninstall

Upgrade per OS (A package or installer never touches your config):

- Linux: install the newer package over the old one. The package restarts the running user units of logged-in users on upgrade (best effort, never fails the upgrade). Otherwise run `systemctl --user daemon-reload` and restart `dofscroll.service` and `dofscroll-ui.service` (or the tray unit), or use the tray items "Restart daemon" and "Restart interface".
- Windows: run the new installer; it stops the daemon and a running `dofscroll-ui.exe` first, then starts the daemon again if it was running (also for silent `/S` installs) and the interface (interactive installs only).
- macOS: quit the interface (tray, Quit), stop the daemon, replace `dofscroll.app`, start both again (a loaded LaunchAgent: `launchctl kickstart -k gui/$(id -u)/io.github.dofscroll.daemon`).

Uninstall: `systemctl --user disable --now dofscroll.service dofscroll-ui.service`, then `sudo dnf remove dofscroll` (or `sudo apt remove dofscroll`), and remove `/etc/udev/rules.d/70-dofscroll-uinput.rules` if you added it. Delete `~/.config/dofscroll` to drop your settings.
