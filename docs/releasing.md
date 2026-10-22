<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Releasing

A release is built and published by GitHub Actions when a tag `v<version>` is pushed.

1. Refresh the pins: merge the open pull request "Refresh CI pins" (weekly, `refresh-ci-pins.yml`) so the
   release uses the newest tested image digests and aqtinstall hashes. If there is none or you
   changed `packaging/ci/` or `AQT_VERSION`, run `scripts/pin-ci-images.sh` and
   `scripts/update-aqt-requirements.sh` yourself (see "Pinned build inputs" below) and merge the result.
   Action SHAs are refreshed by Dependabot: merge its open pull requests too.
2. Set the new version in `CMakeLists.txt` (`project(dofscroll VERSION x.y.z ...)`) and merge it
   to `main`; wait for CI to be green.
3. Tag and push: `git tag v<x.y.z> && git push origin v<x.y.z>`
   (a suffix such as `v0.2.0-rc1` is allowed; the tag must start with the CMake version).
   Without a shell (for example on a phone), run the workflow `tag-release` from the Actions tab or the GitHub
   app on branch `main` with the version (for example `1.2.2`): it checks the version against `CMakeLists.txt`
   and that the checks of the tip of `main` are green, creates the tag there and starts the `ci` run on it.
4. The workflow `.github/workflows/ci.yml` runs all checks, builds RPM, DEB and TGZ packages,
   writes `SHA256SUMS.txt`, attests the build provenance of every file (signed with Sigstore, needs
   no secret) and creates the GitHub Release with generated notes. Versions `0.x` and
   tags with a suffix are marked as pre-release.
5. Users download the files from the repository's **Releases** page
   (`https://github.com/HoZe-Lukas/dofScroll/releases`); the newest stable one is also reachable
   under `/releases/latest`.

Windows and macOS installers (unsigned) are built by the same workflow; see
[release-process.md](release-process.md).

If the tag does not match the CMake version the release job fails before publishing anything.
A failed release can be repeated by deleting the tag and the draft release and tagging again.

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

- The container images of the release builds (`linux-packages.yml`, the `FROM` of
  `packaging/ci/rocky9.Dockerfile`) are pinned by digest. The weekly `ci-image` run publishes a new
  `dofscroll-ci-rocky9(-qt):latest`, but the release builds keep the pinned digest until you refresh it:
  run `scripts/pin-ci-images.sh` (needs `skopeo` or `crane`), review and commit the diff, and let
  CI run. Do this before tagging a release, and after every change to `packaging/ci/` that
  `ci-image` has published. `scripts/pin-ci-images.sh --check` only reports. Normally you do not do
  this by hand: the weekly `refresh-ci-pins.yml` runs both scripts and opens (or updates) the pull request
  "Refresh CI pins", never merged automatically. The refresh PR is merged before a release so the release
  uses the newest tested versions. A PR created with the default token does not start CI: set the secret
  `REFRESH_PINS_TOKEN` (fine-grained token, contents and pull requests write) or close and reopen the PR.
  The workflow needs the repository setting that lets Actions create pull requests.
- aqtinstall and its dependencies come from `packaging/ci/aqt-requirements.txt` (versions and SHA-256,
  `pip install --require-hashes`). After a change of `AQT_VERSION` in `packaging/qt.env`:
  `scripts/update-aqt-requirements.sh`, commit the result (it must run on Python 3.10, the oldest Python of the builds, and needs
  network access; see the header of the script for a Docker one-liner; py7zr 1.1 needs 3.10 or newer, so the Rocky image uses `python3.11`).
- `scripts/check-ci-pins.sh` (a unit test, so also `check-fast`) fails when an image or aqtinstall pin is
  missing, when `aqt-requirements.txt` does not match `qt.env`, or when the release job loses its
  attestation step.
