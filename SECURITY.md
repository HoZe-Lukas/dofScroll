<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Security policy

## Reporting a vulnerability

Please report vulnerabilities **privately** through GitHub private vulnerability reporting:

1. Open the repository's **Security** tab.
2. Choose **Report a vulnerability** (direct link:
   <https://github.com/HoZe-Lukas/dofScroll/security/advisories/new>).
3. Describe the problem, the affected version or commit, and how to reproduce it.

Do not open a public issue or pull request for a vulnerability, and do not post details in
discussions. GitHub is the only contact channel; there is no e-mail address for reports.

## What to expect

This is a small project maintained in spare time. In practice that means:

- I will try to acknowledge a report within about a week.
- I will try to confirm or reject the finding and keep you updated in the advisory thread.
- Fixes are published as a normal release; a confirmed issue gets a GitHub security advisory
  and, where it applies, a CVE through GitHub. Credit is given unless you prefer otherwise.

These are goals, not guarantees: there is no service-level agreement and no bug bounty.

## Supported versions

Only the latest release and the current `main` branch receive security fixes. The project is
pre-1.0 (0.x); older 0.x releases are not patched, please update.

## Scope

In scope: the `dofscroll` daemon, the tray application, the packaging files (systemd units,
the optional udev rule, install scripts) and the CI workflows in this repository. The project's threat model
and hardening measures are described in `docs/security.md`.

Out of scope: vulnerabilities in third-party components (`spacenavd`, `libspnav`, the Qt libraries,
the X server, the kernel), which should be reported to their own projects.
