<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Discoverability (maintainer notes)

Goal: people who search for "SpaceMouse scroll", "use a 3Dconnexion SpaceMouse to scroll and zoom in any application", "3D mouse as scroll wheel Linux", "spacenavd scroll" or "SpaceMouse zoom browser" (or the German equivalents) find dofscroll and understand within seconds what it is. This page lists what is set up in the repository and what only the maintainer can do. It is English only; the German website is in `site/de/`.

## What is in the repository

| Item | Where | Notes |
|---|---|---|
| Landing page, English | `site/index.html`, `site/404.html` | Plain HTML and CSS, no frameworks, no external requests, no tracking. |
| Landing page, German | `site/de/index.html`, `site/de/404.html` | Written for German readers, same sections, links and facts as the English page. German is a documented exception to the English-only rule (`AGENTS.md`, `CONTRIBUTING.md`, `scripts/check-english.sh`). |
| Style, icons, images | `site/style.css`, `site/favicon.svg`, `site/favicon-32.png`, `site/apple-touch-icon.png`, `site/img/` | Screenshots exist in a light and a dark variant (shown by `prefers-color-scheme` or the theme toggle). The `site/img/dofscroll-app.svg` copy keeps the site self-contained. |
| Social images | `site/img/og.png`, `site/img/og-de.png` | 1200x630, made from the app icon and a title text. No 3Dconnexion logo. |
| SEO files | `site/sitemap.xml`, `site/robots.txt` | The sitemap lists both languages with `xhtml:link` alternates. Each page has a title, description, canonical URL, `hreflang` alternates, Open Graph and Twitter tags, and JSON-LD `SoftwareApplication` data with `inLanguage`. |
| Language and theme | in each page | The language switch is a plain link to the same page in the other language; there is no redirect by browser language. The theme toggle is the only JavaScript (a tiny inline script, allowed by the hash in the Content-Security-Policy meta tag). Without JavaScript the page follows the system setting. |
| Checks | `scripts/check-site.sh` (calls `scripts/check-site.py`) | Run by ctest (`check_site`), by the `lint` workflow and before every deployment. It checks links, images, anchors, titles and descriptions, the CSP hash, external resources, English/German parity, WCAG contrast of the colour variables and the size budget. |
| Deployment | `.github/workflows/pages.yml` | On a push to `main` that changes `site/**`, and on manual start. |

Edit both languages together. A change to a link or a section in one language that is missing in the other fails `scripts/check-site.sh`. If you change the inline theme script, the CSP hash in every page must change too (the check prints the expected hash).

## Screenshots

The hero pictures (`site/img/ui-window-*.png`, `ui-overview-*.png`, German ones in `site/img/de/`) are rebuilt without hardware by `scripts/make-site-screenshots.sh`. It runs `dofscroll-ui --screenshots` against a demo configuration (programs firefox, blender, gimp, okular; devices SpaceMouse Pro and Compact) and a status file as the daemon writes it (SpaceMouse Pro connected, running), once with the English and once with a temporary `ui.conf` with `language = de`, then palette-quantizes the PNGs (`scripts/optimize-png.py`). Run it after changes to the interface and commit the result. The animations on the page are plain CSS in `site/style.css` and stop under `prefers-reduced-motion`; `scripts/check-site.sh` checks the contrast of text on the gradients and the reduced-motion rule.

## Publishing (maintainer)

1. Settings > Pages > Build and deployment > Source: GitHub Actions (`build_type=workflow`). Then run the `pages` workflow once or push a change under `site/`.
2. The site is served at `https://hoze-lukas.github.io/dofScroll/` (English) and `https://hoze-lukas.github.io/dofScroll/de/` (German). GitHub Pages serves only the root `404.html` for unknown paths; the German `404.html` is reached through the link on the English one.
3. Social preview: GitHub offers no API for it. Upload `site/img/og.png` in Settings > General > Social preview.

## Repository metadata

Suggested values (set through the GitHub API, not by a file in the repository):

- Description: `Use a 3Dconnexion SpaceMouse as a scroll wheel and zoom control in any program (Linux, Windows, macOS). Free software, per-program profiles.`
- Website: `https://hoze-lukas.github.io/dofScroll/`
- Topics: `spacemouse`, `3dconnexion`, `spacenav`, `spacenavd`, `scroll`, `zoom`, `3d-mouse`, `input`, `accessibility`, `x11`, `wayland`, `linux`, `windows`, `macos`, `qt6`, `cpp`

## Where to answer questions

People ask for exactly this in places where a good answer can be found later:

- 3Dconnexion forum: threads such as "Using space mouse for general applications" and "not working with Safari or Adobe Reader" (the vendor answer is that the device is for 3D applications only).
- The spacenav project (spacenavd and libspnav): its mailing list or issue tracker, for questions about using the device for scrolling on Linux.
- Reddit: r/3Dconnexion, and r/linuxquestions for Linux questions.

Rules: answer the actual question first, say that you made the tool, do not post the same text everywhere, follow each site's self-promotion rules, and only post where it helps the poster.

Template (adapt it to the question):

> You can use a SpaceMouse as a scroll wheel and zoom control in ordinary programs with dofscroll. Push the cap and the window under the pointer scrolls, twist it and it zooms (Ctrl + scroll). You can change the speed per program and per device, and switch to modes such as page up/down for slides or arrow keys for maps.
>
> Full disclosure: I made it. It is free software (GPL-3.0-or-later), not affiliated with 3Dconnexion: https://hoze-lukas.github.io/dofScroll/
>
> What is verified: Linux with X11 on RHEL 9 and KDE Plasma, with a SpaceMouse Pro and a SpaceMouse Compact. Windows and macOS versions are built by CI but have never been run with a real device. Wayland is untested. If you try it on another setup, an issue with what worked and what did not would help a lot.

## Checking indexing

- Search `site:hoze-lukas.github.io/dofScroll` in a search engine a few days after the first deployment. Both `/` and `/de/` should appear.
- Optional: verify the site in Google Search Console (URL-prefix property `https://hoze-lukas.github.io/dofScroll/`, HTML tag or DNS method) and Bing Webmaster Tools, submit `sitemap.xml`, and look at the coverage report. This is not required.
- Check the preview by pasting the URL into a chat or a social network after the social image is uploaded.
- Search for the phrases in the goal above from time to time and note where dofscroll appears; write the findings into an issue, not into this page.

## Packaging channels for later

Each of these is a separate piece of work and would become its own issue (none are created yet):

- AUR package (`dofscroll`, built from the release tarball).
- Fedora Copr repository for the RPM.
- winget manifest for the Windows installer (the unsigned installer may be hard to get accepted).
- Homebrew tap for macOS (needs the macOS build verified on hardware first).
- A Flatpak, if there is demand.

Be honest in every listing: Windows and macOS are unverified on real devices, Linux is verified only on the setup named above.
