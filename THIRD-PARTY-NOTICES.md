<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Third-party notices

dofscroll itself is licensed under the GNU General Public License, version 3 or (at your option)
any later version (`GPL-3.0-or-later`, see `LICENSE`). Its source code contains no third-party
source code. The **binary packages bundle third-party libraries** (Qt and what Qt needs, below);
the programs link against or talk to the other components, which keep their own licenses.

The packages carry the license texts and the exact inventory of what they bundle:
`/usr/share/doc/dofscroll/third-party/` (Linux packages: `bundled-libraries.txt` lists every bundled
library with its source package, version, license and upstream address; one folder per library
package holds its license files; `Qt-LGPL-3.0-only.txt` and `ICU-73.2-LICENSE.txt` are the texts for
Qt and ICU) and this file in `/usr/share/doc/dofscroll/`. The Windows and macOS packages contain this
file and `LICENSE` next to the program.

## Components that are not bundled

| Component | Use in dofscroll | Linking / relationship | License |
|---|---|---|---|
| `libspnav` | Client library to read events from `spacenavd` (daemon) | Linked at build time (`libspnav-devel`), provided by your distribution | Free software license (upstream: BSD-style); check the license of the version you ship |
| `spacenavd` | Hardware driver service that talks to the SpaceMouse | **Separate process**, not linked; dofscroll only connects to its socket | GPL (upstream); check the license of the version you ship |
| Xlib (`libX11`), `libXtst` (daemon, Linux) | X11 connection, XTest event injection, active-window query | Dynamically linked, provided by your distribution (the interface in the Linux packages carries its own libX11, see below) | MIT/X11-style licenses (upstream) |
| gettext (runtime in glibc, tools `msgfmt`/`msginit`) | UI translation catalogs | Runtime in the C library; tools only at build time | Tools: GPL; the libc runtime part is covered by the C library license - check your distribution |

glibc, `libstdc++` and `libgcc_s` come from your system in every package and are not bundled.

## Bundled in the Linux packages (RPM, DEB, TGZ) and the Windows and macOS packages: Qt

| Component | Use | Linking | License |
|---|---|---|---|
| **Qt 6.9.3** (Core, Gui, Widgets, Svg, DBus, Wayland client, XCB platform plugin and the plugins listed in `scripts/bundle-qt-linux.sh`) | The user interface `dofscroll-ui` | **Dynamically linked, never statically**; in `/usr/lib/dofscroll/lib` (Linux) so users can replace the libraries with their own build of the same Qt version | LGPL-3.0-only (the Qt modules used; text in `third-party/Qt-LGPL-3.0-only.txt`) |
| ICU 73.2 (`libicu*`, shipped by the Qt binaries) | Unicode support of Qt | Dynamically linked, bundled with Qt | Unicode License ("ICU License", `third-party/ICU-73.2-LICENSE.txt`) |

Qt: the version is the one in `packaging/qt.env` (all packages use the same). The corresponding source
code of the exact version is available from the Qt project: https://download.qt.io/archive/qt/ (for
example `qt/6.9/6.9.3/submodules/`), https://code.qt.io and https://www.qt.io/download-open-source.
The Qt libraries come unmodified from the Qt binary distribution (`download.qt.io`, installed with
aqtinstall). You may replace them: on Linux copy your own build of the same Qt version over the
libraries in `/usr/lib/dofscroll/lib` and the plugins in `/usr/lib/dofscroll/plugins`. Qt also contains
third-party code (for example PCRE2, double-conversion, harfbuzz parts); its licenses are listed in the
Qt documentation, "Licenses Used in Qt" (https://doc.qt.io/qt-6/licenses-used-in-qt.html). The Windows
and macOS packages bundle the Qt libraries in the same way (`windeployqt`, `macdeployqt`).

## Bundled in the Linux packages only: libraries that Qt needs

Copied unmodified (stripped, with a relative search path) from the oldest supported build
distribution (RPM and TGZ: Rocky Linux 9, DEB: Debian 12), so that `dofscroll-ui` starts on a system
that has nothing installed but the C library. All are dynamically linked and located in
`/usr/lib/dofscroll/lib`; the license files of the distribution packages are in
`/usr/share/doc/dofscroll/third-party/<package>/`. Source code: the source packages of the build
distribution (Rocky Linux 9: https://dl.rockylinux.org/pub/rocky/9/ source repositories; Debian 12:
https://sources.debian.org) and the upstream projects named in `bundled-libraries.txt`.

| Library | License (upstream, as stated by the Rocky Linux 9 package; the DEB build carries the Debian equivalents, see its `copyright` files) |
|---|---|
| libX11, libX11-xcb, libXext, libXau, libxcb (and the libxcb-* extension libraries), xcb-util, xcb-util-cursor, xcb-util-image, xcb-util-keysyms, xcb-util-renderutil, xcb-util-wm, libxkbcommon, libxkbcommon-x11, libwayland-client, libwayland-cursor, libwayland-egl | MIT / X11 |
| libglvnd (libGL, libGLX, libEGL, libOpenGL, libGLdispatch) | MIT / X11 style; **this software is based in part on the work of the Khronos Group** |
| fontconfig | MIT |
| freetype | FreeType License (FTL) or GPLv2+ (used under the FTL); parts BSD, MIT, zlib |
| harfbuzz | MIT |
| graphite2 | LGPLv2+ or GPLv2+ or MPL-1.1 (dynamically linked) |
| libpng | libpng license (zlib style) |
| zlib | zlib license |
| libzstd, bzip2, libbrotli, xz (liblzma), lz4 | BSD / MIT / public domain (zstd: BSD or GPLv2, used under BSD) |
| libxml2 | MIT |
| glib2 (libglib, libgthread) | LGPLv2.1+ (dynamically linked) |
| pcre | BSD |
| libffi | MIT |
| dbus (libdbus-1) | AFL-2.1 or GPLv2+ (dynamically linked) |
| systemd (libsystemd), libcap, libgcrypt, libgpg-error | LGPLv2.1+ (libcap: BSD or GPLv2; dynamically linked) |

The LGPL libraries (Qt, glib, libgcrypt, libgpg-error, libsystemd, graphite2) are used through dynamic
linking only, so users can replace them with modified versions: put the replacement into
`/usr/lib/dofscroll/lib` (the interface finds its libraries there through a relative RPATH and does not use
the libraries of the system). If you ship statically linked binaries, the LGPL conditions change -
check them before doing so.

## Other notes

- `spacenavd` runs as its own system service and is **not** part of dofscroll; dofscroll
  communicates with it through the `libspnav` socket protocol.
- "3Dconnexion" and "SpaceMouse" are trademarks of their respective owners. dofscroll is
  independent and not affiliated with or endorsed by 3Dconnexion; no vendor code or driver is
  included (see the section on the optional driver interface below).
- Maintainers: when Qt is bumped (`packaging/qt.env`) check that the ICU version and the license file
  `third-party/licenses/ICU-<version>-LICENSE.txt` still match the ICU libraries that the new Qt ships
  (`docs/release-process.md`).


## Optional: 3Dconnexion driver interface (Windows, macOS)

On Windows and macOS, dofscroll can read the SpaceMouse through the programming interface of the
**official 3Dconnexion driver, when that driver is installed** (setting `INPUT`, default
`auto`); otherwise, or when you choose it, it reads the device directly as raw HID.

- **Nothing is bundled.** No vendor header, library, binary or documentation is in the repository
  or in any dofscroll package, and the vendor SDK is not linked at build time. dofscroll loads the
  interface from the installed driver at run time: on macOS the `3DconnexionClient` framework
  (`dlopen`, fixed path `/Library/Frameworks/3DconnexionClient.framework`), on Windows the COM
  object `TDxInput.Device` (late binding, registered machine-wide, installed below Program
  Files). The declarations in the source were written by the dofscroll project from public
  knowledge, with the public source named next to each; see `docs/legal-notes.md`.
- **Rights.** The driver, its interface and the names "3Dconnexion" and "SpaceMouse" belong to
  3Dconnexion, a Logitech company. dofscroll is not affiliated with or endorsed by 3Dconnexion.
  No permission was requested from 3Dconnexion; the project says so openly in `docs/legal-notes.md`.
- Linux: not used; spacenavd is the driver there.
- The active path is shown as "Input: 3Dconnexion driver" or "Input: direct (HID)".
