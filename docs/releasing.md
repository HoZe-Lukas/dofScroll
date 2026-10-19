<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Releasing

A release is built and published by GitHub Actions when a tag `v<version>` is pushed.

1. Set the new version in `CMakeLists.txt` (`project(dofscroll VERSION x.y.z ...)`) and merge it
   to `main`; wait for CI to be green.
2. Tag and push: `git tag v<x.y.z> && git push origin v<x.y.z>`
   (a suffix such as `v0.2.0-rc1` is allowed; the tag must start with the CMake version).
3. The workflow `.github/workflows/ci.yml` runs all checks, builds RPM, DEB and TGZ packages,
   writes `SHA256SUMS.txt` and creates the GitHub Release with generated notes. Versions `0.x` and
   tags with a suffix are marked as pre-release.
4. Users download the files from the repository's **Releases** page
   (`https://github.com/HoZe-Lukas/dofScroll/releases`); the newest stable one is also reachable
   under `/releases/latest`.

Windows and macOS installers (unsigned) are built by the same workflow; see
[release-process.md](release-process.md).

If the tag does not match the CMake version the release job fails before publishing anything.
A failed release can be repeated by deleting the tag and the draft release and tagging again.
