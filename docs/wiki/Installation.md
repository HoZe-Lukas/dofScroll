<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Installation

Before you install, check [Requirements](Requirements) (supported systems, drivers, permissions per OS). Full guide: [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md). This page is the short version.

## Linux from a GitHub release

1. Open the [Releases page](https://github.com/HoZe-Lukas/dofScroll/releases). Versions `0.x` and tags with a suffix are marked pre-release and do not appear under `/releases/latest`.
2. Download the package for your system plus `SHA256SUMS.txt`.
3. Verify: `sha256sum -c --ignore-missing SHA256SUMS.txt` (this protects against a damaged download only, not against a compromised release). To check where a file was built: `gh attestation verify <file> -R HoZe-Lukas/dofScroll` proves that this repository's release workflow produced it.
4. Install:

| Format | Command | Notes |
|---|---|---|
| RPM (built on Rocky Linux 9) | `sudo dnf install -y epel-release`, then `sudo dnf install ./dofscroll-*.rpm` | `spacenavd` comes from EPEL on RHEL 9 and its clones (Fedora has it in its own repositories); Qt is inside the package, nothing else to install |
| DEB (built on Debian 12) | `sudo apt install ./dofscroll_*.deb` | `spacenavd` is in Debian 12 and 13 and Ubuntu 24.04 (install-tested in CI); check other releases with `apt-cache policy spacenavd` |
| TGZ | `tar xzf dofscroll-*-Linux.tar.gz`, then `sudo cp -a dofscroll-*-Linux/usr/. /usr/` | Self-contained interface (own Qt): `dofscroll-ui` runs from the unpacked folder; the daemon needs `libspnav` and the X11 libraries from your distribution |

The RPM and DEB files are install-tested in CI containers only (not on a real desktop). A package installs `/usr/bin/dofscroll` and `/usr/bin/dofscroll-ui` (with its own Qt 6.9.3 in `/usr/lib/dofscroll`, which is why the packages are 27 to 35 MB), `/usr/bin/dofscroll-hidprobe` (diagnostic), a menu entry, the user units in `/usr/lib/systemd/user`, example configs in `/usr/share/dofscroll/examples/` (the joystick modes and button modes are built into the program), and the uinput udev rule as an inactive example under `/usr/share/dofscroll/udev/`.

## First-time setup (Linux)

**Install the package, then open dofScroll from the application menu.** That is the whole first start: the interface starts the daemon by itself and shows a short setup guide (daemon running, SpaceMouse found, "Start at login" on by default). The RPM and DEB enable the `spacenavd` service on every install and upgrade (best effort, as root; a fresh install does not start your dofscroll units, the interface does that). The steps below are the alternative: a TGZ install, a machine without a desktop, or when the automatic way did not work.

1. `sudo systemctl enable --now spacenavd` (done by the RPM and DEB)
2. `systemctl --user daemon-reload`, then check `systemctl --user list-unit-files 'dofscroll*'`
3. `systemctl --user enable --now dofscroll.service dofscroll-ui.service`. The interface's "Start at login" switch does the same through one interface on all systems.
4. Wayland only (or `BACKEND = uinput`): activate the udev rule yourself, it is not active by default.
   `sudo cp /usr/share/dofscroll/udev/70-dofscroll-uinput.rules /etc/udev/rules.d/`, then `sudo udevadm control --reload && sudo udevadm trigger`, then log out and in. Do not join the `input` group, see [Security](Security).

On first start the tray creates `~/.config/dofscroll/dofscroll.conf` (global settings) if it does not exist (it never overwrites anything); each profile is a file in `devices/` or `apps/`, and an existing single-file config is split once, with a backup, see [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md#upgrading-the-configuration-one-file-per-profile); the example joystick modes are built into the program and need no copying. Without the tray, copy the config by hand, see [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md).

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
3. **Open dofscroll from the Start menu** (or tick "Open dofscroll now" on the finish page): the interface (`dofscroll-ui.exe`) starts the daemon (`dofscroll.exe`, no window) by itself and the first start shows the setup guide with "Start at login" on. The installer has the optional component "Start the daemon at logon" (off by default) too.
4. Config: `%APPDATA%\dofscroll\` (`dofscroll.conf`, `devices\`, `apps\`; same layout as on Linux). Log: `%LOCALAPPDATA%\dofscroll\dofscroll.log`.
5. The official 3Dconnexion driver can stay installed and running; dofscroll works next to it and never asks you to stop it: with the driver installed it reads the device through the driver's interface (setting `INPUT`, default `auto`), otherwise directly (not verified on hardware yet). Please run the 5 minute test `powershell -ExecutionPolicy Bypass -File scripts\test-driver-coexistence.ps1` from the install folder and attach its file to issue #36, see [Requirements](Requirements). The diagnostic tool `dofscroll-hidprobe.exe` is next to `dofscroll.exe`.

Uninstall: Settings > Apps > dofscroll; your configuration stays. Details: [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md), [port-windows.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-windows.md), [port-windows-checklist.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-windows-checklist.md).

## macOS (unsigned, unverified on hardware)

CI builds `dofscroll-<version>-macos-arm64.dmg` (Apple silicon). It is signed ad hoc only and **not notarized**, and it has **not been run with a real SpaceMouse**.

1. Open the DMG and drag `dofscroll.app` to Applications.
2. Gatekeeper blocks the first start: right-click the app > **Open** > **Open**. On macOS 15 and later use System Settings > Privacy & Security > **Open Anyway**. Alternative: `xattr -dr com.apple.quarantine /Applications/dofscroll.app`.
3. **Open dofscroll from Applications.** The interface starts the daemon inside the app and shows the setup guide: "Start at login" is on by default, and each of the two permissions, **Input Monitoring** and **Accessibility**, has a button that opens the matching pane of System Settings and a live note while macOS does not accept the events. Switch on dofscroll in both lists (if it is not listed, add `Contents/Resources/daemon/dofscroll` with `+`).
4. Alternative by hand: the LaunchAgent script from the image (`README-first-run.txt`).

Details: [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md), [port-macos.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-macos.md), [packaging/macos/README.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/packaging/macos/README.md).

## Upgrade and uninstall

Upgrade per OS (A package or installer never touches your config):

- Linux: install the newer package over the old one. The package stops the running daemon and interface before installing and starts them again afterwards. If that did not happen (for example you started the program from a terminal), run `systemctl --user daemon-reload` and restart `dofscroll.service` and `dofscroll-ui.service`, or use the tray item "Restart".
- Windows: run the new installer; it stops the daemon and a running `dofscroll-ui.exe` first, then starts the daemon again if it was running (also for silent `/S` installs) and the interface (interactive installs only).
- macOS: quit the interface (tray, Quit), stop the daemon, replace `dofscroll.app`, start both again (a loaded LaunchAgent: `launchctl kickstart -k gui/$(id -u)/io.github.dofscroll.daemon`).

Uninstall: `systemctl --user disable --now dofscroll.service dofscroll-ui.service`, then `sudo dnf remove dofscroll` (or `sudo apt remove dofscroll`), and remove `/etc/udev/rules.d/70-dofscroll-uinput.rules` if you added it. Delete `~/.config/dofscroll` to drop your settings.
