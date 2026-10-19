<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Repository settings, gates and maintainer workflow

This page describes how the GitHub repository is configured, which checks gate a merge, and how
to apply the configuration with `scripts/configure-repo.sh`. The ruleset JSON files in
`.github/rulesets/` are the source of truth for branch and tag protection.

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
status checks to the `main` ruleset (JSON `required_status_checks` list, then re-run
`scripts/configure-repo.sh --apply`):

```
# NOT YET IN THE RULESET - add after ports.yml is on main:
Windows (MSVC) build + ctest
Windows (MinGW-w64) build + ctest
macOS build + ctest
```

A required check whose job never reports blocks every PR forever, so verify the exact job names
in a green run first.

## One-time manual steps

1. Nothing needs to be installed. CodeQL, Dependabot and secret scanning are GitHub
   features and need no secrets.
2. Optional: enable **Discussions** (Settings, General) for questions, and **Security
   advisories** for coordinated disclosure (private vulnerability reporting is switched on by
   `configure-repo.sh`, see `SECURITY.md`).
4. Run `scripts/configure-repo.sh` (below).

## Running `scripts/configure-repo.sh`

```sh
scripts/configure-repo.sh            # dry run, prints every gh api call
scripts/configure-repo.sh --apply    # execute (gh must be logged in with admin rights)
REPO=owner/name scripts/configure-repo.sh   # another repository
```

It is idempotent. It sets: squash-only merges, auto-merge allowed, head branches deleted after
merge, projects off, wiki on; secret scanning with push protection; Dependabot alerts and
security updates; private vulnerability reporting; Actions with a read-only default token,
selected actions only plus mandatory SHA pinning, approval for all outside contributors; and it
creates or updates the rulesets named `main` and `release tags` from `.github/rulesets/`.

Calls that the current plan does not allow (HTTP 403, 404 or 422) are printed as SKIPPED and the
script continues; anything else is FAILED and gives a non-zero exit status. After the repository
became public, run it again.

Things to know when the allow-list is active: the `selected` list contains GitHub-owned
actions and `jurplel/install-qt-action`. A new action
must be added to the list in the script (and in Settings) or its workflow fails. The CodeQL
`init`/`analyze` steps belong to `github/*` and are covered.

## Private today, public later

| | private repository (free plan) | public repository |
|---|---|---|
| Rulesets / branch protection | API answers 403/422, not enforced | available and enforced |
| Secret scanning, push protection | not available | free |
| CodeQL code scanning, dependency graph/review | not available without GitHub Advanced Security | free, results in the Security tab |
| Dependabot alerts | limited | free |
| Fork PRs | n/a | run without secrets; first-time and all outside contributors need approval before workflows run (`all_external_contributors`) |

Until then CI, lint and the scripts work, but nothing prevents a direct push
to `main`. Apply the ruleset right when the repository becomes public (or upgrade the plan).
The CodeQL and dependency review workflows will fail on a private free-plan repository (the SARIF
upload and the dependency graph are not available); that is expected and no reason to treat
them as broken.

Fork PRs never receive secrets.

## Maintainer workflow once the ruleset is active

The ruleset has no bypass actors: nobody can push to `main`, force-push, or delete it, including
the owner.

1. `git switch -c my-change` and commit. Pushing the branch builds nothing; open a PR to start CI.
2. All required checks must be green and all review conversations resolved. Zero approvals are
   needed, so you can merge your own PR.
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
