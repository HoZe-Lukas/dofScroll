<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Release checklist 1.3.4 (the maintainer's steps by hand)

The one-time repository settings (environment `release`, the tag ruleset, immutable releases: section A of
[release-checklist-1.3.1.md](release-checklist-1.3.1.md)) and the pin hashes (section B there) were done for 1.3.1; this list
only says what is new compared with [release-checklist-1.3.3.md](release-checklist-1.3.3.md). What the release contains and
what is not verified: [release-notes-1.3.4.md](release-notes-1.3.4.md).
Work top to bottom; tick a box only after you saw the result. Commands use the GitHub CLI (`gh`), logged in as the maintainer.

    export REPO=HoZe-Lukas/dofScroll

**Rule of the maintainer: the release is only made when everything is finished, reviewed and audited.** Nothing ships half done.

## A. Before the tag

- [ ] **A1. Settings are still as in 1.3.1.** `gh api repos/$REPO/rulesets` lists `main branch` and `release tags`; the
  environment `release` still has the required reviewer (Settings > Environments).
- [ ] **A2. Refresh the CI pins.** Start `refresh-ci-pins` (Actions tab), merge the pull request "Refresh CI pins" when its
  checks are green, merge the open Dependabot pull requests. `scripts/check-ci-pins.sh` stays green.
- [ ] **A3. Open pull requests.** `gh pr list --repo $REPO`: merge or close each. The pull request of this release
  (driver cleanup profiles and the key capture field) is merged.

## B. Tests that need a person

- [ ] **B1. Driver cleanup on Windows (and macOS if you have one).** Run **Remove driver axis settings...**, leave one 3D
  program and one ordinary program (for example a browser) unticked: the profiles page lists both with a ticked box; after
  Finish the Profiles page has an empty profile (`ENABLED = 0`) for each, and dofscroll does not scroll there.
- [ ] **B2. The key capture field** on a real desktop at each place of the list in `docs/ui.md` (keys of a motion, lift and
  press, button action, macro step, New function window): click, press a combination, Esc.
- [ ] **B3. The installers.** Run the `installers-dry-run` workflow on `main`.

## C. Version and documents (done in the pull request that carries this file)

- [x] `CMakeLists.txt`, `packaging/windows/dofscroll.manifest`, `readme.md`, `docs/wiki/Home.md`,
  `docs/wiki/Release-notes.md`, `site/index.html` and `site/de/index.html` (`softwareVersion`) say 1.3.4;
  `scripts/check-version.sh` is green.
- [x] `docs/release-notes-1.3.4.md`. The website screenshots were rendered again with the 1.3.4 build and did not change (the
  pages they show do not contain the changed fields).
- [ ] **C1.** Move what you tested in B out of "Not verified" in the notes, the readme and the wiki, and leave what you did not.
- [ ] **C2.** All checks of the tip of `main` are green (`gh run list --repo $REPO --branch main --limit 10`).

## D. The tag, the approval, the check

As in section F of [release-checklist-1.3.1.md](release-checklist-1.3.1.md) with `v1.3.4`: tag by hand as the
administrator (`git fetch origin && git tag v1.3.4 origin/main && git push origin v1.3.4`), approve the release in the
Actions run (environment `release`), then `gh release view v1.3.4 --repo $REPO`, `sha256sum -c --ignore-missing SHA256SUMS.txt` and
`gh attestation verify dofscroll-1.3.4-1.x86_64.rpm -R $REPO --signer-workflow $REPO/.github/workflows/ci.yml`.
An immutable release cannot be repeated: a mistake means 1.3.5, so decide before you approve.

## E. After the release

- [ ] **E1.** Check every open issue against the release (`docs/releasing.md`, step 7): comment what changed, close an issue
  only when it is fully fixed and needs no check on real hardware. #358 and #359 (fixed in 1.3.3) are closed only after
  their checks (see [release-checklist-1.3.3.md](release-checklist-1.3.3.md)). #318 and #356 stay open (see "Known limitations"
  in the notes). #184 stays open for macOS.
- [ ] **E2.** Open the website (English and German, light and dark) and the wiki after they published themselves.
