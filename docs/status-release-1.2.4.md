<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Status of the work for release 1.3.0 (written 2026-10-27, evening; updated 2026-10-28)

Rule of the maintainer: **1.3.0 (first planned as 1.2.4) is only released when everything is finished, reviewed and audited. Nothing ships half
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

## Merged since (the list above was written on 2026-10-27; state of 2026-10-28, `main` at 203f332)

- #232 review and audit fixes of the config split; #222 stage 4 (the interface writes one file per profile, the migration is
  switched on); #237 second review of #222; #221 take over, any action on any motion, New profile builder; #256 take over
  also the tuning; #277 dialog and accessibility fixes.
- #234 golden master of the motion handling (#224 stage 1); #257 lift as the sixth motion, global and device as two layers
  (stage 2); #259 the key engine, analog and digital keys on every motion (stage 3); #236 relative pointer movement in the
  output layer (the output part of stage 5, no action uses it yet).
- #255 key picker as a tree in columns; #235 axis assistant; #248 lift/press warning; #239 live feed device id and selector.
- #251 test coverage; #261 website accessibility; #267 and #279 audit rounds 2 and 3; #278 `refresh-ci-pins` in two jobs.
- Decisions of 2026-10-28 for the rest of the motion functions (pointer on all systems, no `ALLOW_RISKY_KEYS` gate, macro
  function, raised limits) are in `docs/plan-motion-functions.md`, which says what is implemented and what is planned.

The release is **1.3.0** (decision of 2026-10-28, not 1.2.4). The draft
of the release notes is `docs/release-notes-1.3.0-draft.md`, the documentation to check is listed in `docs/doc-drift-1.3.0.md`
(both once merged). Not yet implemented and not part of the release unless merged before it: the motion actions `pointer_x` /
`pointer_y`, `click` and `hold:` on a motion, the presets, the `macro:` function and own functions in profiles (stages 4 to 7
of `docs/plan-motion-functions.md`, issues #224 and #225).

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
1.3.0 in `CMakeLists.txt`, release notes in the Home Assistant style (highlights; backward-incompatible: program buttons beat
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
