# Security: review, systemd hardening, commit hook, repository settings

Status: 2026-10-12, sources under `src/`, `packaging/`, `systemd/`. The review itself was purely
documentary; the fixes listed under "Status" came afterwards. Line numbers were removed
(references name file and function); findings are marked "fixed" where the code or rule has
since been adjusted (state of the fixes: 2026-10-15).

Note (0.1.1, #43): the GTK tray (`dofscroll-tray`, `src/dofscroll_tray.cpp`,
`systemd/dofscroll-tray.service`) was removed; the Qt interface `dofscroll-ui` (unit
`dofscroll-ui.service`, same sandbox) is the only tray and settings window. Where this document
says "tray" it describes the review of that program or, for the sandbox, the unit that `dofscroll-ui`
now uses; the findings about saving the configuration, helper processes and the unit sandbox apply
to `dofscroll-ui` in the same way. File references to the removed sources stay for the audit trail.

Contents:

1. Threat model
2. Findings (with file, severity, recommendation)
3. systemd hardening of the user units (what is set, what deliberately is not, how it was checked)
4. Pre-commit hook (`scripts/pre-commit`, `scripts/install-hooks.sh`)
5. Repository settings (branch protection and required checks for `main`)
6. Second audit (2026-10-15) and fixes
7. Mode plugins and first-start seeding
8. Runtime files in the runtime dir (daemon writes, tray reads)

---

## 1. Threat model

**What is to be protected:** the user's graphical session (keyboard/mouse input, i.e. the
*integrity* of input and the *confidentiality* of what is typed), the availability of the
desktop (no "stuck Ctrl", no event floods) and the configuration file.

**Components and trust boundaries:**

| Component | Runs as | Talks to | Inputs coming from outside |
|---|---|---|---|
| `dofscroll` (daemon) | user, systemd user unit | `spacenavd` via Unix socket `/run/spnav.sock` (root, `0777`), X server (XTest) or `/dev/uinput`, optionally `kdotool` (spawned) | axis values/buttons from `spacenavd`, `~/.config/dofscroll/dofscroll.conf` (fallback: legacy paths `~/.config/spnav-scroll/spnav-scroll.conf`, `~/.config/spnav-scroll.conf`), mode files in `~/.config/dofscroll/modes/`, `WM_CLASS` of foreign X clients, environment (`XDG_*`, `HOME`, `PATH`, `DISPLAY`), SIGHUP |
| `dofscroll-ui` (formerly the GTK tray `dofscroll-tray`) | user, systemd user unit | Qt (X11 or Wayland), DBus (StatusNotifier), starts `systemctl --user` and `xdg-open` | GUI input, config file, environment |
| `spacenavd` | root (system service) | USB HID device | device (including the device name from the USB descriptor) |
| udev rule `packaging/70-dofscroll-uinput.rules` (opt-in, not installed by default: shipped under `share/dofscroll/udev`) | - | - | sets the permissions of `/dev/uinput` once the user installs it |

**Attackers considered:**

- **A1 - other local user / system service** (another login, a service account, a container
  process). Must neither read nor remote-control the user's session.
  Relevant: permissions on `/dev/uinput`, `/dev/input/event*`, `/run/spnav.sock`, config path.
- **A2 - process of the same user with restricted rights** (e.g. a compromised program, a
  browser exploit outside a sandbox). Can write the config anyway, make the daemon reload via
  SIGHUP and - under X11 - generate input itself via XTest. What matters here is mainly that
  the daemon cannot be driven into undefined behavior or paralyze the session by a manipulated
  config, and that systemd restricts the services so that a bug in the daemon does not lead to
  more.
- **A3 - attacker with physical access** (prepared USB device): controls the device name and
  axis values that arrive through `spacenavd`.
- **A4 - manipulated config from outside** (the README suggests taking the file over via `scp`
  when moving; profiles and mode files may be shared).

**Classification of XTest/uinput:** under X11, *any* X client can generate arbitrary input via
XTest - the daemon itself creates no new capability there (but see section 7: a mode file from a
stranger has no XTest access of its own, the daemon acts for it). `/dev/uinput`, however, acts **below** the
display server (also on text consoles, in the login manager and in other sessions at the same
seat). Whoever can write to `/dev/uinput` can inject keyboard input into every session active
there - this is the most critical point of the whole setup (see S1).

**Pointer motion:** `Output::moveMouse` (XTest, uinput `REL_X`/`REL_Y`, `SendInput`, CGEvent) is the same
trust class as key injection: whoever can make the daemon move the pointer can also make it click, so a
profile or mode that moves the pointer needs the same trust as one that binds keys. The per-call delta is
capped (`clampMouseDelta`, 127 px per axis), but the cap bounds only one call: the **caller** must limit the
call rate. The uinput virtual device already registered the relative axes (it also produces scroll and
buttons), so pointer motion adds no new capability there.

**SIGHUP:** only triggers a config reload (flag in the handler, work in the main thread). Only
processes of the same user or the user manager can send it - uncritical, especially since the
daemon checks the file for changes every second anyway.

---

## 2. Findings

Severities: **High** = enables attacker A1 to read/inject input; **Medium** = undefined
behavior or paralyzing the session through reachable inputs; **Low** = exploitable only under
additional conditions, or robustness; **Info** = no action needed, for classification.

### Overview

| ID | Severity | Topic | Location |
|---|---|---|---|
| S1 | **High** (rule/README fixed; world-writable `/dev/uinput` on a system is a local misconfiguration to check) | `/dev/uinput` world-writable; the recommendation "add user to group `input`" allows keylogging | `packaging/70-dofscroll-uinput.rules`, `packaging/README-uinput.md` |
| S2 | **Medium** (fixed) | Unbounded numbers in config → Inf/overflow → UB on `double`→`int`, event floods | `src/config.cpp`, `src/dsp.cpp`, `src/output_x11.cpp` |
| S3 | **Medium** (fixed) | "Stuck" Ctrl key: blocking calls in the tick thread, crash/kill under X11, discarded key-up events with uinput | `src/activewin.cpp`, `src/dofscroll.cpp`, `src/output_uinput.cpp` |
| S4 | Low | `loadProfiles`: no limits on file size, line length, sections; a FIFO/device file blocks the daemon | `src/config.cpp` |
| S5 | Low | Saving: predictable temp name without `O_EXCL`/`O_NOFOLLOW`, no `fsync`, file mode from umask, lost update (TOCTOU) | `src/config.cpp`, `src/dofscroll_tray.cpp` |
| S6 | Low (fixed) | `popen("kdotool ...")`: shell + PATH search, blocks the tick thread; now `execl` without a shell, a fixed list of system directories instead of `PATH` (`src/platform/exec_lookup.hpp`) and a time limit | `src/activewin.cpp` |
| S12 | Low (fixed) | `/dev/uinput` fd without `O_CLOEXEC`: inheritable by the `kdotool` child process | `src/output_uinput.cpp` (`init`) |
| S7 | Low | Shutdown race with the detached reader thread; signal handlers installed late | `src/dofscroll.cpp` |
| S8 | Low | Binaries under `/usr/local/bin` are root-owned symlinks into a user-writable home | `readme.md` (install), `/usr/local/bin/dofscroll*` |
| S9 | Low | `spacenavd` socket `0777`: other users can read movements/reconfigure the device | `/run/spnav.sock` (system configuration) |
| S10 | Low (fixed) | Tray: PATH search for `systemctl`/`xdg-open`; editor inherits the sandbox | `src/dofscroll_tray.cpp` |
| S11 | Low | Config path from environment without checks (relative `XDG_CONFIG_HOME`, missing `HOME`) | `src/config.cpp` |
| I1-I6 | Info | Positives and classification (reader-thread atomics, `findIconDir`, buffers, WM_CLASS spoofing, log injection) | see below |

### S1 (High) - permissions on `/dev/uinput` and group `input`

**Packaging status:** the udev rule is no longer installed by default. Packages ship it under
`share/dofscroll/udev`; users who need uinput (Wayland) copy it to `/etc/udev/rules.d` and reload
udev, see [installation.md](installation.md). Under X11 nothing needs `/dev/uinput`.

**Status (2026-10-15):** findings b and c **fixed** - `packaging/70-dofscroll-uinput.rules`
now reads `KERNEL=="uinput", MODE="0600", OPTIONS+="static_node=uinput", TAG+="uaccess"`
(no `GROUP="input"` any more; access only through an ACL for the user of the active local
session), `packaging/README-uinput.md` removes the `usermod -aG input` step and explains the
keylogging risk of group `input`; the error message in `UinputOutput::init` no longer points
to the group. Finding a (a world-writable `0666` `/dev/uinput`) is a system configuration
outside the repo and **remains to be checked manually** on every machine.

- **Finding a:** if `/dev/uinput` is `0666` (typically caused by another udev rule under
  `/etc/udev/rules.d` or `/usr/lib/udev/rules.d`, e.g. from input-remapping tools), **every
  local account and every system service** (A1) can create a virtual keyboard and inject
  keystrokes into the currently active session, a root console or the lock screen (e.g. type
  commands into an open admin terminal). That is effectively a local privilege escalation.
- **Finding b:** an earlier version of `packaging/README-uinput.md` recommended
  `usermod -aG input "$USER"`. But group `input` also has **read access to all
  `/dev/input/event*`** (`root:input 0660`). Every process of the user (A2) could then read all
  keyboards system-wide - including password entry in `sudo`, on the lock screen or on the
  console, independent of X11/Wayland isolation.
- **Finding c:** the rule combined `GROUP="input", MODE="0660"` with `TAG+="uaccess"`.
  `uaccess` alone is already sufficient (ACL for the user of the active session at the seat);
  the group additionally made access permanently session-independent (also via SSH, also for
  background processes).
- **Recommendation:**
  1. Reset `/dev/uinput` to `0660`/`root:root` (or by rule) on affected systems (find the
     cause of `0666`: another udev rule).
  2. Reduce the rule to `KERNEL=="uinput", OPTIONS+="static_node=uinput", TAG+="uaccess"`
     (without `GROUP`/`MODE` - the udev default is `0600 root`), so that only the user of the
     active local session gets access. Whoever strictly needs a group: a dedicated group only
     for `/dev/uinput`, **not** `input`.
  3. In `packaging/README-uinput.md` replace the `usermod -aG input` step by a note about the
     risk and mark `0666` as a misconfiguration.
  4. In general prefer the `x11` backend under X11 (the default with `BACKEND=auto`); then
     `/dev/uinput` is not needed at all.

### S2 (Medium) - unbounded config numbers: overflow/UB and event floods

**Status (2026-10-15):** recommendation (3) is implemented in the output backends:
`X11Output::wheelV/wheelH` and `UinputOutput::wheelV/wheelH` clamp `n` to +-200 before it is
negated, looped over or multiplied by 120 (no `INT_MIN` UB, no `n*120` overflow, bounded click
count). Config/DSP limits are implemented as well (review 2026-10-15 confirmed): mix weights
<= 100, curve exponent 0.1-10, curve points strictly ascending (input <= 100000, factor <=
100), `ZOOM_EXIT_RATIO` >= 1, `SENS` 0-255, `SMOOTH_MS` 0-500; `StepAccumulator::add` discards
non-finite rates and clamps the steps to +-200 before the cast. The table below shows the
**original** finding.

- **Finding:** several values were bounded only downwards or not at all:
  - mix weights (`src/config.cpp`): only `isfinite` per weight, e.g. `1e308*z`.
  - curve exponent (`src/config.cpp`): `std::max(0.1, v)`, no upper bound.
  - curve points (`src/config.cpp`): factor only `>= 0`.
  - `ZOOM_EXIT_RATIO` (`src/config.cpp`), `DEADZONE` up to 1e9 (harmless).

  In `src/dsp.cpp` (`pow(a/200, exp)*200` resp. `a*f`) this becomes a step rate that
  `StepAccumulator::add` (`src/dsp.cpp`) converts to `int` via
  `static_cast<int>(std::trunc(acc_))`. If the value is outside `int` (or `inf`), that is
  **undefined behavior**. Reproduced with `config.cpp` + `dsp.cpp` (full deflection 350, 8 ms
  tick):

  | Config line | Rate | Steps per tick |
  |---|---|---|
  | `SCROLL_V_CURVE = 60` | 1.6e33/s | `-2147483648` (UB, x86 yields INT_MIN) |
  | `SCROLL_V_MIX = 1e308*z + 1e308*rx` | `inf` | `-2147483648` (UB) |
  | `SCROLL_V_CURVE = 0:0 350:1e6` | 1.7e8/s | 1,340,000 |
  | `SCROLL_V_CURVE = 4` (maximum of the tray slider) | 6,300/s | 50 |

  Consequences: `wheelV(INT_MIN)` negates `n` in `src/output_x11.cpp` (`-n` at INT_MIN is UB
  again) resp. in `src/output_uinput.cpp`; with large but valid values `click()`
  (`src/output_x11.cpp`) resp. `tapArrows()` (`src/dofscroll.cpp`) generate millions of XTest
  events in a loop - the X server and the session are then practically blocked. Already
  reachable through the GUI: exponent 4 + sensitivity 255 = about 2,500 clicks per tick
  (about 320,000/s). Triggerable by A2 (writing the file), A4 (foreign config taken over) and
  by operating errors.
- **Recommendation:** (1) set upper bounds in `applyChannel`/`parseMix`/`parseCurve` (e.g.
  |weight| <= 100, exponent <= 10, curve factor <= 100, `ZOOM_EXIT_RATIO` <= 100) and report
  violations as warnings. (2) Independently, discard non-finite rates in
  `StepAccumulator::add` and hard-limit the steps per tick (e.g. +-50) before converting to
  `int`; clamp the accumulator. (3) Limit `n` in the backends before the negation. (4) Add a
  test in `tests/` with exactly these values.

### S3 (Medium) - "stuck" Ctrl key and lost key-up events

**Status (2026-10-15):** finding a (kdotool part) **fixed** through S6 (200 ms time limit).
Finding b **fixed** (see there). Finding c **fixed**: `UinputOutput::doFlush` keeps unsent
events in the buffer on `EAGAIN` and sends them again on the next flush; on overflow (> 4096
events) only the oldest wheel events are discarded, never key/button events. Config reading no
longer blocks on FIFOs (`loadProfiles` rejects non-regular files, S4). The window query (X11
round trips and `kdotool`) now runs in its own background thread (`ActiveWindow`, interval 100
ms resp. 250 ms for `kdotool`); `poll()` only reads the cache and never blocks the tick thread.
For this `main` calls `XInitThreads()`, the error handler is thread-local and set once.
**Finding a is thereby fixed.**

- **Finding a (blocking in the tick thread):** while zooming the daemon holds Ctrl
  (`src/dofscroll.cpp`, `setCtrl`). All profile inputs ran in the same thread:
  `ActiveWindow::poll()` started `kdotool` via `popen` under KDE/Wayland and waited in `pclose`
  for it to end (`src/activewin.cpp`; DBus timeouts of up to 25 s are possible), under X11 it
  does several `XSync` round trips, and `loadConfig()` reads the file synchronously (see S4: a
  FIFO blocks indefinitely). If one of these calls blocks, Ctrl stays pressed in the session -
  every normal keyboard input then becomes a shortcut (`Ctrl+W`, `Ctrl+Q` ...).
- **Status of finding b: fixed.** At start the daemon releases (`forceReleaseAll`) Ctrl, arrow
  keys and the left mouse button regardless of its own state, and the unit calls
  `ExecStopPost=-<bindir>/dofscroll --release-keys` after every stop (also after
  SIGKILL/crash; shown here for the development install under `/usr/local/bin`; packaged units use
  the packaged binary path, see [the October 2026 review](security-review-2026-10.md), F1, for
  the state of that path). What cannot be fixed is the time between kill and restart/StopPost
  (about `RestartSec=1s`).
- **Finding b (crash/kill under X11):** on `SIGTERM` keys are released, but not on `SIGKILL`
  (e.g. systemd after `TimeoutStopSec`), crash (S2) or Xlib IO error (`quietIOErrorHandler`
  returns, Xlib then terminates the process itself, `src/output_x11.cpp`). Keys pressed via
  XTest are not reliably released when the connection drops. (With the uinput backend the
  kernel releases pressed keys when the device is removed.)
- **Finding c (uinput discards events):** `UinputOutput::flush` (`src/output_uinput.cpp`)
  discarded the whole rest of the buffer on `EAGAIN` (non-blocking fd) - the key-up for Ctrl or
  `BTN_LEFT` may be in there. The internal state `held_`/`btnLeft_` then counts as "released"
  while the key stays pressed in the system.
- **Recommendation:** move the window query to its own thread (or `posix_spawn` with a
  non-blocking pipe, timeout and `kill`); read the config only if it is a regular file (S4).
  In `flush` keep release events on `EAGAIN` and resend them in the next tick. Optionally an
  `ExecStopPost=` with a small helper that releases Ctrl, and a watchdog that releases Ctrl if
  a tick is considerably late.

### S12 (Low, fixed) - `/dev/uinput` file descriptor without `O_CLOEXEC`

- **Finding:** `UinputOutput::init` opened `/dev/uinput` (and the fallback `/dev/input/uinput`)
  with `O_WRONLY | O_NONBLOCK` without `O_CLOEXEC`. The `kdotool` child process started via
  `popen` (S6) thereby inherited a **writable** fd on `/dev/uinput` and it (or any program it
  launched) could have injected input through it.
- **Fix:** both `open()` calls now use `O_CLOEXEC`; in addition the `kdotool` child in
  `runCapture` closes all fds >= 3 and the pipe is created with `pipe2(O_CLOEXEC)`.

### S4 (Low) - `loadProfiles` without input limits

- **Finding:** `src/config.cpp` opened the path with `std::ifstream` and read with
  `std::getline` without any limit: no check for a regular file, no maximum size, no maximum
  line length, no maximum number of sections/terms. A symlink to `/dev/zero` lets the daemon
  allocate memory until OOM (one single endless line), a FIFO already blocks `open()` - in the
  main thread, i.e. together with S3a. `ps.findOrAdd` (`src/config.cpp`) compared every new
  section with all existing ones via `lower()` (quadratic), noticeable with many thousands of
  sections. Every line additionally copied a complete `Settings` object (`src/config.cpp`, plus
  a copy in `applyKey`). Reachable only for someone who can write `~/.config/dofscroll` (or
  `~/.config` for the legacy fallback) (A2) - hence Low. Integers themselves are cleanly bounded
  (`parseInt` rejects |v| > 1e9, `src/config.cpp`; button numbers are checked against `< 32`
  before the shift, `src/dofscroll.cpp`).
- **Recommendation:** open the file with `open(O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)`,
  `fstat` -> accept only `S_ISREG`, size <= 1 MiB, discard lines > 4 KiB, at most e.g. 256
  sections and 32 mix terms. (If the config is deliberately a symlink into a dotfile repo,
  check only `S_ISREG` of the target instead of `O_NOFOLLOW`.) Implemented, see section 6.

### S5 (Low) - saving the config (temp file, permissions, TOCTOU)

- **Finding a (temp file):** `saveProfiles` wrote to `path + ".tmp." + PID`
  (`src/config.cpp`) with `std::ofstream(..., trunc)` - i.e. `O_CREAT|O_TRUNC` without
  `O_EXCL`/`O_NOFOLLOW`. If the config lies in a directory writable by others (e.g.
  `XDG_CONFIG_HOME=/tmp/...`), A1 can pre-create a symlink with the predictable name there; the
  tray would then truncate/overwrite an arbitrary file of the user. Not exploitable in the
  default case (`~/.config/dofscroll`, `0700`, user-owned).
- **Finding b (permissions):** the file was created with the permissions from the umask
  (`0644`). The config contains no secrets - this is Info, but `0600` is the cleaner default.
  With the unit (`UMask=0077`) `0600` is written on the next save; `rename` replaces the old
  file including its permissions.
- **Finding c (durability):** no `fsync` before `rename` (`src/config.cpp`) and no `fsync` of
  the directory. After a power failure an empty file can remain on XFS/ext4; the daemon then
  silently falls back to defaults.
- **Finding d (symlink config):** `rename` replaces a symlink at `path` with a regular file -
  whoever maintains the config as a link into a dotfile repo silently loses the connection.
- **Finding e (lost update/TOCTOU):** the tray reads the file only when the window opens
  (`src/dofscroll_tray.cpp`), keeps it in memory and writes the **whole** file after every
  change (debounced 250 ms). Whatever was changed by hand in the meantime (e.g. through "Open
  configuration file") or by a second tray instance is overwritten without warning.
  `access(F_OK)` before creating is also a (harmless) TOCTOU.
- **Finding f (reader):** the daemon re-reads every 1 s based on a `stat` stamp
  (`src/dofscroll.cpp`). If an editor does not write atomically, it can read a half-written
  file - consequence: briefly wrong but validly parsed values.
- **Recommendation:** temp file via `mkstemp` in the target directory (`O_EXCL`, mode `0600`),
  `fsync(fd)` + `rename` + `fsync(dirfd)`. Before saving, compare the `stat` stamp
  (mtime/inode/size) with the one remembered at load and reload/ask on mismatch; optionally
  `flock` on a lock file. For symlink configs replace the target (`realpath`). Implemented, see
  section 6.

### S6 (Low) - `popen` in `activewin.cpp`

**Status (2026-10-15): fixed.** `ActiveWindow` resolves `kdotool` once in `init()` via
`findInPath` (only absolute `PATH` entries, relative/empty ones are ignored) and starts it in
`runCapture` via `fork`/`execl` without a shell (fixed arguments, stdin/stderr to `/dev/null`,
all other fds closed in the child, pipe with `O_CLOEXEC`). The output is read with `poll` and a
200 ms time limit; on timeout the child is killed with `SIGKILL` and reaped (`waitpid`); an
exit status != 0 or an error yields an empty result (no profile match) instead of blocking. The
description below is the state before the fix.

- **Finding:** `popen("kdotool getactivewindow getwindowclassname 2>/dev/null", "r")`
  (`src/activewin.cpp`) - the command line is fixed, so **no** shell injection. But: `popen`
  starts `/bin/sh` and searches `kdotool` via `PATH` (and `inPath` only checked executability).
  Any `kdotool` earlier in the PATH (e.g. `~/.local/bin`, if it is before `/usr/bin` in the
  user manager's PATH) is executed up to 4 times per second - relevant only for A2, who
  executes code as the user anyway. At most 255 bytes of the first line are read (`fgets` into
  `buf[256]`) - buffer correctly bounded. The main problem is blocking (S3a). The child
  processes inherit the daemon's systemd sandbox (e.g. `MemoryDenyWriteExecute`, only
  `AF_UNIX`), which fits `kdotool` (DBus).
- **Recommendation:** `posix_spawn` with an absolute path (resolve once at start and log),
  read the pipe non-blocking with a timeout, kill the child on timeout - or query directly via
  DBus (KWin scripting) in a separate thread.

### S7 (Low) - reader/main thread, signals, shutdown

- **Finding a (data exchange):** axes, button mask, timestamp and generation are
  `std::atomic`, the device name is handed over under a mutex (`src/dofscroll.cpp`) - **no data
  race**. The six axes are stored/read individually; the main thread can see a "mixed" set from
  two events (for one tick). That is functionally uncritical (Info). The libspnav calls happen
  exclusively in the reader thread, Xlib exclusively in the main thread; `XSetErrorHandler` is
  process-global but is only applied in the main thread (`src/activewin.cpp`,
  `src/output_x11.cpp`).
- **Finding b (shutdown):** the reader thread is detached and blocks in `spnav_wait_event`
  (`src/dofscroll.cpp`). After `return 0` from `main` the static destructors (including
  `std::cerr`) run while the thread may still log events - theoretically UB at exit. The
  deliberately leaked `Daemon` object only prevents access to freed `DeviceReader` memory.
- **Finding c (signals):** the handlers are installed only at the end of `initialize()`
  (`src/dofscroll.cpp`), after backend init (uinput waits 300 ms there). A SIGHUP from the
  tray in this window terminates the process (default action); `Restart=always` catches that.
  The handlers themselves are correctly minimal (`volatile sig_atomic_t`); `std::signal`
  instead of `sigaction` is unproblematic under glibc.
- **Recommendation:** set signals first thing in `main` (or via `sigaction` with `SA_RESTART`);
  on exit after `releaseAll()`/`flush()` use `_exit(0)` instead of `return`, or wake the reader
  via `shutdown(spnav_fd(), SHUT_RDWR)` and `join` it.

### S8 (Low) - root-owned symlinks into a user-writable directory

- **Finding:** `/usr/local/bin/dofscroll` and `...-tray` are root-owned symlinks into the
  cloned repo directory (as provided by the README). Whoever can write the repo directory (the
  user, hence also A2) determines what every other user - including root - executes when
  calling `/usr/local/bin/dofscroll`. Likewise `findIconDir` looks for icons next to the
  resolved binary (`src/dofscroll_tray.cpp`). On a single-user machine without foreign callers
  this is acceptable.
- **Recommendation:** on multi-user systems use `cmake --install` (copy to `/usr/local`,
  root-owned) instead of symlinks, or place binaries/units only in the user context
  (`~/.local/bin`) and do not touch `/usr/local/bin`.

### S9 (Low) - `spacenavd` socket writable for everyone

- **Finding:** `/run/spnav.sock` is `srwxrwxrwx root` (default of `spacenavd`). Every local user
  (A1) can read the SpaceMouse's movements/buttons and - with the libspnav 1.x protocol -
  change `spacenavd` device settings (sensitivity, axes, LED), thereby disturbing operation. A1
  cannot forge the socket (`/run` is root-only). The daemon trusts the axis values from
  `spacenavd` unchecked; extreme values (A3 via a prepared device, if `spacenavd` passes them
  through) run into the same path as S2 - the hardening from S2 covers that as well.
- **Recommendation:** accept on single-user machines; otherwise restrict `spacenavd` to a group
  by configuration or by drop-in (`RuntimeDirectory`/`chmod` in `ExecStartPost`).

### S10 (Low) - tray: helper processes

- **Finding:** positive: `spawnDetached` uses `g_spawn_async` with an argv array, no shell
  (`src/dofscroll_tray.cpp`); the config path is passed as a single argument. GUI values are
  validated with the same parser (`parseMix`/`applyKey`) before writing, profile patterns must
  not contain control characters or line breaks (and no `]` followed by `#` or `;`, which would
  end the section header early; saving refuses it) - **no config injection through the GUI**. But:
  `G_SPAWN_SEARCH_PATH` searches `systemctl`/`xdg-open` via `PATH`. With the hardened unit,
  `xdg-open` and the editor started by it inherit the tray sandbox (only `~/.config/dofscroll`
  and `/run/user/UID` writable, only `AF_UNIX`/`AF_NETLINK`). A freshly started editor then
  cannot save e.g. under `~/Documents`; if the editor is already running (single instance via
  DBus, e.g. Kate), it opens the file outside the sandbox.
- **Recommendation:** use absolute paths (`/usr/bin/systemctl`, `/usr/bin/xdg-open`); for "open
  file" use the DBus portal/`org.freedesktop.FileManager1` or `gtk_show_uri_on_window` variant,
  which starts the editor via the session bus outside the sandbox. (Implemented differently,
  see section 6: `systemd-run --user`.)

### S11 (Low) - config path from the environment

- **Finding:** `defaultConfigPath` (`src/config.cpp`) took over `XDG_CONFIG_HOME` unchecked
  (the XDG specification requires ignoring relative values) and fell back to `"."` when `HOME`
  was missing - the config path then depended on the working directory.
- **Recommendation:** accept only an absolute `XDG_CONFIG_HOME`; without `HOME` determine the
  home via `getpwuid(getuid())` instead of `"."`. Implemented, see section 6.

### Info / positives

- **I1 Buffers:** `spnav_dev_name(name, sizeof(name))` with explicit null termination
  (`src/dofscroll.cpp`); `readlink("/proc/self/exe", exe, sizeof(exe)-1)` with an `n > 0` check
  and termination (`src/dofscroll_tray.cpp`; with paths >= 4095 characters it would silently
  truncate -> wrong icon folder, harmless); `strncpy` of the uinput name with
  `UINPUT_MAX_NAME_SIZE - 1` after `memset` (`src/output_uinput.cpp`); `fmtNum` with
  `snprintf(buf, sizeof buf)` (`src/config.cpp`). No format-string bugs (`fprintf`/
  `gtk_message_dialog_new` always with `"%s"`).
- **I2 Minimal uinput device:** the virtual device reports only the keys and buttons the output
  layer needs (mouse buttons, wheel and the named keys from `src/keys.hpp`; originally only
  `BTN_LEFT`, Ctrl, four arrow keys and wheel) - see section 7 for what mode files may add. A
  compromised daemon could type arbitrary keys through a new device anyway as long as
  `/dev/uinput` is writable (see S1).
- **I3 WM_CLASS spoofing:** every X client chooses its own `WM_CLASS` and can thereby trigger a
  foreign `[app:*]` profile - effect only on scroll parameters (bounded by S2).
- **I4 Log injection:** the device name (from the USB descriptor, A3) was written to the
  journal unescaped (`src/dofscroll.cpp`); with line breaks, log lines could be faked.
  Cosmetic; fixed in section 6 (`sanitizeForLog`).
- **I5 Button presses in the journal:** every SpaceMouse button press was logged - no secrets,
  but journal noise (now only with `--debug`).
- **I6 Network:** neither daemon nor tray needs an IP network; the units block `AF_INET/AF_INET6`.

---

## 3. systemd hardening of the user units

Files: `systemd/dofscroll.service`, `systemd/dofscroll-ui.service` (reviewed as `dofscroll-tray.service` before it was removed).
Target platform RHEL 9 = **systemd 252**; only directives available there were used.

> **Note, effective from the next login/`daemon-reload`:** on a setup that links
> `~/.config/systemd/user/dofscroll*.service` as **symlinks into this repo** (see README),
> changed units become active with the next `systemctl --user daemon-reload` plus a restart of
> the services, or at the next login. Rollback in case of problems: comment out the `[Service]`
> hardening lines or reset single directives via a drop-in (`systemctl --user edit
> dofscroll.service`), e.g. `MemoryDenyWriteExecute=no`.

### Set (both units unless noted otherwise)

| Directive | Purpose / note |
|---|---|
| `NoNewPrivileges=yes` | no privilege gain via setuid/file caps, also not for child processes |
| `UMask=0077` | newly written files (config!) readable only by the user |
| `PrivateUsers=yes` | **required in the systemd 252 user manager**, see below |
| `ProtectSystem=strict` | whole file system read-only (except `/dev`, `/proc`, `/sys` API) |
| `ProtectHome=read-only` | home only readable; the daemon reads the config, writes nothing there |
| `ConfigurationDirectory=dofscroll`, `ConfigurationDirectoryMode=0700` | **tray only**: creates `~/.config/dofscroll` (0700) before the sandbox is set up so that the `ReadWritePaths` target exists |
| `ReadWritePaths=%t` | **daemon only**: `/run/user/UID` writable; the daemon creates only `dofscroll-status` (temp file + rename) and `dofscroll.log` there, see section 8 |
| `ReadWritePaths=%E/dofscroll %t` | **tray only**: write the config (temp + rename in the same directory; this includes `modes/` for first-start seeding), `/run/user/UID` for dconf/at-spi. The rest of `~/.config` (including `systemd/user`, `autostart`, `environment.d`) stays read-only; the earlier `ReadOnlyPaths` exceptions are gone |
| `ProtectKernelTunables=yes`, `ProtectControlGroups=yes`, `ProtectHostname=yes` | kernel/cgroup/hostname protection |
| `PrivateIPC=yes` | **daemon only** (no SysV IPC needed) |
| `KeyringMode=private` | own kernel keyring |
| `RestrictAddressFamilies=AF_UNIX` (daemon) / `AF_UNIX AF_NETLINK` (tray) | X11, `spacenavd`, DBus are Unix sockets; GIO may use netlink |
| `RestrictNamespaces=yes`, `RestrictRealtime=yes`, `RestrictSUIDSGID=yes`, `LockPersonality=yes` | standard hardening |
| `MemoryDenyWriteExecute=yes` | **daemon only** (libX11/libXtst/libspnav need no W+X) |
| `SystemCallArchitectures=native` | no 32-bit syscalls |
| `SystemCallFilter=@system-service` + `~@privileged` + `~@resources`, `SystemCallErrorNumber=EPERM` | syscall allowlist; `~@privileged` blocks e.g. `@module`, `@clock`, `@raw-io`, `@reboot`, `@swap`; `~@resources` (since 2026-10-15) blocks `setpriority`, `sched_set*`, `setrlimit`, `ioprio_set`, `mbind` ... (`prlimit64` for querying stays allowed). Empirically effective (EPERM) and without loss of function: neither daemon nor tray call these syscalls |

### Why `PrivateUsers=yes` is needed (verified)

Without `PrivateUsers=yes` the systemd 252 user manager behaved as follows (test with transient
units, see "Verification"):

- `ProtectSystem=strict`, `ProtectHome=read-only` etc. were **silently ignored** - the test
  unit could still write `~/.config` and `~`; `systemd-analyze security` nevertheless reported
  "2.5 OK" (the offline analysis checks only the unit file, not the effect).
- `CapabilityBoundingSet=`, `ProtectKernelModules=`, `ProtectKernelLogs=`, `ProtectClock=` made
  the start fail with `status=218/CAPABILITIES` (the user manager may not shrink the bounding
  set without its own user namespace). They start together with `PrivateUsers=yes` only where
  unprivileged user namespaces are unrestricted; where a distribution confines them they fail even
  with `PrivateUsers=yes` (next section), so they are not set any more.

With `PrivateUsers=yes` all restrictions take effect. Side effect: supplementary groups appear
as `nobody` inside the service (`id` shows `65534(nobody)`), but the kernel's permission check
still uses the real groups - access to a `root:input 0660` device file worked in the test.
`systemctl --user` from the sandbox (tray: SIGHUP/restart) works as well, since the user
manager sees the caller with the real UID via `SO_PEERCRED`. Prerequisite: unprivileged user
namespaces are allowed (`/proc/sys/user/max_user_namespaces` > 0; RHEL 9 default).

### Why `CapabilityBoundingSet`/`ProtectKernelModules`/`ProtectKernelLogs`/`ProtectClock` are not set

Verified 2026-10-25 on Ubuntu 26.04 (systemd 259, AppArmor,
`kernel.apparmor_restrict_unprivileged_userns = 1`):

- With `PrivateUsers=yes` plus any of the four directives, `systemctl --user start
  dofscroll.service` fails with `status=218/CAPABILITIES`; the journal says `Failed to drop
  capabilities: Operation not permitted`, at the step `CAPABILITIES`, before `ExecStart`.
- The kernel audit log gives the reason: `userns_create ... profile="unconfined" ...
  target="unprivileged_userns"` followed by `apparmor="DENIED" operation="capable" ...
  profile="unprivileged_userns" capability=setpcap` (and `sys_admin`). The sandbox setup runs
  confined in that profile and may not manipulate capabilities or the kernel-related mounts, so
  the start is aborted.
- It is not specific to dofscroll: a transient unit running `/bin/true` with the same directives
  fails with `218` as well (`systemd-run --user --wait -p CapabilityBoundingSet= /bin/true` and
  the same for `ProtectKernelModules=`, `ProtectKernelLogs=`, `ProtectClock=`), while the other
  directives of the units above start fine there.
- Where the calls are allowed, all four start (RHEL 9, systemd 252, verified 2026-10-15 with
  `PrivateUsers=yes`); without `PrivateUsers=yes` they fail with `218` on every system (see above).

A setup that wants them anyway on a distribution that allows the calls:

    systemctl --user edit dofscroll.service

    [Service]
    CapabilityBoundingSet=
    ProtectKernelModules=yes
    ProtectKernelLogs=yes
    ProtectClock=yes

`systemctl --user daemon-reload` plus a restart makes it effective. On a system with the AppArmor
restriction the unit then fails to start again - the directives are a hardening gain there, not a
functional requirement: nothing in the daemon or the interface needs them.

### Limit of the sandbox: DBus/`systemd-run --user`

The hardening restricts only the unit's own process, **not** services it reaches over the
session bus connection. Because `AF_UNIX` and the DBus session socket must be allowed
(AppIndicator, `kdotool`, X11), a compromised daemon or tray - like any process with access to
the session bus or the user manager - can start a **new, unhardened transient unit** via DBus
(`org.freedesktop.systemd1`) or `systemd-run --user`, which inherits neither `ProtectSystem`,
`PrivateUsers`, `MemoryDenyWriteExecute` nor the syscall/address-family filters and runs with
the full rights of the user. The sandbox thus protects against bugs in the daemon (memory
errors, file access) but is **not a boundary against an attacker with code execution** in the
service. Countermeasures would be a filtered bus (`xdg-dbus-proxy`) or dropping the session bus
in the daemon (it needs it only for `kdotool`); both are deliberately not implemented here.

### Deliberately NOT set

| Directive | Reason |
|---|---|
| `CapabilityBoundingSet=`, `ProtectKernelModules=`, `ProtectKernelLogs=`, `ProtectClock=` | they abort the start with `status=218/CAPABILITIES` on distributions that confine unprivileged user namespaces with AppArmor (Ubuntu 24.10+, verified on Ubuntu 26.04), see "Why ... are not set" above; a hardening gain, not a functional requirement |
| `PrivateTmp=yes` | hides `/tmp/.X11-unix/X0` -> Xlib could connect only through the abstract socket, which is missing depending on the X server configuration (`-nolisten local`); also some setups use `/tmp` for `XAUTHORITY`. Read access to `/tmp` stays read-only anyway through `ProtectSystem=strict`. |
| `PrivateNetwork=yes` | cuts abstract Unix sockets (X11 `@/tmp/.X11-unix/X0`, possibly DBus/at-spi); network access is already blocked through `RestrictAddressFamilies` |
| `PrivateDevices=yes`, `DevicePolicy=`/`DeviceAllow=` | `PrivateDevices` hides `/dev/uinput`; device ACLs (cgroup BPF) cannot be enforced by the user manager - the directive would be accepted but stay ineffective |
| `ProtectProc=invisible`, `ProcSubset=pid` | available and startable in systemd 252 (both units, tray 3 s + daemon `--release-keys` tested), but **ineffective** in a user unit with `PrivateUsers=yes` (checked again 2026-10-15: all PIDs, `/proc/sys`, `/proc/acpi` still visible). Not set, to avoid false security |
| `MemoryDenyWriteExecute=yes` (tray) | GLib `GRegex` uses PCRE2 JIT, Mesa/GL in the GTK stack may need W+X memory; also the editor started via `xdg-open` inherits the restriction |
| `PrivateIPC=yes` (tray) | GTK/cairo use MIT-SHM (SysV shared memory shared with the X server) |
| `ReadWritePaths=` (daemon) | only `%t` (runtime dir) for status/log file, nothing under `~` or `/etc`; `/run/user/UID` is private (0700) anyway |
| `IPAddressDeny=any` | starts in both units but is **ineffective** in the user manager (checked 2026-10-15: `connect()` to `127.0.0.1:22` succeeded despite `IPAddressDeny=any`; the cgroup-BPF firewall works only for the root manager). The daemon is covered by `RestrictAddressFamilies=AF_UNIX` anyway; the tray needs `AF_NETLINK` and therefore deliberately stays without an IP block |
| `User=`/`DynamicUser=`, `RootDirectory=` | not sensible for user units that work in the user's session |
| `ProtectSystem` exception for `/tmp` | not needed: `connect()` to Unix sockets also works on read-only mounts (X11, `spacenavd`, DBus tested) |
| `RestrictFileSystems=`, `ProtectHome=tmpfs` | `RestrictFileSystems` needs BPF LSM (not available in the user manager); `tmpfs` would hide binaries that are linked from the home directory |

### Further hardening 2026-10-15 (candidates ProtectProc/ProcSubset/IPAddressDeny/`~@resources`)

Each candidate was tested individually and combined via `systemd-run --user --wait --collect`
with all other directives of the respective unit (daemon: `dofscroll --release-keys`, which is
also the `ExecStopPost` path in the same sandbox; tray: 3 s under `timeout`, exit 124 = ran
without crash, stderr only a Gtk locale warning). Result: all four start; only `~@resources`
is effective (set). `ProtectProc`/`ProcSubset`/`IPAddressDeny` are no-ops under
`PrivateUsers=yes` in the user manager (evidence in the table above) and are therefore not set.
The offline-computed `systemd-analyze security` result (`--offline=true systemd/<unit>`) rose
from 2.2 to 2.1 (both units; the three no-op directives would cosmetically push it to 1.7
without improving the sandbox). Not tested: settings window/editor start of the tray under
`~@resources` (hardware/GUI test by a human).

### Verification (without installing, without restarting the running services)

```sh
systemd-analyze --user verify    "$PWD/systemd/dofscroll.service"       # no output = ok
systemd-analyze --user verify    "$PWD/systemd/dofscroll-ui.service"
systemd-analyze --user security --offline=yes "$PWD/systemd/dofscroll.service"
systemd-analyze --user security --offline=yes "$PWD/systemd/dofscroll-ui.service"
```

Result (systemd 252): `verify` without messages; exposure score **9.8 UNSAFE -> 2.2 OK** for
both units. Treat the score as indicative only: in offline mode the tool reports "runs as root"
for user units, so the number does not describe the real exposure.

Since the offline analysis does not check the actual effect (see `PrivateUsers`), the
`[Service]` hardening lines were additionally applied as **transient** units
(`systemd-run --user --wait --pipe --collect -p ... <test command>`; `%h`/`%t` replaced by
hand) - without touching the real services. Result:

| Check | Daemon profile | Tray profile |
|---|---|---|
| `connect()` to `/tmp/.X11-unix/X0`, `xprop -root`, `xdpyinfo` (XTEST present) | ok | ok |
| `connect()` to `/run/spnav.sock` | ok | ok |
| `open("/dev/uinput", O_WRONLY)` | ok | ok |
| `open()` of a `root:input 0660` device file (group right under `PrivateUsers`) | ok | ok |
| read config | ok | ok |
| write to `~/.config` | denied (EROFS) | denied (EROFS), only `~/.config/dofscroll` ok (create + rename tested) |
| write to `~` | denied | denied |
| write to `/run/user/UID` | ok (since status/log file, not tested separately) | ok |
| connect to the DBus session socket | ok | ok |
| `socket(AF_INET)` | denied (EAFNOSUPPORT) | denied |
| `systemctl --user kill -s HUP <test unit>` | - | ok |
| `/usr/local/bin/dofscroll --version` (loads all libraries under MDWE) | ok | - |
| run `dofscroll-tray` for 5 s | - | ok (only the known locale warning, also occurs without sandbox) |

What could not be checked - this has to happen on the real system: scrolling/zooming/nav/click
button with a SpaceMouse in the hardened service, the Wayland/uinput path with `kdotool`,
opening the settings window and saving via the GUI, "Open configuration file" with the default
editor. Procedure:

```sh
systemctl --user daemon-reload
systemctl --user restart dofscroll.service dofscroll-ui.service
journalctl --user -u dofscroll.service -u dofscroll-ui.service -f
```

---

## 4. Pre-commit hook

- `scripts/pre-commit` (POSIX sh):
  1. **Secret scan** over the added lines of `git diff --cached` (staged changes only).
     Patterns (written in the file with `[Y]`/`[t]`/`[A]`/`[d]` so the hook does not report
     itself):
     - private keys: header line `-----BEGIN ... PRIVATE KE[Y]-----`
     - GitLab personal access token: `glpa[t]-` + at least 20 characters `[A-Za-z0-9_-]`
     - AWS access key ID: `AKI[A]` + 16 characters `[0-9A-Z]`
     - password assignment: `passwor[d]`, optional spaces, `=`, non-empty value
       (case-insensitive)

     On a match: abort with file and line. Bypass only deliberately via
     `git commit --no-verify`.
  2. The CMake `check-fast` target (configured in a `build-hook` directory). Without a
     CMakeLists.txt it is skipped. Note: it checks the working tree, not exactly the index.
- `scripts/install-hooks.sh [--force]`: links `.git/hooks/pre-commit` ->
  `../../scripts/pre-commit` (relative, survives moving the repo; absolute for worktrees or a
  different hook directory). Idempotent; an existing foreign hook is replaced only after a
  prompt (interactive) or with `--force` and backed up to `pre-commit.bak.<timestamp>` first.
  Warns if `core.hooksPath` is set.

The hook is a convenience gate, not a security mechanism - it runs only locally and can be
bypassed. Binding is the CI pipeline (planned as a GitHub Actions workflow) together with the
repository settings below.

---

## 5. Repository: merge rules and branch protection (to be set manually)

These settings cannot be set through a file in the repo, only in the hosting platform's
interface (maintainer/admin role) or via its API. For GitHub:

1. **Settings -> Branches -> Branch protection rule for `main`:**
   - Require a pull request before merging; require at least one approval, dismiss stale
     approvals, optionally require review from code owners.
   - Require status checks to pass before merging; select the CI jobs (`build`, `test`,
     `check`). Do **not** treat skipped checks as successful.
   - Require conversation resolution before merging.
   - Require branches to be up to date before merging (so CI runs on exactly the state that
     is merged).
   - Do not allow force pushes and deletions; apply the rules to administrators as well.
2. Recommended: squash or rebase merges, "Automatically delete head branches".
3. Verification: open a test pull request with a deliberately failing check or an open
   conversation - the merge button must be blocked; a direct `git push` to `main` must be
   rejected.

---

## 6. Second audit (2026-10-15) and fixes

Six parallel reviews (window query/X11, config, tray, daemon, systemd/CI/supply chain, tools).
No high or critical findings were reported by this audit, which predates the packaging work and
the modes feature; the later [October 2026 review](security-review-2026-10.md) found two High
issues in those areas (F1, F2). All findings below are **fixed** unless noted otherwise.

| Area | Finding | Fix |
|---|---|---|
| Config (medium) | O(n^2) loading (50,000 sections: 29.6 s) | index in `ProfileSet`, max. 256 sections, 512 keys per section (10,000 sections: ~0.05 s) |
| Config (medium) | no file/line limits (200 MB line: 3.9 GB RAM) | file <= 1 MiB, line <= 8 KiB, mix <= 12 terms, curve <= 64 points, `ZOOM_LATERAL` <= 6 |
| Config | stat/open TOCTOU (FIFO) | single `open(O_NONBLOCK)`, then `fstat` on the fd |
| Config | path races when saving, predictable temp name | `openat`/`renameat` on the directory fd, temp name from `getrandom`, `O_EXCL` |
| Config | symlink loop replaces the link; setuid/sticky bits copied | ELOOP error; mode masked to 0777 |
| Config | unvalidated `kv` when saving; relative `XDG_CONFIG_HOME`; world-readable config directory | validation, relative paths ignored, directory 0700, `HOME` fallback via `getpwuid` |
| Daemon | suspend file without owner check | `st_uid == geteuid()`, mode `& 077 == 0`, `XDG_RUNTIME_DIR` must be private |
| Daemon | log injection (device name, `WM_CLASS`) | `sanitizeForLog()` (`src/log_sanitize.hpp`) |
| Daemon | log flood through button presses | only with `--debug` |
| X11 | IO error handler returns (then `exit()` from a secondary thread) | shared handler in `src/xerrors.hpp`, `_exit(1)`; installation idempotent |
| X11 | many child windows slow down the query | at most 32 children; `WM_CLASS` truncated to 256 bytes |
| Tray | duplicate keys, case in merge, saving after a failed read, quit with an open dialog | `setKey`/removal on all entries, normalized comparison, saving skipped, `quitNow()` |
| systemd | additional directives | `SystemCallFilter=~@resources` set; `ProtectProc`, `ProcSubset`, `IPAddressDeny` ineffective in the user manager (`PrivateUsers=yes`), deliberately not set |

**Follow-up error and fix (2026-10-15):** with the restricted write permission "Open
configuration file" failed: `xdg-open` (-> `kde-open5`) writes `~/.config/kde-open5rc` and got
"Configuration file ... not writable"; an editor started in the sandbox also could not save
anything outside the config folder. The tray therefore starts `xdg-open` via
`systemd-run --user --collect -p KillMode=process` as its own unsandboxed transient service.
This deliberately uses the same capability as the accepted DBus escape (section 3); no new
capability is added, and command and path are fixed (no shell call). Checked: from a sandbox
(`ProtectHome=read-only`) `~/.config` is not writable, in the transient service it is.

**Open or deliberately unchanged:**

- `/dev/uinput` may be `0666` on a system through another udev rule (see
  `packaging/README-uinput.md`) - system configuration, not in the repo; check on every machine.
- **Fixed (2026-10-15):** the tray unit used to write to all of `~/.config` (except `systemd`,
  `autostart`, `environment.d`). The config now lives in `~/.config/dofscroll/` (`0700`); the
  tray unit may write only this directory (+ `/run/user/UID`) (`ConfigurationDirectory=` +
  `ReadWritePaths=%E/dofscroll %t`, checked empirically: `touch ~/.config/zz-test` and writing
  to `~/.config/systemd/user` fail with EROFS). The tray migrates the old file
  `~/.config/spnav-scroll.conf` at start (copy, the legacy file stays untouched); until then
  the daemon reads the legacy path as a fallback.
- `WM_CLASS` is forgeable by any X client; this only affects the choice of an already
  configured `[app:*]` profile.
- DBus escape (`systemd-run --user`), see section 3: accepted.
- CI should deliberately use the floating tag `rockylinux:9` (GitHub Actions workflow planned).
- Not available locally: `cppcheck`, `clang-tidy`, `valgrind`, ThreadSanitizer runtime,
  `libasan` (CI runs `cppcheck` and ASAN).
- Fuzzing of the profile loader (`src/config.cpp`, `src/profile_files.cpp`) and the migration
  (`src/profile_migrate.cpp`): `tests/test_profile_fuzz.cpp` feeds random and mutated config trees to the parsers,
  the writers and `migrateToSplit` and checks the invariants of the headers (a failed migration leaves `dofscroll.conf`
  unchanged, a second run does nothing). A short deterministic pass is a ctest; the sanitizer job of `ci.yml` also runs
  300 iterations with ASan/UBSan and a seed that changes per run. By hand:
  `cmake -S . -B build-asan -DDOFSCROLL_SANITIZE=ON && cmake --build build-asan --target test_profile_fuzz &&
  build-asan/tests/test_profile_fuzz --iterations 20000 --seed 1`; replay a failure with `--seed S --only K`.

---

## 7. Mode plugins and first-start seeding

Modes (`src/modes.*`, format in `readme.md` section 4) add a new input source (the directory
`~/.config/dofscroll/modes/`) and a new output capability (named keys and mouse buttons chosen
by a file). Assessment and limits:

- **Parser limits:** at most 64 mode files, each <= 64 KiB, <= 128 overrides per file; only
  regular files are read; invalid lines or files are skipped with a warning. Mode ids are
  restricted to `[a-z0-9_-]{1,32}`, so a file name cannot escape the directory. Override keys
  go through the same validated parser as profile keys (the bounds of S2 apply).
- **Key output is a closed set:** actions can only press keys named in `src/keys.hpp` (a fixed
  list; each output backend must map every entry) and the three mouse buttons. A mode file
  cannot send arbitrary key codes. Keys such as Ctrl, Alt or Super are on the list, so a
  malicious mode file (A2/A4: written by the same user or taken over from a stranger) can make
  a device button type shortcuts. The list also holds the media keys and `print` (on Linux the same key code as SysRq; the kernel's magic SysRq functions need Alt and a second key and are usually disabled). Treat mode files like any script you run: only take over mode
  files you have read. For A2 under X11 this adds nothing beyond XTest, but it does for A4 (a
  file from a stranger has no XTest access) and for the uinput backend. Chords are reachable too:
  Ctrl is held while a channel zooms, so a `keys:` action on another channel can send Ctrl+W or
  Ctrl+Q without any Ctrl button (review F2). The daemon warns at load time about
  modifier-capable bindings, but that is a hint, not a gate.
  **Tier rule (key engine):** an analog `keys:` action, which repeats a key as long as the cap is deflected, is only
  accepted for keys that are safe to repeat (tier A: arrows, page up/down, home, end, tab, `ctrl` or `shift` plus one of
  these, plus and minus, volume up/down, media next/previous). Any other key (letters, digits, function keys, Enter,
  Delete, Backspace, Esc, space, `alt` or `super` chords, play/pause, a lone modifier) is tier B: the parser refuses
  `keys:` with "this key can only be used digitally, use step:" in mode files, profiles and the interface alike, and only
  `step:` (one press per push) is accepted, with the plain load warning above. The lift never takes an analog key. There is
  no switch that lifts the rule. The key presses are also capped per tick (4 per function, 8 in all) and the repeat rate at
  `min(100, TICK_HZ / 2)` presses per second. `ENABLED`, `BACKEND` and `TICK_HZ`
  are rejected in mode files so that a mode cannot switch output off.
- **Held state:** keys and buttons held by a mode are released on mode change, suspend,
  `ENABLED = 0` and at daemon exit/start (`forceReleaseAll`, `--release-keys`), so a faulty
  mode file cannot leave a key stuck beyond those events.
- **Seeding (tray):** on start the tray copies the shipped examples into the configuration
  folder only if no config exists yet, and never overwrites an existing file. The daemon has no
  write access to the configuration folder (its unit allows writes only to the runtime dir);
  the tray's write access is limited to `~/.config/dofscroll` (section 3). Destination paths are
  derived from the fixed configuration directory, not from file content.
- **Seeding source:** the destination is fixed, but the *source* directory of the examples can come
  from `$DOFSCROLL_EXAMPLES` or from the location of the executable (review F12). Running the tray
  from an unpacked archive or build tree seeds that tree's examples.
- **Review:** modes loading, symlink handling and the sandbox were reviewed in
  [security-review-2026-10.md](security-review-2026-10.md); findings that stay open are listed in
  "Known limitations" below.
- **Profile files (split layout):** the interface writes `devices/`, `apps/` and `backup/` inside
  `~/.config/dofscroll` (all covered by the `ReadWritePaths` of the UI unit); the daemon only reads
  them (its unit stays read-only for the configuration folder). The files are read through the
  bounded directory reader: owner check, no links, at most 64 KiB each, the first 255 per directory
  in sort order, and only next to a file named `dofscroll.conf` (a legacy `spnav-scroll.conf` in a
  generic folder has no profile directories). A directory that exists but cannot be read makes the
  whole load fail and the daemon keeps its previous settings. Leftover temporary files of an
  interrupted write (`*.conf.tmp.<16 hex>`) older than a day are removed by the next write.
- **Profile files are less restricted than mode files:** a profile may set `ENABLED`, `BACKEND`,
  `TICK_HZ`, `PRIORITY` and channel actions with chords. Mode files reject the process-level keys, but
  `[app:x] ENABLED = 0` is documented and in use (`docs/configuration.md`), so profiles keep it. Instead a
  profile's channel action that presses an Alt/Super chord, or a chord with a non-navigation key, is
  loaded and noted with a warning, as for a mode file. The profile files have the same trust level as
  `dofscroll.conf` (same owner, same folder).

---

## 8. Runtime files in the runtime dir (daemon writes, tray reads)

The daemon writes two files into `$XDG_RUNTIME_DIR` (private, `0700`, owned by the user)
(format: `src/common.hpp`): `dofscroll-status` (profile, device, program, `enabled`, `paused`,
`devices_seen`, `updated`) and `dofscroll.log` (copy of the log lines, <= 300 lines (<= 2000 in
`--debug` mode), rolling: when exceeded only the newest lines remain, additionally <= 256 KiB).

- **Write permission:** `ReadWritePaths=%t` in the daemon unit. The runtime dir is accessible
  only to the user anyway; the daemon creates only these files there (and a short-lived
  `*.tmp.<PID>` of the status file). Everything under `~` and the rest of the file system stay
  read-only.
- **Hardening in code:** `XDG_RUNTIME_DIR` must belong to the effective user and must not be
  group/other-writable, otherwise nothing is written. Status: temp file with
  `O_CREAT|O_EXCL|O_NOFOLLOW`, `0600`, then `rename`. Log: `O_APPEND|O_NOFOLLOW`, `0600`, type/
  owner check via `fstat`. Write errors are never fatal (status: log once; log: mirroring is
  switched off) and do not block the tick thread. Three foreign values (device name, `WM_CLASS`,
  profile description) go through `sanitizeForLog` (no line breaks/control characters). Warnings
  that quote file content (config and mode parse warnings) are logged without that filter, so a
  config or mode file can put control characters into the log (review F7).
- **Runtime path allow-list (all platforms):** every runtime file path is built by one function
  (`src/platform/runtime_path.hpp`): the directory must be absolute and free of `.`, `..`, empty
  parts and control characters, and the file name comes from a fixed list (log, status, suspend
  heartbeat). Windows (`%LOCALAPPDATA%\dofscroll`) and macOS (`~/Library/Caches/dofscroll`) use the
  same rule; these two are CI-verified only. The mode request file (`dofscroll-mode-request`) is read by
  the daemon under the same checks (owner, no symlink, size bound). The interface reads the status
  file bounded, without following symlinks and only if owned by the current user.
- **Trust model:** the tray reads the files and treats their content as foreign data
  (size/permission/owner check, display only as text). Whoever can manipulate the files can
  already act as the user (A2); they contain no secrets except the name of the active program.
- **Single-instance lock (Linux and macOS):** the daemon takes an exclusive, non-blocking `flock(2)` on
  `dofscroll.lock` in the same runtime directory (`src/platform/instance_lock.hpp`) and a second daemon exits
  at once with "another dofscroll daemon is already running" (exit code 75, `RestartPreventExitStatus=75` in the
  unit, exit 0 under launchd, so the service manager does not start it again in a loop). The interface
  starts a daemon when none runs, so this guard keeps a login autostart that races with it from producing
  double events. The kernel drops the lock when the process ends, however it ends: there is no pid file to
  trust and no stale lock to clean up. Same hardening as the other runtime files: the directory is opened with
  `O_NOFOLLOW` and checked on the descriptor (owner, not group/other-writable), the file with
  `openat(O_NOFOLLOW)` under a fixed name of the allow-list, `0600`, owner and type checked, `O_CLOEXEC` so no
  helper program inherits it. Without a trustworthy runtime directory the guard is skipped with a warning (no
  other place is used). Windows keeps its named mutex (`win_main.cpp`). Tested with two holders in one
  process and with forked processes (`tests/test_instance_lock.cpp`).
- On a clean exit the daemon removes the status file; the log file stays (tmpfs, gone at logout).
- `devices_seen` reads `/proc/bus/input/devices` (read access only, no `/sys`).

## 9. Windows and macOS: the file-system layer

The shared core reaches the file system only through `src/platform/fs.hpp` (implementations:
`fs_posix.cpp` for Linux and macOS, `fs_win.cpp` for Windows). The POSIX implementation is the
code that used to be spread over `config.cpp`, `modes.cpp`, `seed.cpp` and `hid_descriptor.cpp`,
moved without functional change; the Linux and macOS properties from sections 2 and 7 therefore
hold on both. What the layer guarantees, and what it does not, per OS:

| Property | Linux / macOS (POSIX) | Windows |
|---|---|---|
| No symlink at the final path component (mode files, HID descriptors, seeding sources, new files) | `O_NOFOLLOW`, `O_EXCL` | `FILE_FLAG_OPEN_REPARSE_POINT`, then **any** reparse tag is refused (symlinks, junctions, mount points, cloud placeholders, dedup/WOF stubs); only `writeFileReplace` follows a symbolic link (and only that tag) so a symlinked config keeps its link; `CREATE_NEW` for new files |
| Names Windows resolves to something else | not applicable | refused in every entry point of the layer (`windowsPathHazard`, `src/platform/path_rules.hpp`): reserved device names (`CON`, `PRN`, `AUX`, `NUL`, `COM1-9`, `LPT1-9`, `CONIN$`, `CONOUT$`, also with an extension or trailing spaces, also the superscript digit forms), alternate data streams (`:`), trailing dots/spaces, `\\?\` and `\\.\` prefixes, drive-relative `C:name`, `* ? < > " |` and control characters. Mode ids reject the reserved stems on **all** OSes (`validModeId`), so a mode `con` can never become `con.conf` |
| Regular file only (no FIFO/device/directory), size cap checked on the open handle | `fstat` + `S_ISREG`, `O_NONBLOCK` | `GetFileInformationByHandle` + `GetFileType == FILE_TYPE_DISK` |
| Mode files owned by the current user | `st_uid == geteuid()` | owner SID equals the user's token SID or `BUILTIN\Administrators` (files written by an elevated process are owned by that group) |
| Private directories/files (`0700`/`0600`) | `mkdir(.., 0700)`, `O_CREAT, 0600` | explicit protected DACL (current user + SYSTEM, nothing inherited); if the descriptor cannot be built the object inherits the ACL of `%APPDATA%`, which is already limited to the user, SYSTEM and Administrators |
| Atomic config save (temp file + rename, old file survives a failure) | `openat` + `renameat` + `fsync` | temp file in the same directory + `MoveFileExW(REPLACE_EXISTING, WRITE_THROUGH)`, retried briefly on sharing violations |
| Symlinked config keeps the link when saved | yes (link resolved up to 16 levels) | the link is resolved once with `GetFinalPathNameByHandleW`; the target is replaced, the link stays |
| Directory swapped between listing and opening mode files | closed by `openat` on a held directory descriptor | **not closed**: files are opened by path (no `openat` equivalent). An attacker needs write access to the parent directory, i.e. already is the user (A2) |
| Mode bits of an existing config are kept, setuid/sticky dropped | yes | not applicable (ACL of the new temp file, which is private) |
| Legacy-config migration (`spnav-scroll` names) | yes (Linux and macOS) | not applicable, never shipped there |

Known gaps, stated plainly:

- **Administrators-owned files are accepted on Windows.** The "owned by the current user" check
  for mode files also accepts the `BUILTIN\Administrators` owner, because files written by an
  elevated process are owned by that group. Any local administrator can already act as the user,
  so this adds no capability, but it is weaker than the POSIX `st_uid == geteuid()` check.
- **No held directory handle on Windows.** `DirHandle` keeps only the directory path; files in it
  are opened by path (there is no `openat`). A directory replaced between listing and opening mode
  files is not detected. Closing it would need `NtCreateFile` with a root directory handle, which
  is not used here.
- Cloud placeholders (OneDrive "files on demand") and other reparse points are refused, so a
  configuration or mode directory inside a cloud-synced folder that uses placeholders must be
  made available offline first.
- Normal UNC paths (`\\server\share\...`) are accepted, the verbatim forms are not; network
  shares have different ACL semantics and the private-DACL guarantees are not checked there.

- The Windows code is compiled with MSVC and MinGW and its pure logic and the file operations are
  exercised by `test_fs` and the other tests on the GitHub `windows-latest` runner (and locally
  under Wine for MinGW builds). Symlink refusal can only be tested where the CI user may create
  symlinks; the test skips otherwise. The owner-SID and DACL code paths have no negative test
  (a file owned by another account cannot be created on the runner).
- Only the final path component is protected against links (as on Linux): a symlinked or
  junctioned *parent* directory is followed. This is covered by the same argument as S8/A2.
- Windows hardening flags (`cmake/windows.cmake`): MSVC builds use `/GS`, `/guard:cf` (control-flow
  guard is not a default), `/DYNAMICBASE`, `/NXCOMPAT`, `/HIGHENTROPYVA` and `/DEPENDENTLOADFLAG:0x800`
  (system32-only DLL search); MinGW builds use `--dynamicbase`, `--nxcompat`, `--high-entropy-va` and
  `-fstack-protector-strong`. `main` also calls `SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32)`
  first. The application manifest is `asInvoker`, long-path aware, UTF-8 code page. These flags are
  compile/link-checked by the CI runners only; the `-fstack-protector`/`-pie`/`relro` set of the
  Linux build is not applicable. The hardening check script is Linux-only.
- macOS: the ad-hoc signature has the hardened runtime since issue #269 (DYLD_* variables are ignored; see `docs/release-process.md`); notarization and the sandbox/entitlement story belong to packaging
  (package W4), not to this layer.
- Profile file names come from the profile name through a slugger that follows the same reserved-name
  rule as the path layer (`isReservedDeviceStem`: CON, CONIN$, COM and LPT with the superscript digits,
  a space before the dot); if a plain slug were still rejected the writer uses the hashed name. The file
  stamp (mtime, ctime, size, file id) is POSIX-complete; Windows has no ctime in it, so a same-size
  rewrite inside one 100 ns timestamp with a reused file id would go unnoticed there until the next change.

---

## 10. Live feed

The daemon publishes the current axes, buttons and its own interpretation (channel outputs, active
mode) to the UI at 30 Hz (`src/live_feed/`, protocol in `port-architecture.md`). On Windows and macOS
the daemon owns the device exclusively, so the UI cannot read it directly.

- **Transport.** POSIX: Unix domain socket `live.sock` in the runtime directory (if `XDG_RUNTIME_DIR`
  is unset, or the path would not fit into `sun_path`, `socketPathFor` falls back to
  `/tmp/dofscroll-<uid>/`, which is created 0700 and checked like any other socket directory), created 0600 in a
  directory that is verified to be a real directory of the current user without group/other write
  access (same code as the macOS reload socket, `src/platform/unix_socket.cpp`); the peer uid is
  checked on accept (`SO_PEERCRED` / `getpeereid`), and the client refuses a socket or server of
  another user. Limitation: before taking over an existing socket file the daemon probes it with a
  non-blocking connect (500 ms). On Linux a full accept queue (EAGAIN) counts as "alive"; on macOS a
  full accept queue answers ECONNREFUSED like a file without a listener, so a daemon whose accept
  queue is full can look stale to a second instance. Only the same user can create that situation,
  and the daemon's single-instance lock normally stops a second instance before this point.
  Windows: outbound-only named pipe `\\.\pipe\dofscroll-live-<user sid>` with a DACL
  that grants only the current user, `PIPE_REJECT_REMOTE_CLIENTS`, `FILE_FLAG_FIRST_PIPE_INSTANCE`
  (a squatted name makes the daemon fail instead of serving); the client verifies that the pipe
  server runs as the same user.
- **Who can read it.** Any process of the same user can connect and read the sample stream: raw
  SpaceMouse deflection, button state and the active mode id. That is no more than the same process
  could get from the device itself on Linux (libspnav) and is not a secret; it contains no keystrokes,
  no window titles and no file content. Other users, remote machines and sandboxes without access to
  the runtime directory cannot connect.
- **No commands.** The feed is one-way. The server never reads from a connection (on Windows the
  pipe is outbound-only), so there is no parser, command or state change reachable from a client.
  Changing settings stays with the config file and the reload channel.
- **Denial of service.** The server never blocks the tick thread: non-blocking accept and write,
  at most 4 clients, frames are dropped for slow clients and the client is disconnected after 30
  consecutive dropped frames, the rate is limited to 30 Hz (maximum 60) and nothing is encoded while
  no client is connected. `LIVE_FEED = 0` switches the feature off; connections are then closed
  immediately.
- **Untrusted input in the client.** The client treats the stream as untrusted although it is local:
  magic, version, frame length (at most 78 payload bytes), every field range, NaN/Inf, reserved bits
  and the mode id characters are validated, the read buffer is bounded and any violation drops the
  connection (reconnect with backoff).

## Known limitations

These findings of the [October 2026 review](security-review-2026-10.md) stay open for 0.1.
Nothing here is hidden by the rest of this document; the review has the details.

- **Stuck keys after SIGKILL (F4).** Under X11, keys held through XTest (Ctrl while zooming) are not
  released when the daemon is killed or crashes, and a stalled tick thread cannot release anything.
  While the daemon runs, a safety net releases every key and mouse button it holds after 250 ms
  without any deflected axis or held button, and the stale-input watchdog ends axis output 250 ms
  after the last device event. `ExecStopPost=--release-keys` covers a stop under systemd; outside
  systemd, or if Ctrl stays down, use the tray item "Release all keys" or run
  `dofscroll --release-keys` by hand. With the uinput backend the kernel releases the keys.
- **File permissions (F8).** Neither loader rejects group- or world-writable config or mode files, and
  the config loader does not compare the owner. Keep `~/.config/dofscroll` private (`0700`).
- **Runtime directory (F10).** The daemon unit may write the whole of `$XDG_RUNTIME_DIR`
  (`ReadWritePaths=%t`), although it needs two files there.
- **Sandbox (F11).** The tray cannot save a config that is a symlink into another directory, and
  `AF_NETLINK` and the missing `MemoryDenyWriteExecute` in the tray unit were not tightened. The
  sandbox is an accident-containment measure, not a security boundary.
- **Seeding source (F12).** Examples are copied from a directory chosen by environment or by the
  executable's location, not necessarily root-owned.
- **HID descriptor loader (F13).** `src/hid_descriptor.cpp` is used by the Windows and macOS input
  (`win_hid_input.cpp`, `mac_hid_input.cpp`, `mac_driver_input.cpp`). The file loader refuses links, but
  `loadHidDescriptorDir` lists the directory without an entry cap and no owner check is made. The directory comes from
  `DOFSCROLL_DEVICES` (macOS) or the folder of the executable, so exploiting it needs control of the environment
  or of the installation folder.
- **Runtime files (F14).** Temporary file names in the runtime directory are predictable; exploiting
  that needs write access to the user's own runtime directory.

## 11. Text and encoding

All text is UTF-8 and is handled byte-safe (`docs/encoding.md`). Guarantees: no string is cut inside
a multi-byte sequence, log and status lines never contain control characters (`sanitizeForLog`),
and display text from files (mode name, description) has control characters removed and invalid
bytes shown as U+FFFD. Known limit: look-alike (homoglyph) and bidirectional control characters are
valid text and are shown as they are. A profile or mode name can therefore look like another one
in the UI or in the log; matching is exact for every non-ASCII byte, so a look-alike never matches
the profile it imitates. Only use mode files from sources you trust (see section 7).

## 12. Several users on one PC

The 3D mouse is one shared device. When two users are logged in at the same time (fast user
switching, remote desktop, multi-seat) every user's dofscroll daemon can receive its events.

- **Isolated per user:** config, modes, status, log and suspend files (private runtime directory),
  the X11/uinput/CGEvent/SendInput output, the profile matching and the reload channel. One user's
  settings never change another user's daemon.
- **Shared:** the device and its event stream. Linux: `spacenavd` is one system daemon that feeds
  the clients of every user. Windows and macOS: the HID device is read by every process that opens it.
- **What the pause does:** each daemon asks the operating system once per second whether its own
  session is the active one (Linux: the logind session file `/run/systemd/sessions/<XDG_SESSION_ID>`,
  `ACTIVE=0` or `STATE=online|closing`; Windows: not the console session and WTS connect state not
  active; macOS: `kCGSessionOnConsoleKey` false). While it is not, the daemon behaves like the
  tray's pause: it releases everything it holds, ignores the device, writes `session=inactive` to the
  status file and logs `session inactive: output paused` once. It resumes with
  `session active: output resumed`, without replaying motion from the time it was paused.
  The Linux probe reads one small root-owned file without following links (the session id is
  validated as a short file name), and adds no libsystemd or D-Bus dependency.
- **Limits:** the pause stops the output, not the reading: another user's daemon can still read the
  device events (the same is true for any process of that user that talks to `spacenavd` or opens
  the HID device). If the state cannot be determined (no `XDG_SESSION_ID`, unreadable file, failed
  query) the daemon counts as active, so detection problems never silence it. A locked screen of the
  active user is not detected on Windows and macOS. The vendor driver may handle several sessions
  differently; this was designed from the documented APIs and not tested with a second user session.

## 13. Loading the driver interface (Windows and macOS)

With `INPUT = driver` (or `auto` when the driver is installed) the daemon loads code that dofscroll
does not own: the client library of the installed official 3Dconnexion driver. That is a new
attack surface (code that runs inside the daemon, which can inject keys and clicks), so the loading
is restricted, fixed and logged. Background and the legal side: [legal-notes.md](legal-notes.md).

- **Fixed locations only.** macOS: `/Library/Frameworks/3DconnexionClient.framework/3DconnexionClient`.
  Windows: the COM class registered for the ProgID `TDxInput.Device`. Nothing comes from `PATH`,
  the current directory, the library search path, an environment variable, a config key or a
  command-line option: **no setting can redirect the library** (that would be a code-injection hole
  for anyone who can edit a config file). The tests load a fake driver through an explicit path
  argument of a test-only factory that the daemon never calls.
- **macOS checks before `dlopen`:** the path is resolved (`realpath`); the result must lie inside the
  framework directory (no `..`, links out of it are refused), be a regular file, be **owned by
  root** and **not writable by group or others**. Otherwise the driver path counts as not installed
  and the reason is logged.
- **Windows checks before the COM class is created:** the ProgID and class are read from
  `HKEY_LOCAL_MACHINE` only (admin-writable); the class must **not be overridden for the current
  user** (`HKEY_CURRENT_USER\Software\Classes` is where a non-admin program could plant its own
  server); the server file (or, for a managed server, the code base file; its host must be a system
  DLL) must exist, and its final path after links and junctions must lie **below Program Files** (the
  pure path rules reject relative, UNC, device paths, `..` and alternate data streams). A GAC
  assembly (no code base) is accepted.
- **What these checks do not do:** they do not verify a signature or the identity of the vendor; they
  only keep a different, user-writable file from being loaded in place of the installed driver. An
  administrator or malware with administrator rights can replace the driver; so they can replace
  dofscroll. On macOS a notarized, hardened-runtime build needs the entitlement
  `com.apple.security.cs.disable-library-validation` to load a library signed by another team; this is
  a trade-off to be decided when the DMG is signed (`release-process.md`), and the unsigned DMG
  of today does not need it. Under the App Sandbox, registering with the driver fails (reported
  publicly); dofscroll is not sandboxed.
- **Failure modes:** driver absent, library or class refused by the checks, interface member
  missing ("interface mismatch"), registration or `Connect` refused, driver stops answering. In every
  case the path simply does not open: with `auto` the other path is used, with `driver` the daemon
  logs why and retries with backoff; nothing is loaded partially, and the daemon never falls back to a
  different library. A driver that stops answering makes the source report "closed" and the daemon
  reconnects; held keys and motion are released first.
- **What runs where:** driver callbacks (macOS) arrive on a driver thread or on the daemon's own
  run loop; they only write into a bounded queue. Windows polls on one thread of its own.
- Everything else about the device stream is as in section 12: the driver's interface is shared by
  all sessions of the machine.

## 14. Third review (2026-10-21, before 1.1.1)

A code review and a security audit of everything since 1.0.0 (multi-device code, upgrade script, interface, installers,
workflows). Nothing high or critical, no local privilege escalation. Fixed in 1.1.1:

- **Import of a joystick mode or button mode file** now writes the content that was checked and shown (with its warnings)
  to the user, not the file read a second time (`importInspected()`); a file that changes between the check and the
  click cannot bring other content in.
- **Display text from files, programs and devices** (profile names and match strings, device names, mode texts,
  configuration path, action texts) is shown as plain text in the labels that carry it. Before, a name such as
  `<img src=...>` could be rendered as rich text.
- **Upgrade scripts (`packaging/cpack/preinst` and `postinst`, run as root):** `preinst` stops the user units
  (interface, then daemon) and `postinst` starts them again, for every logged-in user; no file is written. A user name must not start with `-`; every
  call is time-limited, and when `timeout` is missing nothing is done; a process is signalled only after its owner is
  checked again right before the signal (process numbers can be reused); an interface is stopped only for a user who
  has the unit enabled.
- **Windows:** the installer and the interface start Windows PowerShell by its full path (`$WINDIR\System32\...`,
  `%SystemRoot%`), not by name; the uninstaller removes the folder only when `dofscroll.exe` is in it, and the folder
  picker refuses a folder that is not empty and not a dofscroll install (a silent install with `/D=` is not checked).

Fixed afterwards (issue #150): the interface, the autostart code and the reload request no longer start `systemctl` and
`launchctl` through `PATH`. They use the same lookup as the daemon's helper programs (`findHelper()` in
`src/platform/exec_lookup.hpp`: `/usr/bin`, `/bin`, `/usr/sbin`, `/sbin`, only absolute directories, only executable regular
files that are root-owned and not group/other-writable; `/usr/local/bin` is searched last and only for the optional
helper `kdotool`); a helper that is not there is an error, never a `PATH` fallback. The argv plans in
`src/ui/model/restart_plan.hpp` stay plain names, `resolveHelperArgv()` (unit-tested) turns them into paths right before
the start. Windows PowerShell is started by its full path in the installer, in the interface and in
`scripts/test-driver-coexistence.ps1`.

CI supply chain (issue #149), status after the fix:

- Fixed: the container images of the release builds (Rocky 9 Qt image, Debian 12, and the `FROM` of the Rocky Dockerfile)
  are pinned by digest in `packaging/ci/images.env` (together with the images of the CI jobs; the workflows read the file,
  so the refresh changes no workflow file); `scripts/pin-ci-images.sh` refreshes them and the weekly `qt-update-check` run warns when they
  are out of date. aqtinstall and all its dependencies are installed with `pip install --require-hashes` from
  `packaging/ci/aqt-requirements.txt` (every Qt install: CI image, Debian 12, Windows, macOS, `ui.yml`).
  `scripts/check-ci-pins.sh` keeps these pins from being removed. Every release file, including `SHA256SUMS.txt`,
  carries a signed build provenance attestation (`actions/attest-build-provenance`, commit-pinned, `id-token` and
  `attestations` write permissions only on the release job); verify with `gh attestation verify <file> -R
  HoZe-Lukas/dofScroll` (readme, `docs/releasing.md`). A checksum file made in the same pipeline is still no
  provenance by itself: the attestation is.
- Remains: aqtinstall verifies each Qt archive against a SHA-256 that it fetches from download.qt.io too, so a
  compromised Qt server could serve both; there is no independent per-archive hash of our own. The one source
  distribution in the aqtinstall closure (`patch`) is built by pip with an unpinned build backend (setuptools),
  which `--require-hashes` does not cover. The pins are refreshed monthly by a reviewed PR (`refresh-ci-pins.yml`, never auto-merged; the generators run in a read-only job on a copy without `.git`, the job with the write token only commits three named files from the artifact; a
  tampered image published before a refresh would be pinned as it is, so review that PR and the monthly image build). The jobs of `ci.yml` and CodeQL use the same pinned images; only
  the install smoke test still uses the distribution images by tag (it tests the
  current distributions on purpose and produces no release file). Windows and macOS
  runner images are GitHub's own and not pinned.

Open, tracked as issues: root removes a dangling
tray link through a glob in a directory the user controls (only that fixed name, no practical harm); look-alike texts
(see section 11). Plain `key:ctrl`, `key:super`, `key:enter` and `keys:` pairs get no warning on load, only chords do
(known, F2).
