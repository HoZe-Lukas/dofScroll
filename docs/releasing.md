<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Releasing

A release is built and published by GitHub Actions when a tag `v<version>` is pushed.

1. Refresh the pins: start the workflow `refresh-ci-pins` (Actions tab; it also runs monthly) and merge the pull
   request "Refresh CI pins" (close and reopen it once so that CI runs) so the
   release uses the newest tested image digests and aqtinstall hashes. If there is none or you
   changed `packaging/ci/` or `AQT_VERSION`, run `scripts/pin-ci-images.sh` and
   `scripts/update-aqt-requirements.sh` yourself (see "Pinned build inputs" below) and merge the result.
   Action SHAs are refreshed by Dependabot: merge its open pull requests too.
2. Documentation: bring the wiki (`docs/wiki/`, published to the GitHub wiki), the website (`site/` and
   `site/de/`), `readme.md`, the other READMEs (`packaging/windows/README.md`, `packaging/macos/README*`,
   `packaging/README-uinput.md`) and the agent files (`AGENTS.md`) up to date. Check their statements against the
   code and the release notes, not against the previous version: tested platforms and devices, status and known
   limitations, new features, version numbers, links. `scripts/check-site.sh` keeps both website languages in parity. Check the screenshots too: every picture that shows a changed part of the interface must have been regenerated (`scripts/make-site-screenshots.sh`).
3. Set the new version in `CMakeLists.txt` (`project(dofscroll VERSION x.y.z ...)`), write
   `docs/release-notes-x.y.z.md` (see "Release notes" below) and merge both to `main`; wait for CI to be green.
4. Tag and push: `git tag v<x.y.z> && git push origin v<x.y.z>`
   (a suffix such as `v0.2.0-rc1` is allowed; the tag must start with the CMake version).
   Without a shell (for example on a phone), run the workflow `tag-release` from the Actions tab or the GitHub
   app on branch `main` with the version (for example `1.2.2`): it checks the version against `CMakeLists.txt`
   and that the checks of the tip of `main` are green, creates the tag there and starts the `ci` run on it.
5. The workflow `.github/workflows/ci.yml` runs all checks, builds RPM, DEB and TGZ packages,
   writes `SHA256SUMS.txt`, attests the build provenance of every file (signed with Sigstore, needs
   no secret) and creates the GitHub Release with generated notes. Versions `0.x` and
   tags with a suffix are marked as pre-release.
6. Users download the files from the repository's **Releases** page
   (`https://github.com/HoZe-Lukas/dofScroll/releases`); the newest stable one is also reachable
   under `/releases/latest`.
7. Check the open issues against the release (`gh issue list --state open`): comment what the release changed,
   close an issue only when it is fully fixed and needs no check on real hardware, leave the rest open and say what
   is missing. Add the release to the issues it fixes.

Windows and macOS installers (unsigned) are built by the same workflow; see
[release-process.md](release-process.md).

If the tag does not match the CMake version the release job fails before publishing anything.
A failed release can be repeated by deleting the tag and the draft release and tagging again.

## Release notes

`docs/release-notes-<version>.md` follows the style of the Home Assistant release notes: a reader sees what is new
first and can look up every change afterwards. Order of the sections (leave one out when it is empty):

1. SPDX header, title `# dofscroll <version>`, and the quote with the tested platforms (copy it from the last
   release and update it).
2. A short intro (what kind of release, link to the previous notes, whether the configuration format changes),
   then a **Highlights** list of three to five bullets.
3. One section per notable change, with what the user sees and where to read more (`ui.md`, `troubleshooting.md`,
   ...). Fixes go in sections of their own.
4. **Backward-incompatible changes**: everything that changes behavior for existing installations (configuration,
   units, defaults). Say "None" when there is nothing.
5. **All changes**: one line per merged pull request with its number. Contributions from other people carry
   `by @handle` and, if there is one, `reported in #issue`. Maintainer pull requests get only the number.
6. **Thank you**: thank every external contributor and reporter by GitHub handle and say what they did.
7. **Not verified**: what only a human on real hardware can check (see AGENTS.md, "Verifying a change").
8. **Known limitations** with the issue numbers.

Get the list for "All changes" from `gh pr list --state merged --search "merged:>=<date of the last release>"`.
Names and email addresses never go in; the GitHub handle is enough.

## Verifying a release

`SHA256SUMS.txt` is made in the same pipeline as the files, so it only detects a damaged download. The
proof of origin is the build provenance attestation: it says which workflow, in which repository and
for which tag and commit, built a file. With the GitHub CLI:

    gh attestation verify dofscroll-<version>-1.x86_64.rpm -R HoZe-Lukas/dofScroll

(any release file; add `--signer-workflow HoZe-Lukas/dofScroll/.github/workflows/ci.yml` to insist on
the release workflow). It works offline against a bundle too (`gh attestation download`, then
`--bundle`). A failure means the file was not built by this repository's release job: do not install it.

## Pinned build inputs

The release builds do not take their inputs from moving tags:

- The container images of the CI and release builds (`packaging/ci/images.env`, read by the job `pins` of
  `ci.yml`, `codeql.yml` and `linux-packages.yml`; the `FROM` of `packaging/ci/rocky9.Dockerfile`) are pinned by
  digest. They are not in the workflow files, so refreshing them changes no workflow. The monthly `ci-image` run
  publishes a new `dofscroll-ci-rocky9(-qt):latest`, but the builds keep the pinned digest until you refresh it:
  run `scripts/pin-ci-images.sh` (needs `skopeo` or `crane`), review and commit the diff, and let
  CI run. Do this before tagging a release, and after every change to `packaging/ci/` that
  `ci-image` has published. `scripts/pin-ci-images.sh --check` only reports. Normally you do not do
  this by hand: the monthly `refresh-ci-pins.yml` (or a manual start of it) runs both scripts and opens (or updates) the pull request
  "Refresh CI pins", never merged automatically. The refresh PR is merged before a release so the release
  uses the newest tested versions. The job changes only files under `packaging/ci`, so the default token
  suffices. A PR created with it does not start CI: close and reopen the PR once (the optional secret
  `REFRESH_PINS_TOKEN`, a fine-grained token with contents and pull requests write, would start it by itself).
  The workflow needs the repository setting that lets Actions create pull requests.
- aqtinstall and its dependencies come from `packaging/ci/aqt-requirements.txt` (versions and SHA-256,
  `pip install --require-hashes`). After a change of `AQT_VERSION` in `packaging/qt.env`:
  `scripts/update-aqt-requirements.sh`, commit the result (it must run on Python 3.10, the oldest Python of the builds, and needs
  network access; see the header of the script for a Docker one-liner; py7zr 1.1 needs 3.10 or newer, so the Rocky image uses `python3.11`).
- `scripts/check-ci-pins.sh` (a unit test, so also `check-fast`) fails when an image or aqtinstall pin is
  missing, when `aqt-requirements.txt` does not match `qt.env`, or when the release job loses its
  attestation step.
