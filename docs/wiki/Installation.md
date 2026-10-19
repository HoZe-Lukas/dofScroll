<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Installation

Full guide: [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md). This page is the short version.

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

The RPM and DEB files have not been installed on a real machine yet. A package installs `/usr/bin/dofscroll`, `/usr/bin/dofscroll-tray`, the user units in `/usr/lib/systemd/user`, examples in `/usr/share/dofscroll/examples/`, and the uinput udev rule as an inactive example under `/usr/share/dofscroll/udev/`.

## First-time setup (Linux)

1. `sudo systemctl enable --now spacenavd`
2. `systemctl --user daemon-reload`, then check `systemctl --user list-unit-files 'dofscroll*'`
3. `systemctl --user enable --now dofscroll.service dofscroll-tray.service`
4. Wayland only (or `BACKEND = uinput`): activate the udev rule yourself, it is not active by default.
   `sudo cp /usr/share/dofscroll/udev/70-dofscroll-uinput.rules /etc/udev/rules.d/`, then `sudo udevadm control --reload && sudo udevadm trigger`, then log out and in. Do not join the `input` group, see [Security](Security).

On first start the tray creates `~/.config/dofscroll/dofscroll.conf` and the example modes if they do not exist (it never overwrites anything). Without the tray, copy them by hand, see [docs/installation.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/installation.md).

## Build from source

```sh
# RHEL 9 family: sudo dnf install -y epel-release
# then: sudo dnf install -y gcc-c++ cmake pkgconf-pkg-config spacenavd libspnav-devel \
#   libX11-devel libXtst-devel gtk3-devel libappindicator-gtk3-devel gettext
cmake -S . -B build
cmake --build build --target check-deps
cmake --build build -j4
ctest --test-dir build --output-on-failure
sudo cmake --install build --prefix /usr
```

CMake 3.16 or newer. Debian-family package names are in the full guide (marked unverified there). Options: `-DDOFSCROLL_TRAY=OFF` (daemon only, no GTK), `-DDOFSCROLL_HARDEN=OFF`, `-DDOFSCROLL_SANITIZE=ON`, `-DDOFSCROLL_WERROR=ON`. With the default prefix `/usr/local` systemd does not read the installed units; use `--prefix /usr` or `cmake --build build --target install-user-links`.

## Windows (experimental, unverified on hardware)

The daemon is built by CI (MSVC and MinGW-w64) and uploaded as the artifact `dofscroll-windows-*`. It has **not been tested with a real SpaceMouse**. There is no installer, no tray and no settings window yet.

1. Unpack the artifact and keep `dofscroll.exe`, `devices\` and `examples\` together.
2. Start `dofscroll.exe` (no console window). Config: `%APPDATA%\dofscroll\dofscroll.conf` (same format as on Linux). Log: `%LOCALAPPDATA%\dofscroll\dofscroll.log`.
3. Optional per-user autostart without administrator rights: `packaging-windows\install-autostart.ps1 -ExePath <full path to dofscroll.exe>`.
4. If the 3Dconnexion driver is installed, disable its per-application navigation for the programs you test, otherwise scrolling is doubled.

Details and the tester checklist: [port-windows.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-windows.md), [port-windows-checklist.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-windows-checklist.md).

## macOS (experimental, unverified on hardware)

Build from source (Xcode command line tools and CMake), then install the per-user LaunchAgent:

```sh
sh packaging/macos/install-launchagent.sh --daemon "$PWD/build/dofscroll"
```

Grant the binary **Input Monitoring** and **Accessibility** in System Settings > Privacy & Security. There is no signed or notarized package, no tray and no settings window. Details: [port-macos.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/port-macos.md), [packaging/macos/README.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/packaging/macos/README.md).

## Upgrade and uninstall

Upgrade: install the newer package over the old one, then `systemctl --user daemon-reload` and restart both units. A package never touches your config.

Uninstall: `systemctl --user disable --now dofscroll.service dofscroll-tray.service`, then `sudo dnf remove dofscroll` (or `sudo apt remove dofscroll`), and remove `/etc/udev/rules.d/70-dofscroll-uinput.rules` if you added it. Delete `~/.config/dofscroll` to drop your settings.
