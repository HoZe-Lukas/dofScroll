<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Third-party notices

dofscroll itself is licensed under the GNU General Public License, version 3 or (at your option)
any later version (`GPL-3.0-or-later`, see `LICENSE`). It does not bundle any third-party source
code; it links against or talks to the components below, which keep their own licenses.

This list gives license information only as far as it is certain. The exact license of a
component depends on the version and the distribution package you build against or ship - **check
the license of the version you ship** (package metadata or the upstream license file) before
redistributing binaries.

| Component | Use in dofscroll | Linking / relationship | License |
|---|---|---|---|
| `libspnav` | Client library to read events from `spacenavd` (daemon) | Linked at build time (`libspnav-devel`) | Free software license (upstream: BSD-style); check the license of the version you ship |
| `spacenavd` | Hardware driver service that talks to the SpaceMouse | **Separate process**, not linked; dofscroll only connects to its socket | GPL (upstream); check the license of the version you ship |
| Xlib (`libX11`), `libXtst` | X11 connection, XTest event injection, active-window query | Dynamically linked | MIT/X11-style licenses (upstream); check the license of the version you ship |
| gettext (runtime in glibc, tools `msgfmt`/`msginit`) | UI translation catalogs | Runtime in the C library; tools only at build time | Tools: GPL; the libc runtime part is covered by the C library license - check your distribution |
| Qt 6 (Widgets, Core, Gui; optional Svg) | Cross-platform user interface `dofscroll-ui` | Dynamically linked, never statically | LGPL-3.0 (Qt Widgets/Core/Gui/Svg modules; some other Qt modules are GPL/commercial and are not used) |

Notes:

- The LGPL libraries are used through dynamic linking only, so users can replace them with
  modified versions. If you ship statically linked binaries, the LGPL conditions change - check
  them before doing so.
- Qt 6: the licence text and the corresponding source code come from the Qt project (https://www.qt.io/download-open-source, https://code.qt.io) or from your distribution's source packages. Bundled Windows/macOS builds must ship the LGPL-3.0 text and an offer or link to the Qt sources for the exact version bundled; users can replace the Qt libraries with modified builds.
- `spacenavd` runs as its own system service and is **not** part of dofscroll; dofscroll
  communicates with it through the `libspnav` socket protocol.
- "3Dconnexion" and "SpaceMouse" are trademarks of their respective owners. dofscroll is
  independent and not affiliated with or endorsed by 3Dconnexion; no vendor code or driver is
  included.
