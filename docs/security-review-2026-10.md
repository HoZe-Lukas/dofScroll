<!--
SPDX-License-Identifier: GPL-3.0-or-later
SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
-->
# Security review, October 2026 (report only)

Scope: reading of the code on `main` (commit 11b4678) for the seven areas listed below.
No source code was changed. Cheap suspicions were verified with throw-away programs outside
the repository (linking `src/modes.cpp` and `src/config.cpp`; a CPack TGZ build in a scratch
directory; `systemd-analyze --user security --offline=true` on the rendered units; nothing
was started or modified). Hardware behaviour (real uinput/XTest, a real X server, a real
SpaceMouse) was **not** tested; findings that depend on it are marked SUSPECTED.

Labels: **CONFIRMED** = reproduced by a test or unambiguous from the cited code;
**SUSPECTED** = plausible from the code but depends on environment/hardware not available here.

Threat actors are those of `docs/security.md` section 1: A1 other local user, A2 process of
the same user, A3 malicious USB device, A4 a config or mode file taken over from a stranger.

## Summary

| Severity | Count | IDs |
|---|---|---|
| High | 2 | F1, F2 |
| Medium | 4 | F3, F4, F5, F6 |
| Low | 8 | F7 - F14 |
| Info | 4 | I1 - I4 |

Overall: the file loaders (modes, config, seeding, runtime files) are carefully written
(`O_NOFOLLOW`, `fstat` on the fd, bounded reads, `openat`/`renameat`, owner and mode checks)
and I found no memory-safety or path-traversal problem in them. The two High findings are in
packaging and in the trust model around mode files, not in parsing.

---

## High

### F1 - Packaged systemd units contain the CPack staging path (CONFIRMED for TGZ, SUSPECTED for RPM/DEB)

- Where: `cmake/InstallUnits.cmake:7-12` (derives `DOFSCROLL_BINDIR` from `CMAKE_INSTALL_PREFIX`
  at install time), `packaging/cpack/DofscrollCPack.cmake` (`CPACK_PACKAGING_INSTALL_PREFIX /usr`
  is not what `CMAKE_INSTALL_PREFIX` holds while CPack stages the files).
- Test: `cpack -G TGZ` in a scratch build. The packaged
  `usr/lib/systemd/user/dofscroll.service` contains
  `ExecStart=<build dir>/_CPack_Packages/Linux/TGZ/dofscroll-1.0.1-Linux/usr/bin/dofscroll` and the
  same for `ExecStopPost`. `rpmbuild`/`dpkg-deb` are not installed here, so RPM and DEB could not be
  checked; they stage the same way, so they are very likely affected.
- Impact: (a) the released service cannot start on a user's machine (path does not exist); (b) the
  builder's absolute path (CI runner path, or a developer's home directory on a local build) is
  embedded in a public release artifact; (c) `ExecStopPost=--release-keys`, the stuck-key safeguard
  of F4, silently does nothing.
- Scenario: a user installs the release RPM, runs `systemctl --user enable --now dofscroll`, gets
  status 203/EXEC. Anyone who can create the staging path on the target (unlikely for `/__w/...`,
  trivial for a `/tmp/...` local build) would get their binary executed as the user.
- Fix: before the first release, install the units with the final path baked in. For packages use
  `/usr/bin` explicitly (for example render the unit at configure time with
  `${CMAKE_INSTALL_FULL_BINDIR}` and `install(FILES ...)`, and set `CMAKE_INSTALL_PREFIX` to `/usr`
  for the package builds). Add a CI step that unpacks the package and fails if any shipped file
  contains `_CPack_Packages` or the string `/__w/`.

### F2 - A mode file can inject Ctrl/Alt/Super chords and shortcuts without any user-visible warning (CONFIRMED)

- Where: `src/modes.cpp:226-251` (`key:<name>`), `src/modes.cpp:201-224` (`keys:<neg>,<pos>`),
  `src/keys.hpp` (closed list incl. Ctrl, Alt, Super, Delete, Esc, F1-F12, A-Z, 0-9),
  `src/mode_runner.cpp:154,226` (Ctrl is held while any channel zooms),
  `src/mode_runner.cpp:243` (`tapKeys` fires on another channel in the same tick).
- Test: a mode file with `BUTTON_2=key:ctrl+alt+delete` is rejected (single keys only), but
  `BUTTON_3=key:super`, `key:alt`, `key:ctrl`, `key:delete` and `key:f1` all load without a warning.
- Impact: chords are still reachable, because modifiers are separate keys and Ctrl is added by the
  zoom logic:
  - `V_ACTION=zoom` + `H_ACTION=keys:w,w`: any diagonal push sends **Ctrl+W** (close tab);
    `keys:q,q` sends **Ctrl+Q** (quit); `keys:a,c`, `v`, `z` and so on likewise. No Ctrl button
    is needed, the user only moves the puck.
  - `BUTTON_1=key:ctrl`, `BUTTON_2=key:alt`, twist `keys:f1,f2`: Ctrl+Alt+F1/F2 switches the VT
    (a user is dropped out of the graphical session), with a third button Ctrl+Alt+Delete or
    Super+L (lock) style shortcuts. Ctrl+Alt+Backspace on setups where X zap is enabled.
  - `BUTTON_n=key:enter` / `keys:enter,enter` into a focused terminal: auto-repeating Enter at the
    rate of the axis accumulator (pager, `y/n` prompts, dangerous confirmations).
  - Where the daemon uses the uinput backend (Wayland) the same events reach the lock screen and
    other sessions on the seat (see F3).
- Limits (why this is High and not Critical): a mode file cannot type text sequences, cannot send
  codes outside the list (the list does hold the media keys and `print`, which is the same key code as SysRq on Linux;
  see `docs/security.md`), and every effect needs the physical
  device to be moved or pressed. It is a social-engineering vector for shared mode files (A4), the
  scenario `docs/security.md` section 7 explicitly invites ("modes may be shared").
  The doc's claim that this "does not exceed what A2 can do via XTest" is wrong for A4 (a stranger's
  file has no XTest access) and for the uinput backend.
- Mitigation proposals, cheapest first:
  1. Document it prominently in `readme.md` next to the mode format and in a header comment of every
     shipped example: "a mode file can press keys; read it before use". (Before the repo goes public.)
  2. Loader warning: for every mode that binds a modifier key (`ctrl`, `alt`, `super`, `shift`) via
     `key:`, or uses a letter/digit/function/delete key in `keys:` while another channel of the same
     mode can hold Ctrl (zoom), emit one `Mode <id>: binds <keys> (shortcut-capable)` line to the log
     and show it in the tray mode list. No behaviour change.
  3. Opt-in gate: a global config key `ALLOW_MODIFIER_BINDINGS=0` (default) in `dofscroll.conf` (never
     overridable from a mode file) makes the loader drop `key:ctrl|alt|super` bindings and refuse
     `keys:` pairs whose members are not navigation/media-like (arrows, page, home/end, space, enter, esc,
     +/-). Seeded examples must then only use the allowed set; users who want more flip the switch once.
  4. Optional: never hold Ctrl for the zoom channel while a `keys:` action of another channel fires in
     the same tick (suppress those taps), which removes the "Ctrl+W by diagonal push" class entirely.

---

## Medium

### F3 - The packages install the uinput udev rule unconditionally (CONFIRMED)

- Where: `cmake/Systemd.cmake` (globs `packaging/*.rules` into `lib/udev/rules.d`),
  `packaging/70-dofscroll-uinput.rules` (`MODE="0600", TAG+="uaccess"`).
- Impact: merely installing the RPM/DEB gives the user of the active local session an ACL on
  `/dev/uinput`, even if they only use X11. Any process of that user (A2: a browser exploit, a
  malicious npm/pip script) can then create a virtual keyboard that works **below** the compositor:
  on Wayland it bypasses all input isolation, and it reaches the lock screen, the login manager and
  VTs. Without the package a compromised same-user process on Wayland has no such capability.
  `docs/security.md` S1 discusses the rule itself, but not that a normal package install activates it.
- Scenario: user on Wayland installs the package for the scroll feature; a later compromised
  user-level process types into an open root terminal or unlocks via the lock-screen password field.
- Fix: ship the rule in `/usr/share/doc/dofscroll/` (or an `-uinput` subpackage) and let
  `readme.md`/`README-uinput.md` tell users to copy it deliberately. Keep `BACKEND=auto` preferring X11.
  Also note the virtual device advertises every key of `keys.hpp` and uses vendor id `0x1209`
  (pid.codes test range), product `0x5350` (`src/output_uinput.cpp:95-100`): register a project id
  or document this as a test id (I4).

### F4 - Stuck keys after SIGKILL, crash or a stalled tick thread under X11/XTest (SUSPECTED)

- Where: `src/output_x11.cpp:63-90` (keys held via `XTestFakeKeyEvent`),
  `src/mode_runner.cpp:154,226` (Ctrl held while zooming), `systemd/dofscroll.service.in:8`
  (`ExecStopPost`), no `WatchdogSec`.
- Impact: X does not release keys held through XTest when the client disconnects (known XTest
  behaviour; the existing doc S3 states the same). `ExecStopPost=--release-keys` covers a crash or
  kill **only under systemd** and only if F1 is fixed; a run from a terminal (`dofscroll &`, the
  readme's quick start) has no safeguard. There is also no watchdog: the tick thread calls `XFlush`
  and file/kdotool code (activewin runs in its own thread, good); if the X connection stalls
  with Ctrl held, Ctrl stays down for the whole session until a restart.
- Scenario: user zooms (Ctrl held), the daemon is `kill -9`ed or OOM-killed; Ctrl+W/Q on every
  keystroke until the restart one second later. With the uinput backend the kernel releases the
  keys on device removal (`UI_DEV_DESTROY` / fd close), so this is X11 only.
- What `--release-keys` does (`src/dofscroll.cpp:66-79`): loads the default config, opens its own
  output for the configured backend and calls `forceReleaseAll()`, which sends key-up for every key
  in the table, the right-hand modifiers and mouse buttons 1-3 regardless of its own state. Side
  effects: it also releases keys the user is physically holding at that instant (harmless), and
  with `BACKEND=uinput` it creates a short-lived virtual device (300 ms sleep) to do so.
- Fix: add `WatchdogSec=` with `sd_notify` pings from the tick loop (or a tick-lateness check that
  releases everything when a tick is more than ~500 ms late); on startup path log a note when run
  outside systemd. Keep `ExecStopPost`.

### F5 - A mode file can switch the daemon off with no way back on the device (CONFIRMED by code reading)

- Where: `src/modes.cpp:156-164` (any global key is accepted as an override, including `ENABLED`,
  `BACKEND`, `TICK_HZ`), `src/daemon_core.cpp:299-306` (overrides applied), `src/daemon_core.cpp:483-499`
  (disabled means `buttons = 0` and no output).
- Test: `ENABLED=0`, `BACKEND=uinput` and `TICK_HZ=1000` are all accepted as overrides of a mode file.
- Impact: a mode reachable through `BUTTON_n=mode:<id>` or `mode:next` containing `ENABLED=0` leaves the
  user in a state where the device does nothing and its buttons are ignored, so the "next" button
  cannot be pressed again. Recovery needs the tray or a reload (a reload drops the button-selected
  mode, `src/daemon_core.cpp:214`). It is a denial-of-service/confusion vector for shared modes,
  not an escalation. `BACKEND` as an override is only logged as a note (`daemon_core.cpp:337`) and
  has no effect; `TICK_HZ` is clamped to 30-1000.
- Fix: restrict mode overrides to a whitelist of per-channel tuning keys (sensitivity, deadzone,
  curve, sign, zoom parameters); reject `ENABLED`, `BACKEND`, `TICK_HZ`, `MODE`, `BUTTON_*`
  (the last two are already special-cased). Alternatively keep buttons live in the disabled state
  for `mode:` bindings.

### F6 - Release pipeline: unpinned inputs, no provenance, release-job checkout keeps the write token (CONFIRMED)

- Where: `.github/workflows/ci.yml:22,170` (`actions/download-artifact@v4.3.0` by tag, the file
  itself notes this), `ci.yml:28,51,69,101,129` (`rockylinux:9`, `debian:12` floating tags),
  `ci.yml:160` (checkout in the `contents: write` job keeps `persist-credentials: true`),
  `ci.yml:174-184` (checksums created by the same job that publishes).
- Good: top-level `permissions: contents: read`, write only in `release`; all `uses:` except one are
  SHA-pinned; no `${{ }}` is interpolated into a `run:` script (only `github.token` in `env:` and
  `github.ref`/`workflow` in `concurrency`); the ref name reaches shell only as a quoted
  `$GITHUB_REF_NAME`; the tag is validated against the CMake version. `pull_request` runs with a
  read-only token and no secrets.
- Impact: (a) a retagged or compromised `download-artifact@v4.3.0` tag or base image runs with
  `contents: write` in the release job and could swap the packages; (b) `SHA256SUMS.txt` is published
  next to the files by the same workflow, so it protects against corruption only, not tampering;
  (c) any collaborator who can push a `v*` tag publishes a release; (d) the `release` job
  keeps the checkout token in `.git/config` during later steps.
- Fix: pin `download-artifact` to its commit SHA; add `persist-credentials: false` to every checkout;
  add `actions/attest-build-provenance` (or sigstore/cosign) for the release assets and mention
  verification in the readme; protect `v*` tags with a tag ruleset and gate the `release` job with an
  `environment:` that requires approval; pin container images by digest (accept the maintenance cost
  or keep floating tags only for the non-release jobs).

---

## Low

### F7 - Warnings and error texts are not sanitized before they reach the log (CONFIRMED)

- Where: `src/daemon_core.cpp:221-225` (`Config: <warning>`), `daemon_core.cpp:231-236` (mode
  warnings), built in `src/modes.cpp:134,146,151,158` and `src/config.cpp:330` (`unknown key '<raw>'`).
- Test: a mode file with a line `<ESC>]0;PWN<BEL>=1` produced a warning containing the raw ESC and BEL
  bytes; likewise `BUTTON_1=key:<ESC>[2J`.
- Impact: raw control characters from a mode/config file reach stderr, the journal and
  `$XDG_RUNTIME_DIR/dofscroll.log` (a file shown in the tray, which renders plain text, so no markup
  injection). `journalctl` and `systemctl status` print non-printable bytes escaped, so terminal
  injection is limited to tools that cat the log file. The file's author is a mode-file stranger (A4)
  at best. It contradicts the claim of `docs/security.md` section 8 ("all foreign values go through
  `sanitizeForLog`").
- Fix: wrap every warning in `sanitizeForLog` at the two `std::cerr` sites in `daemon_core.cpp` (one
  place each) rather than in every parser message.

### F8 - Mode and config files: no permission check, config loader has no owner check (CONFIRMED)

- Where: `src/modes.cpp:289` (owner only), `src/config.cpp:863-880` (no `st_uid` test),
  `src/config.cpp:1067-1083` (existing mode bits are carried over on save, up to 0777).
- Impact: a mode file that is group- or world-writable (for example extracted from an archive with
  `umask 0`, or a directory shared through a group) can be edited by another local account and the
  daemon still trusts it, since only the owner is compared. A group-writable *directory* lets another
  user delete or rename the user's files, though not create files owned by the user. The main
  config has neither check, and saving preserves a world-writable mode.
- Fix: apply the same rule as the runtime files: reject (warn) files with `st_mode & 022`, and when
  saving an existing file that has `& 022`, tighten to `& ~022`.

### F9 - Embedded NUL bytes are accepted in values and overrides (CONFIRMED)

- Where: `src/modes.cpp:99-125` (lines are `std::string`, NUL kept), `src/config.cpp` value parsers
  (`strtod`/`strtol` on `c_str()`).
- Test: `SCROLL_V_SENS = 3<NUL>junk` is accepted and stored as the 7-byte string; `BUTTON_1=key:space<NUL>x`
  is correctly rejected (the exact-match key parser).
- Impact: validation and stored value differ (`3` is validated, `3\0junk` is stored in
  `overrides` and may be written back by the tray's save path or logged). No memory-safety effect found.
- Fix: reject any file containing a NUL byte once, after the bounded read (both loaders), with a warning.

### F10 - `ReadWritePaths=%t` gives the daemon write access to the whole runtime directory (CONFIRMED)

- Where: `systemd/dofscroll.service.in:43`.
- Impact: a compromised daemon could replace sockets and files of other applications in
  `/run/user/UID` (D-Bus, PipeWire, Wayland socket, gpg-agent socket) and so mount man-in-the-middle
  attacks on them. The daemon needs only two files there.
- Fix: move the status/log files to `$XDG_RUNTIME_DIR/dofscroll/` created via `RuntimeDirectory=dofscroll`
  in the daemon unit and `ReadWritePaths=%t/dofscroll`; the tray reads that subdirectory (small code
  change in `runtimeFilePath` and `suspendFilePath` callers). `systemd-analyze` does not score this.

### F11 - Sandbox compared with real needs (CONFIRMED by analysis)

- `systemd-analyze --user security --offline=true` on both rendered units: exposure **2.1 OK**
  each. The tool misreports "runs as root" for user units in offline mode, so the number is only a
  rough guide. Remaining open items it lists are all justified in the unit comments (`PrivateDevices`
  and `PrivateTmp` break uinput and the X11 socket; `PrivateNetwork` breaks abstract X sockets).
- Daemon: needs X11 and spacenavd sockets, `/dev/uinput`, read access to `~/.config`, writes to two
  runtime files. Directives are consistent with that. `DeviceAllow=` is not set: adding
  `DeviceAllow=/dev/uinput rw` plus `DevicePolicy=closed` is not possible without `PrivateDevices`
  semantics, so leave it. `/dev/input/event*` and `/proc/bus/input/devices` stay readable by the
  reachable user's own rights only.
- Tray: `RestrictAddressFamilies` includes `AF_NETLINK` (GLib/GIO needs it for network monitoring;
  untested if it can go). `MemoryDenyWriteExecute` is off for GLib/PCRE2 JIT as documented; try
  `MemoryDenyWriteExecute=yes` once on the target distro (cheap test, not done here). The tray cannot
  save the config if `~/.config/dofscroll/dofscroll.conf` is a symlink into a dotfiles repository,
  because the save writes next to the resolved target (`src/config.cpp:1024-1049`), outside
  `ReadWritePaths`: a functional limitation, not a security issue; document it.
- The tray's `systemd-run --user` for "Open configuration file" (`src/dofscroll_tray.cpp:2090-2097`)
  and the AF_UNIX D-Bus access make the tray sandbox an accident-containment measure, not a security
  boundary (already documented in `docs/security.md` section 3).

### F12 - Seeding trusts an examples directory chosen by environment or by the binary's location (CONFIRMED)

- Where: `src/seed.cpp:134-149` (`$DOFSCROLL_EXAMPLES`, `<exe_dir>/examples`,
  `<exe_dir>/../share/dofscroll/examples`).
- Impact: seeded mode files come from a directory that is not necessarily root-owned. Running the
  tray from an unpacked tarball or a build tree seeds that tree's examples; an attacker who controls
  the unit's environment is already A2. The copy itself is safe: sources are `O_NOFOLLOW`, regular,
  <= 64 KiB, at most 64 files, destinations are fixed names, `link()` never overwrites, and the modes
  directory appears atomically by `rename`. `mkdirs` (seed.cpp:42) follows symlinks in parents (A2 only).
- Fix: skip candidates that are group/world-writable or not owned by root/the user; ignore
  `DOFSCROLL_EXAMPLES` unless `--debug`. Mention the first-start seeding of shared modes in the readme.

### F13 - `hid_descriptor.cpp` is not wired in, and has weaker limits than the other loaders (CONFIRMED)

- Where: `src/hid_descriptor.cpp:234-257` (`loadHidDescriptorDir`): `grep` finds no caller outside
  the file, so today it is dead code in the daemon.
- Impact when wired in: `readdir` is not capped before sorting (a directory with millions of `.conf`
  entries exhausts memory, unlike `modes.cpp:27`), no owner or regular-file-in-directory check beyond
  `O_NOFOLLOW`/`S_ISREG`, the `name` value allows 0x7f and invalid UTF-8
  (`hid_descriptor.cpp:132-134`), and `scale` accepts any finite non-zero double so
  `value * scale` can overflow to infinity (SUSPECTED, not run). The decoder itself bounds-checks
  offsets against the payload (`:268,278`).
- Fix: when the loader is connected, copy the modes.cpp pattern (entry cap, owner check, `sanitizeForLog`
  for the name, clamp the scale to a sane range and check `isfinite` of the product).

### F14 - Runtime files: predictable temp names inside a trusted directory (CONFIRMED, minor)

- Where: `src/platform/linux/runtime_files.cpp:152-154` (`dofscroll.log.tmp`, `unlink` then `O_EXCL`),
  `:250-256` (`dofscroll-status.tmp.<pid>`, `unlink` on `EEXIST`).
- Assessment: the directory is verified to be owned by the user and not group/other-writable
  (`runtimeDirTrusted`, `:26-30`), files use `O_NOFOLLOW`, mode 0600, an owner and mode check on
  the log file (`:66-73`), and the tray re-checks everything on read (`src/dofscroll_tray.cpp:2112-2116`).
  Symlink races therefore need A2 write access to the user's own runtime directory, which already
  implies the same rights as the daemon. `runtimeDirTrusted` uses `stat` (follows a symlink),
  acceptable since `XDG_RUNTIME_DIR` is set by logind. Log text is mirrored with line timestamps from
  `std::cerr`; see F7 for the content. No finding beyond hygiene: consider a random suffix.

---

## Info

- **I1 - Parser limits hold up (CONFIRMED by test).** In a directory with a symlink to `/etc/passwd`,
  a FIFO, a 70 000 byte file, a 5 000 byte line and an embedded-NUL name: the symlink is refused
  (`ELOOP`), the FIFO is opened `O_NONBLOCK` and rejected as not regular, the large file is ignored,
  the long line is skipped, the label is cleaned and capped. Directory scan is capped at 4096 entries,
  64 files, 128 overrides, 200 warnings. The ownership check is on the opened fd (no
  stat/open TOCTOU); only the directory is opened twice (`opendir` in `listConfNames`, `open` in
  `loadModes`), harmless because every file is re-validated after `openat`.
- **I2 - spacenavd socket trust.** The daemon connects to the root-owned `/run/spnav.sock` through
  libspnav and trusts the event stream; axis values are bounded by DSP and the S2 fixes. The socket is
  world-writable by spacenavd's design, so any local user can connect and read device motion (see S9);
  nothing in this repository can change that. A malicious USB device (A3) controls the device name
  only, which is sanitized (`daemon_core.cpp:122`, `sanitizeForLog`).
- **I3 - Reload path.** `systemctl --user kill -s HUP` is started with `execlp`/`g_spawn_async`
  using a `PATH` lookup (`src/platform/linux/linux_platform.cpp:75`, `src/dofscroll_tray.cpp:104,108,2097`).
  The environment is the user's own; a hostile `PATH` means A2. The tray resolves `systemd-run` to an
  absolute path first; doing the same for `systemctl` and `xdg-open` would be consistent. SIGHUP only
  sets a flag (S7 in `docs/security.md`).
- **I4 - uinput identity.** The virtual device is `dofscroll virtual input`, bus virtual, vendor
  `0x1209`, product `0x5350` (`src/output_uinput.cpp:95-100`); `0x1209` is the shared pid.codes
  prototyping vendor id. Harmless, but another project can claim the same pair. `/dev/uinput` is opened
  `O_CLOEXEC` (S12). No `--release-keys` interaction beyond F4.

---

## Documentation claims that the code does not support (area 7)

| Claim in `docs/security.md` | Reality |
|---|---|
| Section 8: "All foreign values (device name, `WM_CLASS`, profile description) go through `sanitizeForLog`" | True for those three, but config and mode warnings (file content) are logged raw (F7). |
| Section 7: a mode file "does not exceed what A2 can do under X11 via XTest anyway" | Wrong for A4 (a stranger's file) and for the uinput backend; also omits the Ctrl-leaks-into-`keys:` chords (F2) and the `ENABLED=0` lock-out (F5). |
| Section 7: "Not yet audited in depth" | Superseded by this review; update the section and point here. |
| Section 3 / S3: `ExecStopPost=-/usr/local/bin/dofscroll --release-keys` after every stop | Only for the dev install. Packaged units embed the staging path (F1) and the safeguard does not run. |
| Section 6: "No high or critical findings" | Predates the packaging and the modes feature; F1 and F2 are new. |
| Section 7: seeding "Destination paths are derived from the fixed configuration directory" | Correct; the missing part is that the *source* directory may come from `DOFSCROLL_EXAMPLES` or the executable's location (F12). |
| Section 3: sandbox score claims | Offline `systemd-analyze` shows 2.1 OK, but reports root for user units, so treat the score as indicative only (F11). |

---

## Prioritized fix list

### Before the repository is made public

1. **F1** fix the unit path in packages and add the CI check for `_CPack_Packages` / runner paths
   (blocker: the first release would not start and leaks the builder path).
2. **F2** documentation in `readme.md` and in the example mode files that mode files can press any
   key; add the loader warning for modifier-capable bindings (proposal 2). Decide on the opt-in
   gate (proposal 3) before the first release if modes are advertised as shareable.
3. **F3** stop installing the uinput udev rule by default (doc-only or subpackage).
4. **F6** pin `download-artifact` by SHA, `persist-credentials: false`, tag protection ruleset;
   provenance attestation can follow in 0.2.
5. Correct the doc claims in the table above (cheap, avoids a credibility problem).
6. **F5** reject `ENABLED`, `BACKEND`, `TICK_HZ` as mode overrides (smallest possible change, closes
   the lock-out).

### Can wait

- **F4** watchdog (needs `sd_notify`; document the limitation and `--release-keys` meanwhile).
- **F7, F9** sanitize warnings and reject NUL in one place each.
- **F8** permission checks (`& 022`) in both loaders.
- **F10** runtime subdirectory; **F11** `MemoryDenyWriteExecute` trial for the tray.
- **F12, F13** harden the seed source and the HID descriptor loader before it is wired in.
- **F14, I3, I4** hygiene.
