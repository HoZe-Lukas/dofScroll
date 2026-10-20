<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Legal notes: the 3Dconnexion driver interface

This page states plainly what dofscroll does with the official 3Dconnexion driver on Windows and
macOS, what it does not do, and where the rights lie. It is the project's own statement of facts,
**not legal advice**.

## What dofscroll uses

On Windows and macOS, dofscroll can read the SpaceMouse **through the programming interface of the
official 3Dconnexion driver, when that driver is installed on the computer** (config key `INPUT`,
see `configuration.md`). Without the driver, or when you choose so, it reads the device directly
as raw HID.

- macOS: the client interface of the driver (the `3DconnexionClient` framework that the driver
  installs under `/Library/Frameworks`). dofscroll opens that library at run time with `dlopen`.
- Windows: the COM object `TDxInput.Device` that the driver registers. dofscroll creates it through
  COM and talks to it by late binding (`IDispatch`); no type library is imported.
- Linux: not applicable. spacenavd (a free driver) is the driver there.

## What dofscroll does not do

- **Nothing of the vendor is shipped.** No 3Dconnexion header, library, binary, installer, type
  library or documentation is in this repository or in any dofscroll package. dofscroll does not
  link the vendor SDK at build time.
- The declarations in the source (function signatures, structures, constants, member names) were
  **written by the dofscroll project from public knowledge**, not copied from a vendor header. Next
  to each declaration the source files name the public place it comes from (for macOS mainly the
  GPL program Blender, `intern/ghost/intern/GHOST_NDOFManagerCocoa.mm`; for Windows the public
  interface description and forum samples). What could not be established from public sources is
  left out and listed in `port-architecture.md`, section "Driver interface backend".
- dofscroll does not modify, patch, hook or inject into the driver, and does not redistribute it.

## Where the rights lie

3Dconnexion, a Logitech company, owns the driver, its programming interface and the names
"3Dconnexion" and "SpaceMouse" (trademarks of their owners). dofscroll is an independent project
and is **not affiliated with, endorsed by or supported by 3Dconnexion or Logitech**. The
dofscroll code is licensed under GPL-3.0-or-later (see `LICENSE`); that licence covers dofscroll
only and gives no rights to anything of the vendor.

## Permission

**No permission was requested from 3Dconnexion.** The maintainer decided to go ahead and to say
so openly everywhere (decision recorded in issue 122), on these public facts, none of which is a
legal opinion:

- Blender (GPL) first linked the macOS client framework weakly and later changed to loading the
  driver glue at run time, so that the driver is not needed at build time. FreeCAD (LGPL)
  supports 3Dconnexion devices on macOS and Windows through the vendor interface and builds with or
  without it.
- The text of the vendor's SDK licence (as reproduced in third-party notices; a secondary copy)
  grants a personal, limited, non-exclusive, revocable licence and requires attribution to
  3Dconnexion. It is not known whether that licence can be combined with the GPL; this is exactly
  why dofscroll does not ship or link the SDK and only loads the installed driver's interface at
  run time.

Open point before the first release that contains this feature: ask 3Dconnexion developer support
in writing whether an open-source (GPL) program may use the client interface at run time and which
attribution they want. **No answer exists at this time** (nothing has been asked yet). When there
is one, it is recorded here; until then this page is the state of affairs.

## Attribution

The attribution is shown where users meet the feature: `THIRD-PARTY-NOTICES.md`, the README,
`installation.md`, `windows-mac.md`, `security.md`, the wiki pages Requirements and FAQ, the
website FAQ and footer (English and German), and the About box of `dofscroll-ui` on Windows and
macOS ("The official 3Dconnexion driver, its interface and the names 3Dconnexion and SpaceMouse
belong to 3Dconnexion (a Logitech company). dofscroll uses the driver's interface when the driver
is installed and does not contain any of it. Not affiliated with or endorsed by 3Dconnexion.").
The status line shows which path is active ("Input: 3Dconnexion driver" or "Input: direct (HID)").

## Verification status

Nothing of the driver path has been run against the real driver. What is verified, and what is
not, is listed in `port-architecture.md` (section "Driver interface backend") and in the hardware
checklists `port-windows-checklist.md` and `port-macos-checklist.md`.
