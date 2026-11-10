<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Repository settings, gates and maintainer workflow

This page describes how the GitHub repository is configured and which checks gate a merge. **The
settings are made by hand by the maintainer** (Settings in the web interface); no script applies them
(`configure-repo.sh` was removed, issue #362: it no longer matched the live state and could undo it).
The ruleset JSON files in `.github/rulesets/` document the live rulesets (checklist step A2 in the
release checklist compares them); to apply one by hand, see `docs/releasing.md`.

## Gates

| Gate (job name = status check context) | What it checks | Required? | Defined in |
|---|---|---|---|
| `build + ctest (Rocky Linux 9)` | `-Werror` build of daemon and tray, all unit tests, hardening check | required | `.github/workflows/ci.yml` |
| `ASan/UBSan (Rocky Linux 9)` | tests under AddressSanitizer and UBSan | required | `ci.yml` |
| `cppcheck (Rocky Linux 9)` | static analysis of `src` and `tests` | required | `ci.yml` |
| `secret scan` | whole tree against the secret patterns of the pre-commit hook | required | `ci.yml`, `scripts/secret-scan.sh` |
| `lint` | actionlint (pinned, checksum-verified) on all workflows, shellcheck on `scripts/`, SPDX header check | required | `.github/workflows/lint.yml`, `scripts/check-spdx.sh` |
| `CodeQL (c-cpp)` | CodeQL `security-extended` queries on a manual CMake build; also weekly | required | `.github/workflows/codeql.yml` |
| `dependency review` | new or changed dependencies with known high-severity advisories | advisory (needs the dependency graph, see below) | `.github/workflows/dependency-review.yml` |
| package / release jobs | RPM, DEB, TGZ and the GitHub release | tags only | `ci.yml` |

Notes:

- A required status check can only be satisfied by a job that runs on `pull_request`. Every
  required job above has that trigger. `ci.yml` pushes are limited to `main` and `v*` tags, so
  WIP branches are built only through their pull request.
- Dependabot (`.github/dependabot.yml`) opens one grouped weekly PR for GitHub Actions updates.
  Every third-party action is pinned to a full commit SHA with a version comment; Dependabot
  keeps both up to date.

### Ports (to be added later)

The ports workflow (`ports.yml`) is not on `main` yet, so its jobs are deliberately **not** in
`.github/rulesets/main.json`. Once the workflow is merged and has run at least once, add these
status checks to the `main` ruleset (JSON `required_status_checks` list and Settings > Rules > Rulesets):

```
# NOT YET IN THE RULESET - add after ports.yml is on main:
Windows (MSVC) build + ctest
Windows (MinGW-w64) build + ctest
macOS build + ctest
```

A required check whose job never reports blocks every PR forever, so verify the exact job names
in a green run first. `Windows (MinGW-w64) build + ctest` runs only for pushes to `main`, tags and
on demand (it is skipped for pull requests, #40): leave it out of the required list, since a
skipped job would not gate anything.

## Settings that must stay as they are

Read from the live repository; change them only deliberately.

- **Actions > General > Workflow permissions:** read-only default token, and **"Allow GitHub Actions to create and approve
  pull requests" is ON**. The monthly `refresh-ci-pins` workflow opens its PR with the repository token and fails without it.
- **Actions > General > Fork pull request workflows:** approval required for all outside contributors.
- **Actions permissions:** `sha_pinning_required` is **off** on purpose: with it on, GitHub also rejects actions that
  other actions call by tag internally (`jurplel/install-qt-action` calls `setup-python@v5`), and the Qt jobs failed in
  "Set up job". Our own workflows still pin every action to a full SHA (`scripts/check-ci-pins.sh`).
- **Merge methods:** merge, squash and rebase are all allowed; head branches are deleted after merge; auto-merge is on.
- **Security:** secret scanning with push protection, Dependabot alerts and security updates, private vulnerability
  reporting (see `SECURITY.md`). Optional: Discussions.
- **Rulesets:** `base`, `main branch`, `release tags` as in `docs/security.md` section 5.
- **Environment `release`** and **immutable releases:** `docs/releasing.md`, "Protecting the release".

## Maintainer workflow once the ruleset is active

The ruleset `main branch` has no bypass actors; the ruleset `base` is bypassed by repository admins
(`docs/security.md` section 5).

1. `git switch -c my-change` and commit. Pushing the branch builds nothing; open a PR to start CI.
2. All required checks must be green and all review conversations resolved. The ruleset `main branch` needs zero approvals; `base` asks for one, which repository admins bypass.
3. Merge with **squash** (auto-merge can be enabled so the PR merges itself when the checks
   pass). The head branch is deleted automatically.
4. Release: bump the version in `CMakeLists.txt` through a PR, merge, then tag the merge commit
   `git tag -a vX.Y.Z -m "..."` and `git push origin vX.Y.Z`. The tag build packages and
   publishes the release (see `docs/releasing.md`). `v*` tags can not be deleted or moved
   (`release tags` ruleset); fix a bad release by tagging the next version.

## Uncertainties (not verified against a live repository)

- The ruleset JSON was checked against the field names and types of GitHub's published OpenAPI
  description for `POST /repos/{owner}/{repo}/rulesets`; it was never submitted to GitHub.
- `~DEFAULT_BRANCH` in `conditions.ref_name.include` is the documented token for the default
  branch. Status check `context` strings must equal the job names exactly; they are not
  bound to an app (`integration_id` omitted), which GitHub accepts.
- `sha_pinning_required` is set to false on purpose: with it on, GitHub also rejects actions that
  other actions call by tag internally (jurplel/install-qt-action calls setup-python@v5), and the
  Qt jobs failed in "Set up job". Our own workflows still pin every action to a full SHA.
