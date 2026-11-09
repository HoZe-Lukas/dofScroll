<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Release checklist 1.3.2 (the maintainer's steps by hand)

The one-time repository settings (environment `release`, the tag ruleset, immutable releases: section A of
[release-checklist-1.3.1.md](release-checklist-1.3.1.md)) and the pin hashes (section B there) were done for 1.3.1; this list
only says what is new. What the release contains and what is not verified: [release-notes-1.3.2.md](release-notes-1.3.2.md).
Work top to bottom; tick a box only after you saw the result. Commands use the GitHub CLI (`gh`), logged in as the maintainer.

    export REPO=HoZe-Lukas/dofScroll

**Rule of the maintainer: the release is only made when everything is finished, reviewed and audited.** Nothing ships half done.

## A. Before the tag

- [ ] **A1. Settings are still as in 1.3.1.** `gh api repos/$REPO/rulesets` lists `main branch` and `release tags`; the
  environment `release` still has the required reviewer (Settings > Environments).
- [ ] **A2. Refresh the CI pins.** Start `refresh-ci-pins` (Actions tab), merge the pull request "Refresh CI pins" when its
  checks are green, merge the open Dependabot pull requests. `scripts/check-ci-pins.sh` stays green.
- [ ] **A3. Open pull requests.** `gh pr list --repo $REPO`: merge or close each (for example #371, which removes
  `configure-repo.sh`); the release notes list what is in.

## B. Tests that need a person

- [ ] **B1. The driver cleanup assistant on real Windows** (issue #184, pull request #352) with the real driver folder:
  open it, tick a few programs with the mouse and with Space, read the preview, apply, see the backup folder, restore it,
  apply again. Look at the check boxes in light and dark. Note what the driver does with the changed files.
- [ ] **B2. Restart the 3Dconnexion driver on real Windows**, from the General page and from the last page of the
  assistant: the settings window closes, `3DxService.exe` is a new process, the SpaceMouse works again within seconds, the
  driver's settings window opens again from the tray. Does the running dofscroll daemon pick the driver up again? (If not,
  that is a finding for the notes.)
- [ ] **B3. Restart the driver on a Mac** (never run): General page > *Restart the 3Dconnexion driver...*. If there is no
  Mac, keep the sentence "never run on a Mac" in the notes.
- [ ] **B4. #354 on the driver path.** Compare the signs of push, tilt and twist on the driver path with the direct HID
  path (the direct path was measured on one Pro: see the notes). **#355** needs a Pro with buttons:
  `dofscroll-hidprobe --dump` while you press them.
- [ ] **B5. X11 layout change** (pull request #369): start the daemon on `us`, switch to `de` with `setxkbmap de`, run the
  built-in `map` mode: `minus` must type `-`.
- [ ] **B6. The installers.** Run the `installers-dry-run` workflow on `main`, install the Windows setup on a machine that
  has no Visual C++ runtime in System32 if you can get one (issue #363), and the DMG on a Mac.

## C. Version and documents (done in the pull request that carries this file)

- [x] `CMakeLists.txt`, `packaging/windows/dofscroll.manifest`, `readme.md`, `docs/wiki/Home.md`,
  `docs/wiki/Release-notes.md`, `site/index.html` and `site/de/index.html` (`softwareVersion`) say 1.3.2;
  `scripts/check-version.sh` is green.
- [x] `docs/release-notes-1.3.2.md`. The website screenshots were **not** rendered again: no picture shows a page that changed
  in this release (the Joystick modes page and the Overview); the German ones are still stale (issue #344).
- [ ] **C1.** Move what you tested in B out of "Not verified" in the notes, the readme and the wiki, and leave what you did not.
- [ ] **C2.** All checks of the tip of `main` are green (`gh run list --repo $REPO --branch main --limit 10`).

## D. The tag, the approval, the check

As in section F of [release-checklist-1.3.1.md](release-checklist-1.3.1.md) with `v1.3.2`: tag by hand as the
administrator (`git fetch origin && git tag v1.3.2 origin/main && git push origin v1.3.2`), approve the release in the
Actions run, then `gh release view v1.3.2 --repo $REPO`, `sha256sum -c --ignore-missing SHA256SUMS.txt` and
`gh attestation verify dofscroll-1.3.2-1.x86_64.rpm -R $REPO --signer-workflow $REPO/.github/workflows/ci.yml`.
An immutable release cannot be repeated: a mistake means 1.3.3, so decide before you approve.

## E. After the release

- [ ] **E1.** Check every open issue against the release (`docs/releasing.md`, step 7): comment what changed, close an issue
  only when it is fully fixed and needs no check on real hardware. #184 has its Windows part in this release; leave it open
  for macOS.
- [ ] **E2.** Open the website (English and German, light and dark) and the wiki after they published themselves.
