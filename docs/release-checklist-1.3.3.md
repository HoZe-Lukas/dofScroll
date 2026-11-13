<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Release checklist 1.3.3 (the maintainer's steps by hand)

The one-time repository settings (environment `release`, the tag ruleset, immutable releases: section A of
[release-checklist-1.3.1.md](release-checklist-1.3.1.md)) and the pin hashes (section B there) were done for 1.3.1; this list
only says what is new compared with [release-checklist-1.3.2.md](release-checklist-1.3.2.md). What the release contains and
what is not verified: [release-notes-1.3.3.md](release-notes-1.3.3.md).
Work top to bottom; tick a box only after you saw the result. Commands use the GitHub CLI (`gh`), logged in as the maintainer.

    export REPO=HoZe-Lukas/dofScroll

**Rule of the maintainer: the release is only made when everything is finished, reviewed and audited.** Nothing ships half done.

## A. Before the tag

- [ ] **A1. Settings are still as in 1.3.1.** `gh api repos/$REPO/rulesets` lists `main branch` and `release tags`; the
  environment `release` still has the required reviewer (Settings > Environments).
- [ ] **A2. Refresh the CI pins.** Start `refresh-ci-pins` (Actions tab), merge the pull request "Refresh CI pins" when its
  checks are green, merge the open Dependabot pull requests. `scripts/check-ci-pins.sh` stays green.
- [ ] **A3. Open pull requests.** `gh pr list --repo $REPO`: merge or close each. #378 (package upgrades start only the
  units that were running before, issue #364) is not part of these notes: if it is merged before the tag, add it to
  `docs/release-notes-1.3.3.md` (Highlights, and under "Not verified": no real RPM or DEB upgrade was run) before you tag.

## B. Tests that need a person

- [ ] **B1. Windows log-off (#358).** Hold a key bound to a hold action (or `hold:` in a macro), log off, sign in and read
  `dofscroll.log`: the clean-quit sequence is there and no key stayed down. If it works, close the issue.
- [ ] **B2. systemd duplicate start (#359).** Start `dofscroll` by hand, hold a key it sent, run
  `systemctl --user start dofscroll.service`: the unit exits with 75 and the first daemon keeps its key. Stop the unit
  after a `kill -9` of its daemon: the keys are released as before.
- [ ] **B3. The key capture field** on a real desktop (macro step, New function window, a button action): click, press a
  combination, Esc.
- [ ] **B4. The installers.** Run the `installers-dry-run` workflow on `main`.

## C. Version and documents (done in the pull request that carries this file)

- [x] `CMakeLists.txt`, `packaging/windows/dofscroll.manifest`, `readme.md`, `docs/wiki/Home.md`,
  `docs/wiki/Release-notes.md`, `site/index.html` and `site/de/index.html` (`softwareVersion`) say 1.3.3;
  `scripts/check-version.sh` is green.
- [x] `docs/release-notes-1.3.3.md`. The website screenshots were rendered again for 1.3.2 (#374); they were **not** rendered
  again for the key capture field of this release.
- [ ] **C1.** Move what you tested in B out of "Not verified" in the notes, the readme and the wiki, and leave what you did not.
- [ ] **C2.** All checks of the tip of `main` are green (`gh run list --repo $REPO --branch main --limit 10`).

## D. The tag, the approval, the check

As in section F of [release-checklist-1.3.1.md](release-checklist-1.3.1.md) with `v1.3.3`: tag by hand as the
administrator (`git fetch origin && git tag v1.3.3 origin/main && git push origin v1.3.3`), approve the release in the
Actions run (environment `release`), then `gh release view v1.3.3 --repo $REPO`, `sha256sum -c --ignore-missing SHA256SUMS.txt` and
`gh attestation verify dofscroll-1.3.3-1.x86_64.rpm -R $REPO --signer-workflow $REPO/.github/workflows/ci.yml`.
An immutable release cannot be repeated: a mistake means 1.3.4, so decide before you approve.

## E. After the release

- [ ] **E1.** Check every open issue against the release (`docs/releasing.md`, step 7): comment what changed, close an issue
  only when it is fully fixed and needs no check on real hardware. #358 and #359 have their fix in this release; close them
  only after B1 and B2. #318 and #356 stay open (see "Known limitations" in the notes). #184 stays open for macOS.
- [ ] **E2.** Open the website (English and German, light and dark) and the wiki after they published themselves.
