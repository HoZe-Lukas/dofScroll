<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Status of the work for release 1.2.4 (written 2026-10-27, evening)

Rule of the maintainer: **1.2.4 is only released when everything is finished, reviewed and audited. Nothing ships half
done.** Tracking issue: #230 (checklist). Version 1.2.3 is released (2026-10-27).

## Merged into main since 1.2.3

- #212 and #213 self-test on Wayland skips the info popover checks (part 1 of #193; the default size check can still fail).
- #205, #209, #210, #211, #214, #223 CI: pins in `packaging/ci/images.env`, monthly refresh that approves its own checks,
  `ci-image` ignores the pins file, retries and timeouts for apt in the ui jobs.
- #208 German texts use "du" (`scripts/check-german-du.sh`).
- #206 Profiles page without the separate Buttons tab; the settings search filters the button rows.
- #215 profile editing: per-motion inherited / set / cleared, Clear, Switch on, new optional profile keys `V_ACTION`,
  `H_ACTION`, `TWIST_ACTION`, `V_PUSH_ACTION`, `V_TILT_ACTION`, `H_PUSH_ACTION`, `H_TILT_ACTION`; program `BUTTON_<n>` now
  beats the joystick mode's binding (behavior change for the release notes).
- #216, #219 plan of the split configuration (`docs/plan-config-split.md`, with PRIORITY); #231 plan for any function on any
  motion and own functions (`docs/plan-motion-functions.md`).
- #218 config split core (stages 1 and 2: `src/profile_files.*`: slugger, loader, writer, stamps, PRIORITY), #220 stage 3
  (migration and seeding, `src/profile_migrate.*`).

## Open pull requests and the order to merge them

1. **#232** `fix/review-findings-1`: fixes of the code review (a program profile did not take effect on a profile switch
   because `applyProfile` compared `allKeys()`; minor findings) and of the security audit F1 to F10 (migration failed from 101
   profiles per kind, migration silently dropped a legacy section shadowed by a file, unreadable `devices/`/`apps/` treated
   as missing, legacy path read foreign `devices/`/`apps/`, slugger knew fewer Windows reserved names, ...). The last known
   red check was MSVC warning C4100 in `tests/test_profile_hardening.cpp` (unused parameter on Windows); a fix was pushed
   (head 62aa7e4), CI was still running. **Merge first.**
2. **#222** `cfg/split-ui` (stage 4): the UI reads and writes one file per profile, switches the migration on
   (`kSplitLayoutInUi = true`), defective files, PRIORITY move up/down, delete, rename, export as file copy, import with safety
   rules (strips PRIORITY/ENABLED/BACKEND/TICK_HZ/INPUT, confirms short names and ctrl/alt/super chords), display
   sanitizing. CI green. Merge after #232 (the migration warnings need its fixes). Open question: an imported profile is never
   placed above the user's own, but if all existing profiles have negative PRIORITY an imported 0 can end up above.
3. **#221** `ui/profile-borrow-build`: take over a motion's or button's assignment from another profile or mode as a COPY
   (push and tilt individually; profile tab and joystick mode editor), zoom on any motion (the daemon now pauses the other
   wheel motions while a split part zooms), profile builder ("New profile...": kind, name, base, joystick mode, button modes
   by name or own copy, single motions/buttons taken over). CI green. It touches the same profile pages as #222: rebase
   onto main after #222 and resolve carefully. Open question: copy only the action or also the tuning (sensitivity, dead
   zone, curve, smoothing, direction)? Recommendation: an option in the dialog "also take over the tuning".
4. **#217** `docs/config-split-docs` (draft, merge LAST): docs of the split layout, examples/split, release-note draft. It must
   be updated to the real state: the profile list is in applied order (PRIORITY) not alphabetical, import rules, take over,
   profile builder, new profile keys; it describes behavior in present tense.

## Not started yet (waiting for decisions): #224 and #225

Specification in `docs/plan-motion-functions.md`: every function on every motion with analog or digital control (an
analog/digital check box for key-like functions: analog = key repeat rate follows the deflection, digital = one press;
device buttons stay digital), lift as a motion, new functions (pointer, clicks, hold, volume, tab switching as presets), and own
functions defined in the profile (inline first). Stage 1 (golden-master tests of today's output) can start immediately, it
changes no behavior. Decisions still needed from the maintainer (recommendations in the plan): reject analog keys with
dangerous keys; pointer on all systems or X11/uinput first (the maintainer wants all systems, which makes the untested Windows
and macOS paths part of the release); `ALLOW_RISKY_KEYS` gate only for new motion actions, `seq` and `func:`; order of the
stages.

## Other open points

- Wiki page Requirements still has old statements about the Windows driver coexistence; check against the first Windows
  tests. The website does not mention the macOS cleanup survey (#184). German website: "du" everywhere is done.
- QTBUG text for the Wayland SHM overflow (part 2 of #193) is prepared (scratchpad file qtbug-wayland-shm-overflow.md of the
  session; ask for it again or rewrite from #193); someone with a Qt account must submit it.
- Security audit: fuzzing ran without ASan/UBSan (no libasan on the review host): the sanitizer runs happen in CI (the
  ASan/UBSan job is green on the PRs), but a dedicated sanitizer fuzz run of the loader and migration is still to do.
- Not checked by review and audit: the Qt pages, real Windows and macOS execution, real hardware.
- Header comment written by `saveProfiles` in `dofscroll.conf` ("Sections: [global], [device:NAME], [app:NAME]") is outdated.

## Before the release (checklist #230)

Everything above merged and reviewed again (a second code review and audit of #222, #221 and whatever #224/#225 add), version
1.2.4 in `CMakeLists.txt`, release notes in the Home Assistant style (highlights; backward-incompatible: program buttons beat
joystick-mode bindings, new profile keys, PRIORITY, automatic migration with backup in `backup/dofscroll.conf.pre-split`,
downgrade by hand with `cat devices/*.conf apps/*.conf >> dofscroll.conf`; all changes with credits; thank you; not verified;
known limitations), wiki, website (EN and DE), readmes, `AGENTS.md`, screenshots (the site images show only the joystick
modes page and the overview; regenerate if a shown page changes), refresh the CI pins first (run the workflow
`refresh-ci-pins`), tag with `tag-release`, then check all open issues (`docs/releasing.md`).

## What only a human on real hardware can verify

Migration of a real configuration and the first start on a fresh account on Linux, Windows and macOS; the Qt dialogs
(import/export file dialogs, take over, new profile) and defective-file entries on a real desktop (large fonts, dark mode);
zoom from tilt (feel, direction), analog and digital keys and pointer movement once built; Wayland; the installer upgrade on
Windows and macOS.
