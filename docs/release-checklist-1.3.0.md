<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Release checklist 1.3.0 (the maintainer's steps by hand)

Everything in this list is something only the maintainer can do: it changes repository settings, needs a real device, or
needs the release approval. The documentation, the notes and the code are prepared in pull requests (see
[release-notes-1.3.0.md](release-notes-1.3.0.md) and [releasing.md](releasing.md), which explains why each setting
exists). Work top to bottom; tick a box only after you saw the result. Commands use the GitHub CLI (`gh`), logged in as the
maintainer. `REPO=HoZe-Lukas/dofScroll` is used below.

    export REPO=HoZe-Lukas/dofScroll

**Rule of the maintainer (`docs/status-release-1.2.4.md`, now removed): the release is only made when everything is
finished, reviewed and audited. Nothing ships half done.** Features that are not merged are not announced; the section
"Not on main yet" of the release notes is finalized last (step 12).

## A. Repository settings (before the tag, once)

These are the settings behind issue #293. They cannot be set by a pull request. Without them the release job still runs
its gate, but it would not wait for an approval and anybody with write access could create a tag.

- [ ] **A1. Environment `release`.** Settings > Environments > `release` (create it if it does not exist: GitHub
  creates a missing environment on the first run **without** protection rules). Tick **Required reviewers** and add the
  maintainer. **Untick "Allow administrators to bypass configured protection rules"**, otherwise an administrator
  skips the approval. Leave **Prevent self-review** unticked (with one maintainer nobody could approve otherwise). Under
  **Deployment branches and tags** choose *Selected branches and tags* and add a rule of type **Tag** with the pattern
  `v*`. The same by CLI (check the id first: `gh api users/HoZe-Lukas --jq .id` must print `26377753`):

        gh api -X PUT repos/$REPO/environments/release --input - <<'JSON'
        {
          "can_admins_bypass": false,
          "prevent_self_review": false,
          "reviewers": [ { "type": "User", "id": 26377753 } ],
          "deployment_branch_policy": { "protected_branches": false, "custom_branch_policies": true }
        }
        JSON
        gh api -X POST repos/$REPO/environments/release/deployment-branch-policies -f name='v*' -f type=tag

  Check: `gh api repos/$REPO/environments/release --jq '{can_admins_bypass, rules: .protection_rules, policy: .deployment_branch_policy}'`
  shows `can_admins_bypass: false`, a rule of type `required_reviewers` with your account, and
  `custom_branch_policies: true`; `gh api repos/$REPO/environments/release/deployment-branch-policies --jq '.branch_policies[] | [.name, .type]'`
  lists `v*` with type `tag`.

- [ ] **A2. Tag ruleset `release tags` with the `creation` rule and two bypass actors.** Settings > Rules > Rulesets >
  `release tags`: *Enforcement* active, target *tags*, include `refs/tags/v*`; rules **Restrict creations**, **Restrict
  deletions**, **Block force pushes**; **Bypass list** with exactly two entries, both *Always*: **Repository admin**
  (the maintainer; role id 5) and the **GitHub Actions** app (app id 15368, so `tag-release.yml` can create the tag). The
  checked-in copy `.github/rulesets/tags.json` documents exactly this. Compare it with the live ruleset; the diff must
  print nothing:

        norm='{name, target, enforcement, bypass_actors: [.bypass_actors[] | {actor_id, actor_type, bypass_mode}] | sort_by(.actor_id), conditions, rules: [.rules[] | {type}] | sort_by(.type)}'
        id=$(gh api repos/$REPO/rulesets --jq '.[] | select(.name == "release tags") | .id')
        gh api repos/$REPO/rulesets/$id | jq -S "$norm" > /tmp/live-tags.json
        jq -S "$norm" .github/rulesets/tags.json > /tmp/repo-tags.json
        diff /tmp/live-tags.json /tmp/repo-tags.json && echo "ruleset matches tags.json"

  If they differ and the file is right, apply it: `gh api -X PUT repos/$REPO/rulesets/$id --input .github/rulesets/tags.json`
  (or create the ruleset with `-X POST repos/$REPO/rulesets` if none exists). If the live ruleset is right and the file is
  not, fix the file in a pull request.

- [ ] **A3. Immutable releases.** Settings > General > Releases > **Enable release immutability**. A release published
  afterwards cannot get files added or changed and its tag cannot be moved or deleted, so a wrong release can only be fixed
  by a new version. (The release job already creates a draft, attaches the files and then publishes.) Check:
  `gh api repos/$REPO/immutable-releases` (if GitHub has no such endpoint for the repository yet, read the setting on the page).

- [ ] **A4. Test the protection** (as `docs/releasing.md` says). Push a test tag from an account that is not an
  administrator; it must be refused: `git tag v0.0.0-test1 && git push origin v0.0.0-test1`. Then start the release workflow
  on a throw-away tag that is not on `main` and approve the environment: the `release` job must stop at the release gate
  with "not an ancestor of origin/main". Delete the test tags with an administrator account
  (`git push origin :refs/tags/v0.0.0-test1`). **The `release` job has never run on a real tag**: the gate against the real
  API, `checks: read`, the draft-then-publish release and the approval were only tested with fakes (PR #287).

- [ ] **A5. Settings that the notes of the pull request #280 list as decisions** (merge methods, CODEOWNERS enforcement,
  strict status checks; `docs/security.md` section 5). Decide each, or leave it and keep the sentence in `docs/security.md`.

## B. Pins and hashes (before the tag)

- [ ] **B1. Run `qt-archive-hashes` once and commit its output (issue #295).** The list
  `packaging/ci/qt-archives.sha256` is empty, so every Qt install in the builds only warns "NOT verified".

        gh workflow run qt-archive-hashes.yml --repo $REPO --ref main
        gh run list --repo $REPO --workflow qt-archive-hashes.yml --limit 1      # note the run id
        gh run watch --repo $REPO <run-id> --exit-status
        mkdir -p /tmp/qt-hashes && gh run download --repo $REPO <run-id> -n qt-archives.sha256 -D /tmp/qt-hashes
        cat /tmp/qt-hashes/qt-archives.sha256

  Check the result: lines for Linux, Windows and macOS of the Qt version in `packaging/qt.env`, every line
  `<64 hex>  <archive file name>`. The hashes are what `download.qt.io` serves now; **compare a few with a second source**
  (another network, a Qt mirror, the `.sha256` file next to the archive on the server) before you trust them. Then copy
  the lines below the header comment of `packaging/ci/qt-archives.sha256` in a pull request (`QT_VERSION` unchanged), let CI
  run (the five Qt install sites now verify instead of warn; a failure there is a real finding or a wrong file name
  assumption of PR #291, read the first runs), merge it, and close #295. Optionally make the check strict by setting
  `QT_ARCHIVES_REQUIRED=1` in the build steps (`docs/release-process.md`, "Qt archive hashes"); the CI image is rebuilt from
  `main`, so merge before the tag.

- [ ] **B2. Refresh the CI pins (issue #285).** This is also the first real run of the two-job `refresh-ci-pins` workflow.

        gh workflow run refresh-ci-pins.yml --repo $REPO --ref main
        gh run watch --repo $REPO "$(gh run list --repo $REPO --workflow refresh-ci-pins.yml --limit 1 --json databaseId --jq '.[0].databaseId')" --exit-status

  Check: the job `refresh` produced the artifact, the job `propose` changed exactly `packaging/ci/images.env`,
  `packaging/ci/aqt-requirements.txt` and `packaging/ci/rocky9.Dockerfile`, the pull request "Refresh CI pins" is open
  (`gh pr list --repo $REPO --search "Refresh CI pins"`) and its checks ran (the job approves them itself; if they sit in
  `action_required`, approve them in the Actions tab). Review the diff, merge the pull request, comment on and close #285.
  Also merge the open Dependabot pull requests (`gh pr list --repo $REPO --author "app/dependabot"`). If there was no
  change, run `scripts/pin-ci-images.sh --check` and `scripts/update-aqt-requirements.sh` yourself as `docs/releasing.md` says.

- [ ] **B3. `scripts/check-ci-pins.sh` is green** (ctest `check_ci_pins`; part of `check-fast`) after the two merges.

## C. Tests that need a person (issues stay open until you checked)

None of the 1.3.0 features was tested with a SpaceMouse (see "Not verified" in the release notes). Build or download the
release candidate packages from the `ci` run of `main` (Actions > ci > the run > Artifacts) and test as far as you can. Write
the result into the issue: what you ran, on what, what happened. Close an issue only when fully done.

- [ ] **#36 Windows** (daemon, Qt UI, installer): installer and ZIP start, scroll and zoom in both directions with the
  Windows default, buttons, the interface pages, start at login, upgrade over 1.2.3 (the migration of a real configuration
  and the backup `backup\dofscroll.conf.pre-split`).
- [ ] **#37 macOS** (daemon, Qt UI, DMG): DMG opens and Gatekeeper steps are right; **the hardened runtime**: the app
  starts with SIP on, the Qt frameworks and plugins load, the daemon still loads the vendor framework with the driver
  installed (`diagnostics/test-driver-coexistence.sh`), Input Monitoring and Accessibility have to be granted again after
  the update from 1.2.3; scrolling, keys, buttons, start at login. Note that the daemon keeps `disable-library-validation`
  and is not protected against DYLD injection.
- [ ] **#242** the profile list, import, export and defective-file entries on a real desktop (large fonts, dark mode).
- [ ] **#246** the device selector and the axis assistant with two real SpaceMice.
- [ ] **#247** the new start and stop thresholds of the axis assistant on real hardware (tune `axis_assistant_*` in
  `ui.conf` if they do not fit, and write the values into the issue).
- [ ] **#250** the axis assistant and the axis names on real Windows and macOS.
- [ ] **#254** Take over (with and without the tuning) and New profile on a real desktop.
- [ ] **#130** the interface on real Windows and macOS machines (themes, font sizes, focus ring, symbols).
- [ ] Not in the list above but part of "Not verified": analog and digital keys (feel, thresholds, crosstalk on tilt with
  digital keys), click and hold (trigger and half-level re-arm; a drag with a hold on macOS, uinput, Windows), the lift
  (misfires when grabbing), zoom from a tilt, an RPM and a DEB install, upgrade and removal on a real desktop (the new
  scriptlets), Wayland.
- [ ] Linux smoke: read the summary of the `linux-smoke` job of the release run. It does not gate publishing
  (`docs/release-process.md`).

## D. Decisions and cleanups before the tag

- [ ] **D1. `PROVISIONAL, may change` in `src/profile_files.hpp`** (the `PRIORITY` order). The order is implemented, tested
  and documented in `docs/configuration.md` and `docs/plan-config-split.md`. Decide: final (remove the word in the same
  pull request as the version bump) or still open (then say so in the release notes).
- [ ] **D2. Wiki page Requirements** still carries older statements about the coexistence with the Windows driver; read
  them against the first Windows tests and the issue #36 result.
- [ ] **D3. Wayland self-test (issues #193 and #258):** the text for the Qt bug (SHM overflow, part 2 of #193) has to be
  submitted by someone with a Qt account; the "Known limitations" of the release notes name the issue.
- [ ] **D4. Pull requests of other agents.** Re-check `git log origin/main` and the open pull requests
  (`gh pr list --repo $REPO`). For every feature that landed after the notes were written (pointer movement from a motion,
  presets, macros and "New function", parser hardening, UI robustness): move it from "Not on main yet" into the notes,
  add its line to "All changes", and update the wiki, the website (English and German, informal "du") and the screenshots
  (`scripts/make-site-screenshots.sh`, with `QT_QPA_PLATFORM=offscreen`). Anything not merged stays out and stays in
  "Not on main yet" or is removed from the notes.
- [ ] **D5. `gh issue list --repo $REPO --state open`**. Many issues of the audits are
  already closed by their pull request (#263, #264, #265, #268, #269, #273, #274, #276, #240, #260, #193); the rest of the
  list: #224 and #225 stay open (stages 5 to 7 are not merged), #272 and #275 stay open until the parser hardening and the
  UI robustness land, #266 (low findings L1 to L9 of the supply-chain audit) is partly done by #280, #184, #42,
  #258 are known limitations of the notes (#245, #249 and most of #275 are fixed by #304). Settle each in step G1 after the release.

## E. Version bump (one pull request, merged last)

- [ ] **E1.** `CMakeLists.txt`: `project(dofscroll VERSION 1.3.0 ...)` (the tag must start with this version; the
  `tag-release` workflow and the release job check it).
- [ ] **E2.** Status lines that say "1.2.3": `readme.md` ("Current stable version"), `docs/wiki/Home.md` ("Status: version
  1.2.3"), `docs/wiki/FAQ.md`, `docs/wiki/Requirements.md`, `site/index.html` and `site/de/index.html` (`softwareVersion` in the
  JSON-LD block and the status texts that name 1.2.3; the contributor's Ubuntu report was for 1.2.3), `docs/wiki/Release-notes.md`
  (replace the "draft text" wording of the 1.3.0 entry). `grep -rn "1\.2\.3" readme.md docs/wiki site` lists them.
  The site's inline script hash (`Content-Security-Policy`) only changes if you edit the inline script; `scripts/check-site.sh`
  checks it.
- [ ] **E3.** Finalize `docs/release-notes-1.3.0.md`: remove "(draft)" wording if any is left, finalize "Not on main yet",
  check "Not verified" against section C (what you did test moves out of it, what you did not stays), check every
  statement against the code.
- [ ] **E4.** Run the checks on the merged `main`: `cmake --build build --target check-fast`, `ctest --test-dir build`,
  `scripts/check-english.sh`, `scripts/check-site.sh`, `scripts/check-german-du.sh`, `scripts/check-spdx.sh`,
  `scripts/check-terms.sh`, `scripts/check-utf8.sh`. All checks of the tip of `main` must be green
  (`gh run list --repo $REPO --branch main --limit 10`): the required ones are `build + ctest (Rocky Linux 9)`,
  `ASan/UBSan (Rocky Linux 9)`, `cppcheck (Rocky Linux 9)`, `secret scan`, `lint` and `CodeQL (c-cpp)`; a cancelled run is not
  a failure, rerun it: `gh run rerun <run-id> --failed`.

## F. The tag

- [ ] **F1.** Start the tag from the Actions tab, or by CLI (it checks the version against `CMakeLists.txt` and that the
  checks of the tip of `main` are green, creates the tag and starts `ci.yml` on it):

        gh workflow run tag-release.yml --repo $REPO --ref main -f version=1.3.0

  or, as the administrator, by hand: `git fetch origin && git tag v1.3.0 origin/main && git push origin v1.3.0`.
- [ ] **F2. Approve the release.** The `ci` run on the tag builds RPM, DEB, TGZ, the Windows setup and ZIP and the macOS DMG,
  then the job `release` waits for the environment `release`. Open the run (`gh run list --repo $REPO --workflow ci.yml
  --limit 3`), check the gate step passed (the tagged commit is on `main`, the required checks are green), then
  **Review deployments > release > Approve and deploy**. The job writes `SHA256SUMS.txt`, attests every file and creates the
  release as a draft, attaches the files and publishes it. It fails if any of the six packages is missing.
- [ ] **F3. Verify the release.** `gh release view v1.3.0 --repo $REPO`; download a file and run
  `sha256sum -c --ignore-missing SHA256SUMS.txt` and
  `gh attestation verify dofscroll-1.3.0-1.x86_64.rpm -R $REPO --signer-workflow $REPO/.github/workflows/ci.yml`.
  Check that the release is marked immutable and not as pre-release.

## G. After the release

- [ ] **G1. Check every open issue against the release** (`gh issue list --repo $REPO --state open`, `docs/releasing.md`
  step 7): comment what changed, close an issue only when it is fully fixed and needs no check on real hardware, name what
  is missing on the others. Add the release to the issues it fixes. #293 (settings), #295 (hashes) and #285 (pin refresh) are
  closed by section A and B.
- [ ] **G2. Post-release website and wiki update** if you did not do it in E2: status text of the website in English and
  German ("Tested ..."), `softwareVersion`, the wiki Home status and the wiki Release-notes entry, `readme.md`. The wiki
  publishes itself on every push to `main` (`.github/workflows/wiki-sync.yml`), the website through `pages.yml`; check both
  pages in a browser, including the German one, and the dark theme.
- [ ] **G3.** Announce only what the notes say; keep "Not verified" in any text you post.
- [ ] **G4.** If the release must be repeated: an immutable release cannot be changed or its tag deleted, so a mistake
  means a new version (1.3.1). Decide this before you approve in F2.
