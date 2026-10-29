<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Releasing

A release is built and published by GitHub Actions when a tag `v<version>` is pushed. The maintainer's by-hand steps of
the release in preparation (repository settings, hashes, pins, device tests, the tag, after the release) are listed with exact
commands in [release-checklist-1.3.0.md](release-checklist-1.3.0.md).

1. Refresh the pins: start the workflow `refresh-ci-pins` (Actions tab; it also runs monthly) and merge the pull
   request "Refresh CI pins" (the job starts its checks itself; wait for them to be green) so the
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
   (With the `creation` rule of "Protecting the release" below, only an administrator and the `tag-release` workflow can create the tag.)
   Without a shell (for example on a phone), run the workflow `tag-release` from the Actions tab or the GitHub
   app on branch `main` with the version (for example `1.2.2`): it checks the version against `CMakeLists.txt`
   and that the checks of the tip of `main` are green, creates the tag there and starts the `ci` run on it.
5. The workflow `.github/workflows/ci.yml` runs all checks, builds RPM, DEB and TGZ packages,
   the `release` job waits for the approval of the environment `release` and runs the release gate (the tagged commit is on
   `main`, the required checks are green), writes `SHA256SUMS.txt`, attests the build provenance of every file (signed with Sigstore, needs
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

## Protecting the release (set by the maintainer, by hand)

The workflow side is in the repository: the `release` job of `ci.yml` has `environment: release`, and its first step
`scripts/release-gate.sh` refuses to publish unless the tagged commit is an ancestor of `origin/main` and every status
check that the ruleset `main branch` requires (`.github/rulesets/main.json`) is green on that commit (the newest finished
run of each name must be `success`; a skipped or missing check is not green). A tag pushed on a side branch, or on a
commit whose checks failed, therefore never produces a release. The gate cannot stop a person from pushing the tag; the
repository settings below do that. **They are repository settings and are not changed by any workflow or script
of the repository; the maintainer sets them once, by hand** (`scripts/configure-repo.sh --apply` would also apply
`.github/rulesets/tags.json`; read the notes below before running it).

1. **Environment `release` with a required reviewer and a tag rule.** Settings > Environments > New environment, name
   `release`; tick **Required reviewers** and add the maintainer, and untick **Allow administrators to bypass configured protection
   rules** (otherwise an administrator skips the approval); under **Deployment branches and tags** choose
   *Selected branches and tags* and add a rule of type **Tag** with the pattern `v*`. With one maintainer leave
   **Prevent self-review** off (otherwise nobody can approve). From then on the release job waits for an approval in the
   Actions run before it publishes. The same with the CLI (`26377753` is the numeric id of the maintainer's account;
   check it with `gh api users/HoZe-Lukas --jq .id`):

        gh api -X PUT repos/HoZe-Lukas/dofScroll/environments/release --input - <<'JSON'
        {
          "can_admins_bypass": false,
          "prevent_self_review": false,
          "reviewers": [ { "type": "User", "id": 26377753 } ],
          "deployment_branch_policy": { "protected_branches": false, "custom_branch_policies": true }
        }
        JSON
        gh api -X POST repos/HoZe-Lukas/dofScroll/environments/release/deployment-branch-policies \
          -f name='v*' -f type=tag

2. **Only the maintainer and the release workflow may create `v*` tags.** Settings > Rules > Rulesets > `release tags`:
   tick **Restrict creations** and put **two** actors on the **Bypass list**, both with mode *Always*: *Repository admin*
   (the maintainer) and the *GitHub Actions* app (so `tag-release.yml` keeps working). Nobody else can create a `v*`
   tag. The checked-in copy `.github/rulesets/tags.json` documents exactly that (rules `creation`, `deletion`,
   `non_fast_forward`; bypass actors: role id 5 = repository admin, and the integration `github-actions`, app id
   15368). It is documentation of the live ruleset; to apply it by hand:

        id=$(gh api repos/HoZe-Lukas/dofScroll/rulesets --jq '.[] | select(.name == "release tags") | .id')
        gh api -X PUT repos/HoZe-Lukas/dofScroll/rulesets/$id --input .github/rulesets/tags.json

   The Actions app bypass is deliberate: `tag-release` creates the tag with the repository token, which the `creation`
   rule would otherwise block. The real protection is not the tag creation but the release job: its gate (tagged
   commit is an ancestor of `main`, required checks green) and the approval of the `release` environment (step 1)
   must both pass before anything is published.

3. **Immutable releases.** Settings > General > Releases > **Enable release immutability** (also reachable as "Immutable
   releases" in the repository settings). A release published afterwards cannot get files added or changed, and its
   tag cannot be moved or deleted. The `release` job therefore creates the release as a draft, attaches all files, and
   only then publishes it (`gh release edit --draft=false`).

Check after setting them: push a test tag such as `v0.0.0-test1` from a non-admin account (it must be refused), and
run the release workflow on a tag that is not on `main` (the job must stop at the release gate, after you approved the
environment). Delete the test tag with an admin account.

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
  uses the newest tested versions. The workflow has two jobs: `refresh` (read-only token, runs the two scripts on a copy of the tree without `.git`
  and uploads `images.env`, `aqt-requirements.txt` and `rocky9.Dockerfile` as an artifact) and `propose` (the write token; it never
  runs the scripts, takes over exactly those three files and commits them by name). The job changes only files under `packaging/ci`, so the default token
  suffices. The checks of a PR opened by the bot wait for an approval (`action_required`), so the job approves them
  itself and they count as the checks of the PR (a dispatch would not: its runs belong to the commit, not to the PR,
  and do not satisfy the required checks). If the approval ever fails, approve the runs in the Actions tab or via the
  PR. The optional secret `REFRESH_PINS_TOKEN`, a fine-grained token with contents and pull requests write, would start
  the checks by itself, and the approval is skipped then.
  The workflow needs the repository setting that lets Actions create pull requests.
- aqtinstall and its dependencies come from `packaging/ci/aqt-requirements.txt` (versions and SHA-256,
  `pip install --require-hashes`). After a change of `AQT_VERSION` in `packaging/qt.env`:
  `scripts/update-aqt-requirements.sh`, commit the result (it must run on Python 3.10, the oldest Python of the builds, and needs
  network access; see the header of the script for a Docker one-liner; py7zr 1.1 needs 3.10 or newer, so the Rocky image uses `python3.11`).
- `scripts/check-ci-pins.sh` (a unit test, so also `check-fast`) fails when an image or aqtinstall pin is
  missing, when `aqt-requirements.txt` does not match `qt.env`, or when the release job loses its
  attestation step.
