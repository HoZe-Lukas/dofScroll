# Installation layout

`cmake --install` and the CPack packages (RPM, DEB, TGZ) install:

| What | Where |
|---|---|
| `dofscroll`, `dofscroll-tray` | `<prefix>/bin` |
| systemd user units | `<prefix>/lib/systemd/user` |
| udev rule (uinput) | `<prefix>/lib/udev/rules.d` |

The units and the udev rule always go to `lib`, never `lib64` (also on RHEL-family
systems where `CMAKE_INSTALL_LIBDIR` is `lib64`), because systemd and udev do not read
`lib64`. The unit files are rendered from `systemd/*.service.in` at install time, so
`ExecStart=` points at the real `<prefix>/bin` (`/usr/bin` for packages, `/usr/local/bin`
for the default prefix). All sandbox directives are identical to the template.

Developer workflow: `cmake --build build --target install-user-links` prints the symlink
commands. The user units are linked from `build/systemd-dev/`, which is generated at
configure time with `/usr/local/bin` paths (matching the binary symlinks of that flow).

`ctest` includes `install_layout`, which installs into a temporary `DESTDIR` for the
prefixes `/usr` and `/usr/local` and checks this layout. RPM and DEB generation itself is
not covered by it; the CI release job is its first real test.
