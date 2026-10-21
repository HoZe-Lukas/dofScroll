<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# The dofscroll interface (`dofscroll-ui`)

`dofscroll-ui` is the Qt6 interface: a tray icon plus a settings window, one code base for Linux,
Windows and macOS. It edits the same config file as the daemon and asks the daemon to reload, so
changes apply immediately. There is no Save button: every change is saved by itself (see "Saving"
below). The daemon runs without it. The interface starts minimized to the
tray (no window opens); "Open settings" or a click on the tray icon shows the window. Without a
system tray the window is shown minimized instead.

The window remembers its size, position and the page you last used (`window = x,y,w,h` and
`page = <id>` in `ui.conf`, next to the language and theme) and restores them at the next start,
also when you raise it from the tray or by starting the interface a second time. A saved position
that is off all screens (for example a monitor was unplugged) is ignored, and the size never goes
below the minimum window size. Closing the window with the close button hides it and the
interface keeps running in the tray; the first time in a run, the tray shows a short notice
("dofscroll is still running in the tray"). Without a system tray there is nothing to keep the
interface reachable, so closing the window quits it, and the status bar says so ("No system tray
found: closing the window quits dofscroll-ui"). Use "Quit" in the tray menu to quit otherwise. The old GTK tray (`dofscroll-tray`) was removed in 0.1.1: `dofscroll-ui` is
the only tray and settings window on Linux, Windows and macOS (see
[installation.md](installation.md#upgrade) for the autostart migration).

State: the pages below are merged on `main`. Behavior on a real desktop has been checked by the
maintainer only on Linux (RHEL 9, X11, KDE Plasma) with the SpaceMouse Pro and Compact; the
Windows and macOS builds come from CI and have not been run on hardware. See "Known limitations"
in the [readme](../readme.md).

## Look and layout principles

The interface is meant to be open and calm, in the manner of the settings apps of current Windows
and macOS versions: rounded cards on a slightly tinted window, thin borders instead of bevels,
one accent colour (primary buttons, selection, focus ring, the underline of the selected page tab),
toggle switches for on/off settings (a plain box only where several boxes are choices, such as
modifier keys), large click targets, and help text in a muted colour. The look is applied in one
place, `src/ui/qt/theme.cpp`: an application style sheet built from the theme colours
(`src/ui/model/theme_colors.hpp`; "System" keeps the platform palette and light/dark choice and
only adds the shapes and our accent) and a layout style that gives every layout that does not set
its own numbers the same margins and gaps. In the Linux packages (which bundle their own Qt, see
[installation.md](installation.md)) no KDE or GNOME platform theme exists, so "System" means Qt's
Fusion style (the base of the style sheet) with the light or dark scheme that the desktop reports through the XDG
desktop portal; `--self-test` checks on Linux that the base style is Fusion. The numbers are tokens in
`src/ui/model/design_tokens.hpp`:

| Token | Value (px) | Use |
|---|---|---|
| `kPageMargin` | 24 | margin of a page and of a dialog |
| `kSectionGap` | 20 | between cards (group boxes) |
| `kRowGap` | 12 | between rows of a form and between buttons |
| `kCardPadding` | 18 | inside a card |
| `kControlHeight` | 34 | buttons, inputs and combo boxes |
| `kRowHeight` | 32 | rows of lists and trees |
| `kRadius`, `kRadiusCard` | 8, 10 | corners |

New pages should use plain layouts and group boxes and set no margins of their own; mark a primary
button with `setProperty("primary", true)`, a check box that is one of several choices with
`setProperty("look", "box")`, a heading label with `setProperty("heading", "page")` or `"section"`.

Every page sits in a scroll area: in a small window text stays readable and controls keep their
size, and the page scrolls (horizontally only as a last resort) instead of squashing. Info,
status and help labels wrap. The default window is 1560x1040 (clamped to the screen); the saved
geometry (saved a moment after every resize or move, and when the window closes) wins, the smallest size is 720x480.

The mouse wheel never changes a combo box, spin box or slider, anywhere (also in dialogs): one
application-wide event filter hands the wheel to the surrounding page, so scrolling a page with the
pointer over a control scrolls the page. Change a value by clicking or with the keyboard.

Colour contrast (WCAG AA, 4.5:1 for text, 3:1 for the accent marks) is checked for both themes by
`tests/test_ui_theme.cpp`; `dofscroll-ui --screenshots <dir>` renders every page at three sizes in
both themes for a visual check.

## Window layout

A sidebar on the left (icon and name, the selected entry is a rounded pill) picks a category, and
the category name is shown as a large page title; where a category has
several pages, horizontal tabs on top of the content pick the page (a category with one page shows
no tab bar). A status bar at the bottom shows the daemon state (the same line as the tray's first
entry). The grouping comes from `src/ui/model/navigation_model.hpp`, the registry
(`pageFactories()`) says which category a page belongs to:

| Category | Page (tab) | Content |
|---|---|---|
| Overview | Overview | Only summarizes and links: the state of the daemon, task buttons, quick actions (see "Overview quick actions" below) and one card per device or program profile with its joystick mode, a one-line summary of what it overrides and how many settings that are; "Open" shows the profile on the Profiles page. Every profile of the configuration has a card, a filter box narrows the list |
| Profiles | Profiles | The list of profiles (Global first, then devices, then programs) and the editor of the selected one with the tabs General, Channels, Buttons and Overrides, see "Profiles" below |
| Modes | Joystick modes | The available joystick modes (built-in and files in the joystick modes folder) with their origin and description: new, new from example, duplicate, import (button or drop `.conf` files onto the page), export, rename, edit in the page, delete; for every joystick mode the profiles that use it; a reminder that joystick mode files can press any key |
| Modes | Button modes | The button mode files (`buttons/`): create, new from example, edit in the page, duplicate, import, delete; used from the button drop-downs of the Profiles page |
| Modes | Extensions | Your joystick mode files and button mode files in one list, each with a switch (on / off), where it is used and Remove; "Open folder" for both folders, see "Extensions" below |
| Device | Device | The connected devices and their button count (the number of button rows a profile offers), the daemon state |
| Device | Axes | Live bars of the six axes (and, while the active joystick mode splits a scroll channel, the push and tilt parts of that channel) and the axis assistant (wizard) that detects which axis does what |
| Settings | Settings | Output on/off (`ENABLED` of Global), start at login, language and theme (the box "Language and appearance"), the daemon settings (tick rate, output backend; on Windows and macOS also **Input path**: Automatic, 3Dconnexion driver or Direct (HID), key `INPUT`, with the active path shown), the read-only status of the daemon (including "Input: ..." on Windows and macOS), "Open config", "Show log...", "Documentation" and an **About** box (version, licences, Qt under the LGPL and, on Windows and macOS, the 3Dconnexion note) |

The Channels settings (sensitivity, dead zone, direction, curve, zoom detection, smoothing) are not a
page of their own: they belong to a profile and are the Channels tab of the profile editor, for
Global and for every other profile. No setting appears in two places: the output switch is on the
Settings page (not in the Global profile), the daemon settings (tick rate, backend) are on the
Settings page too (they do not belong to a profile).

## Several devices

With one lane per device (Windows and macOS, direct HID; [multi-device.md](multi-device.md)) the status line lists every device with its joystick mode ("Devices: A (Joystick mode: normal), B (Joystick mode: timeline)") and the warning "N SpaceMice connected, only 'A' is reported" does not appear. The Axes page shows the device that moved last; the names of all devices are listed, a device selector is planned (issue #139). The first device's values stay in the plain `profile=`, `mode=` keys of the status file, the others are `device2=`, `mode2=` and so on.

## Overview quick actions

- **What do you want to do?** Three buttons that open the page for a task: "Tune scroll and zoom"
  (the Channels tab of the Global profile), "Check the axes" (Axes) and "Switch joystick modes"
  (Joystick modes).
- **Add a profile...** opens the Profiles page; **New profile for the running program...** opens the
  running-program picker of the Profiles page directly.
- **Active joystick mode** lists the loaded joystick modes and marks the one the daemon runs now;
  every entry has its description as tooltip. Picking one switches the running daemon to it until
  the next reload or restart (the same runtime request as the tray's Joystick mode menu); the
  Global joystick mode in the configuration stays as it is. It is disabled while the daemon is not
  running.
- **Open log** (and "Show log..." on the Settings page and in the tray) opens a small log window: the
  end of the daemon log (the last 64 KB, at a line start; control characters are replaced), with
  Refresh and Copy. The log file is the one in the runtime directory
  (`dofscroll.log`, see [troubleshooting.md](troubleshooting.md)).
- **Documentation** (Overview, Settings, and on the Button modes page) opens the project
  documentation in the default browser: `docs/ui.md`, or `docs/modes.md` from the Button modes page.
  Only https addresses are ever opened.
- **Release all keys**, **Restart daemon** and **Restart interface** as before.
- The filter box above the profile cards matches every word against the name, the joystick mode, the kind
  ("device" or "program") and the settings of a profile, ignoring ASCII case. A card says how many settings the profile overrides ("Overrides 3 settings"; the
  profile list on the Profiles page shows the same number after the name). The tooltip of a card
  lists all its settings, not only the first ones shown in the summary.
- On a very wide window the Overview and the Channels tab stop at a maximum content width
  (`kMaxContentWidth` in `src/ui/model/design_tokens.hpp`) and stay centered.


## Editing a joystick mode

"Edit" on the Joystick modes page opens an editor next to the list (a built-in joystick mode first
gets an editable file with the same id). It edits name and description (the description field is
always there; a joystick mode without one shows "No description"), the three channels (default,
off, vertical or horizontal wheel, zoom, **keys** or **step** with modifier check boxes and a key
picker per direction), the buttons (the same drop-down per button as on the Profiles page, including
the button modes and "Default (inherit from Global)", which removes the binding) and the settings
overrides (key, value, Add, Remove; `MODE`, `ENABLED`, `BACKEND` and `TICK_HZ` are not offered).

Each scroll channel (vertical, horizontal) has a **Split push and tilt** switch. Off, the channel is
one action row. On, it becomes two rows, push (forward/back or sideways) and tilt, each with the
same action picker as the channel, and each part gets a "Tuning" box with its own sensitivity,
dead zone, direction, curve and smoothing; an empty field means "as the channel" and writes no key.
The switch writes `V_PUSH_ACTION` / `V_TILT_ACTION` (`H_...`) and the per-part keys
`SCROLL_<V|H>_<PUSH|TILT>_*`; switching it off removes the action keys again (tuning keys stay in the
Overrides tab, where they have no effect while the channel is not split). The information popover of
a joystick mode lists one line per part for a split channel.

The editor has three tabs: Settings (name, description, the three channels), Buttons and
Overrides. Each tab scrolls on its own; the problem list and the buttons "Open in text editor" and
"Close editor" stay visible below the tabs. The selected tab is kept while you edit and when you
open another joystick mode.

There is no Save or Revert button: a change is written by itself, a drop-down or switch at once,
typed text about 600 ms after you stopped typing or when you leave the field. Only a valid file is
ever written: the editor lists what is wrong ("Not saved until this is fixed: ...") and leaves the
file as it is until the form is valid again. Writing rewrites the file line by line: the lines of
the keys the editor owns are updated in place, new ones are appended, cleared ones are removed;
comments, the SAFETY header and unknown lines stay as they are, and the id (file name) never
changes. The result is checked with the daemon's loader first (a write that would drop one of its
lines is refused) and replaces the file atomically; the loader's warnings, including "can press
shortcut chords", are shown below the form. If the file changed on disk while you were editing, the
editor asks before overwriting it (the one question that remains: "Overwrite it with your changes?").
Switching to another joystick mode, "Close editor", leaving the page, closing the window and Quit
write the last change first.

## Saving

Nothing in the interface has a Save, Revert or "unsaved changes" question. Every edit is saved by
itself and tells the daemon to reload:

- Switches, drop-downs, spin boxes, a released slider, button rows, Reset, Add and Remove are
  written at once (the next turn of the event loop; edits of one gesture become one write and one
  reload).
- Text fields (axis mix, curve points, override values, names and descriptions) are checked while
  you type. A valid text is written about 600 ms after the last key (or when you leave the field);
  an invalid text is not written: the field gets a red border and the problem is written next to it
  ("Invalid value: ..."), and the file stays valid.
- The footer shows "Saved" for a moment, or "Could not save: <reason>" until the next successful
  write; a daemon that could not be notified is reported too ("Saved, but the daemon was not
  notified").
- Page switches, tab switches, profile switches, closing the window and Quit write what is pending
  first, synchronously, so no edit is lost. (An invalid form of the joystick mode editor cannot be
  written by design; it stays as you left it in the editor until you fix it.)
- The window follows the configuration file: when the tray (Enabled) or a text editor changes it
  and the window has nothing of its own pending, the window reads it again.

The model of the rule (debounce, burst coalescing, flush, failure handling) is
`src/ui/model/autosave.hpp` with a fake clock in `tests/test_ui_autosave.cpp`; the Qt side is
`src/ui/qt/autosaver.hpp`. The `--self-test` changes every kind of control, waits for the
autosave, reads the file from disk and checks it (`autosaveAuditSelfTest`: about 70 controls, the
audit table is printed).

## Getting started and accessibility

While the daemon has not seen a device or no profile exists yet, the Overview shows "Getting
started": connect the SpaceMouse, check the axes on the Axes page (the assistant helps), add a
profile for a program you use. Each step says "Done:" or "To do:" in words and opens the page that
does it; the box disappears once the device is seen and a profile exists. The first step opens the
Axes page (it shows what the daemon sees of the device) and, while the daemon is not running, says
so. The axes step is "Done:" after the axis assistant has been applied once; that is remembered in
`ui-progress.conf` next to the configuration file (`axis_assistant_applied = 1`), not in `ui.conf`.

Every input control has an accessible name (screen readers): the `--self-test` checks all pages for
controls without one. The whole interface works with the keyboard (tab order follows the layout;
drop-downs, spin boxes and buttons are real controls), state is never shown by colour alone (origin
and "Done:"/"To do:" are written out), and the themes meet the contrast limits of
`tests/test_ui_theme.cpp`.

## Accessibility

What the Light and Dark themes guarantee, and how each point is tested. `tests/test_ui_theme.cpp` is
toolkit-free (colour tokens in `src/ui/model/theme_colors.hpp`); the Qt checks are in
`dofscroll-ui --self-test`.

| Item | Guarantee | Test |
|---|---|---|
| Text contrast | Body text 7:1; muted help text, links, error, warning, success and info text, text on buttons, cards, hovered rows and the selection at least 4.5:1 | `test_ui_theme` (`testThemesAreReadable`, `testAccessibility`) |
| Selected rows | Name 7:1 and the muted second line 4.5:1 on the selection, selected, hovered and unfocused, in every list and tree (joystick modes, button modes, profile tree, dialogs), also in the System theme | `test_ui_theme`; self-test "selection contrast" renders the lists and measures the pixels |
| Non-text contrast | Borders of inputs, buttons, boxes, radio buttons, the off state of switches, slider grooves, scroll bar handles, the accent and the focus ring: at least 3:1 against window, card, input, hover and selection | `test_ui_theme` (`minAccentContrast`, `testAccessibility`) |
| Focus | Every button, input, box, switch, slider, list, tree, tab and left category shows a 2 px ring in the focus colour; the style sheet never removes it | style sheet in `src/ui/qt/theme.cpp`; the ring colour is tested (3:1) |
| Colour alone | Every error, warning and success message starts with a symbol (error, warning sign, check mark; `src/ui/model/status_text.hpp`) and keeps its words ("Could not save: ...", "Invalid value: ...", "Warning: ...", "Saved"): the hint under a field, the problem lists of the joystick mode and button mode editors, the Settings messages and the footer, which also uses the status colours (red, orange, green). "Inherited from ..." and "Set in this profile" are words; on/off is a switch with a knob position, not only a colour | `test_ui_status_text`, `--self-test` (problem next to the field has its symbol), review rule |
| Colour-vision deficiencies | Status, accent, focus and border colours keep 3:1 luminance contrast after a protanopia, deuteranopia and tritanopia simulation (Machado matrices); selected text keeps 7:1 | `test_ui_theme` (`simulateCvd`) |
| Operating system | System follows the platform palette (light or dark). A high-contrast request (the Qt contrast preference where Qt provides it, Windows high-contrast mode) switches the style sheet off so that the platform palette is shown unchanged. On other systems or older Qt nothing is detected | code review; needs a human on the platform |
| Font size | The base font is the system font (plus 8%); the interface has no fixed pixel font sizes. The lengths of the style sheet (padding, heights, radii, indicator sizes) are in em, converted from the design pixels at a reference font of 14 px (`src/ui/model/qss_scale.hpp`), so the geometry follows the font size of the system; border widths stay in pixels. At 150% and 200% font size and in a window of 560 px, in the System theme and in the Light theme, no label or button is cut off: a page that is too narrow scrolls (the scroll bar appears only then); at the normal font size in the normal window no page shows a horizontal scroll bar | `test_ui_qss_scale`, self-test "font scale" |
| Motion | The interface has no animations, so there is nothing to reduce | none needed |
| Screen readers, keyboard | Every input has an accessible name; the tab order follows the layout (no jump back up inside a column); the arrow keys, Home and End move between the left categories (the current one shows the focus ring); every "i" info button takes Tab focus and opens its popover with Space or Enter, the popover has an accessible name and Esc closes it; Escape closes dialogs | `accessibility`, `keyboard` and `sidebar focus` self-tests |

Target size: the `target size` self-test asserts at least 24 x 24 px (WCAG 2.5.8) for every visible
button, switch, input and slider of every page, in the Light and the Dark theme (planned 32 to 34 px;
the style sheet gives that, the self-test pins the minimum). In the System theme the platform's own
controls are used, which this test does not measure.

Not yet covered: high-contrast rendering was not checked with screenshots, and screen readers,
magnifiers and a colour-blind reviewer were not tried (they need people and real devices, see issue #104).

## Appearance

Settings > Language and appearance sets the theme: **System default** (the look of the platform), **Light** or
**Dark**. A change applies at once and is remembered in `ui.conf` (`theme = system|light|dark`,
next to the language). Light and Dark use the Fusion style with the dofscroll palette
(`src/ui/model/theme_colors.hpp`); System restores the style and palette the program started with.
The palettes keep a contrast of at least 7:1 for normal text and 4.5:1 for everything else
(hints, disabled text, links, selection), checked by `tests/test_ui_theme.cpp`. The left tabs have
icons (`src/ui/qt/icons/*.svg`, drawn in black and tinted with the text colour of the current
palette); spacing follows one scale (`src/ui/model/design_tokens.hpp`). The screenshots of
`dofscroll-ui --screenshots` show the light theme at three sizes and the dark theme at one.
`dofscroll-ui --screenshots <dir>` renders every page at three window sizes into PNG files (use
`QT_QPA_PLATFORM=offscreen` on a machine without a display); CI attaches them to the UI job.

When the joystick mode the daemon runs splits the vertical or horizontal channel, the live view
shows two more bars per split channel below the channel outputs ("Scroll V push", "Scroll V tilt",
...): the value of the push part (the `x`, `y`, `z` terms of the channel mix) and of the tilt part
(the `rx`, `rz` terms), before sensitivity and curve, in the units of the axis bars, so you see
which movement drives which part. The screen reader description of the view lists what each part
does. The page takes the mixes from the Global profile and the overrides of the active joystick
mode; a per-program mix override is not applied here.

While the Axes page is open the daemon pauses its output, so moving the cap does not scroll the
window you look at. The page shows as many button cells as the factory button count of the
device (see below), or the highest button it has seen when no device information is available.

## Extensions

The Extensions page (category Modes, third tab) lists the files of yours that extend dofscroll: the joystick
mode files (`modes/*.conf`) and the button mode files (`buttons/*.conf`). An extension is a plain file that the
daemon's own loaders read with their checks and warnings; nothing is executed. Each row has

- a **switch**: off renames `<id>.conf` to `<id>.conf.disabled` (the loaders only read names that end in `.conf`),
  on renames it back. The file is kept, nothing is overwritten (if the other name is taken, the page says so and
  leaves both files alone). A switched-off file with the id of a built-in joystick mode or button mode lets the
  built-in one apply again. The change is written at once, there is no Save button;
- the name and description of the file, and **where it is used** (profiles that select it, buttons that switch to
  it) so you see what a switch changes;
- **Remove**, which asks first and deletes the file, switched on or off.

"Open folder" opens `modes/` or `buttons/`. Device description files (`devices/*.conf`) are not on this page:
the daemon does not load them from the configuration folder, only `dofscroll-hidprobe` reads them (see
`docs/devices.md`). The examples that ship with dofscroll are installed with "New from example..." on the Joystick
modes and Button modes pages.

## Joystick modes and button modes

The key of a button action can be a chord: the "Press key" action shows the check boxes ctrl, shift, alt and super next to the key list. With a modifier ticked the action is `key:<mod>+<key>` (for example `key:ctrl+0`), which is tapped once per press; without one it is a plain key that is held while the button is held. The built-in button mode "Reset zoom" is `key:ctrl+0`.

There are two kinds of modes. A **joystick mode** says what the axes (and optionally buttons)
do: scroll, zoom, keys. A **button mode** is a named behavior of a single button (a key, a click,
navigation, a joystick mode switch). Config keys and file names do not change with the words
(`MODE`, `modes/*.conf`, `buttons/*.conf`, `buttonmode:<id>`).

**Descriptions everywhere.** Every joystick mode and every button mode shows its short description
(`DESCRIPTION`; all built-in joystick modes have one; "No description" when a file has none) wherever it can be
picked or listed: as tooltip of every entry of the joystick mode drop-downs (profile General tab,
the joystick mode of a button, the Overview's Active joystick mode, the tray's Joystick mode menu)
and of the button mode entries of the button drop-downs; as a second line (muted, wrapped) under the
name in the lists of the Joystick modes and Button modes pages; and in the detail view. Next to
the joystick mode drop-down of a profile is an **i** button: it opens a popover with the name, where
the joystick mode comes from (Built in / Your file), the description and what it does per channel in
plain words ("Vertical push: scrolls vertically", "Horizontal push: taps Left or Right once per
push", ...). The description field of both editors is always available.

**Origin hints.** Every entry of the lists says where it comes from: "Built in", "Your file", "Your
file, replaces the built-in joystick mode" (button modes: "Built in", "Your file", "Your file, replaces the
built-in button mode"). A built-in button mode has no file: "Create editable copy" makes a file with the same
id that replaces it (a built-in joystick mode offers the same on "Edit"). **New from example...** (both pages) offers the
examples that ship with dofscroll and are not in your folder yet ("Example, not installed yet"); the
chosen one is copied like an import (checked, never overwrites) and selected. Delete removes only
your own file (a file that replaces a built-in one brings the built-in back).

The Profiles page has a "Joystick mode" drop-down per profile on the General tab (Global, device,
program): Global chooses the joystick mode that applies unless a device or program profile chooses
another one; the others have "Same as Global" as first choice. Every button row on the Buttons tab
has its own drop-down with these entries, in this order: "Default (inherit from Global)" (device
and program profiles; it removes the profile's own `BUTTON_<n>` line; in Global the first entry is
"Nothing"), the **button modes** by name (tooltip: the description), then the plain actions:
navigation, joystick mode switch (`mode:<id>`, `mode:next`), toggles (`toggle:nav`,
`toggle:mode:<id>`) and, behind "Advanced" in Global, click and key. All buttons are assigned
there, one row each; the old `NAV_BUTTON` / `CLICK_BUTTON` settings are not offered any more (see
[configuration.md](configuration.md#deprecated-nav_button-and-click_button)). Choosing a button mode
writes `BUTTON_<n> = buttonmode:<id>`; the same button mode can be chosen on several buttons. A
reference to a button mode that is not installed shows as "missing: <id>" and is not rewritten. See
[button-modes.md](button-modes.md). The tray shows "Navigation on" while navigation is latched.
Precedence is unchanged: Global < device < joystick mode overrides < program. A profile that names a
joystick mode that does not exist is shown with a warning instead of being changed silently.

Mode library: "Export..." saves a copy of the selected joystick mode file (a built-in one as its
template text) to a place you choose and never overwrites without asking. "Rename..." gives a file
a new name; the id (and file name) follows the name. Profiles that still use the old id are listed
and confirmed first; they are not rewritten and fall back to the Global joystick mode until you change
them. A built-in joystick mode, or a file that replaces one, cannot be renamed. The details of every
joystick mode show "What it does" and "Used by": the profile sections whose `MODE` selects it or
whose `BUTTON_<n>` is `mode:<id>`, `toggle:mode:<id>` or `hold:mode:<id>`. Dropping `.conf` files
onto the Joystick modes page imports them with the same checks, warnings and confirmation as
Import; files are only read, never run.

The **Button modes** page (category Modes, next to Joystick modes) manages the button mode files:
list, editor for name, description and action (saved by itself, see "Saving"; a drop-down is written
at once), New, New from example, Duplicate, Import, Delete (it names the profiles and joystick mode
files that still use the button mode) and "Open button modes folder". A `CYCLE` list of a
`mode:next` / `mode:prev` button mode stays in the file when the name or description changes.

## Profiles

The list on the left (Global first, then device profiles, then program profiles; each profile shows
the number of settings it overrides, "firefox (3)") stays visible next to the tabs of the selected
profile. The editor on the right always says which profile it edits: **icon, name and kind** (Global
profile, Device profile, Program profile) in the header, so it never looks like a global page, and
one line about inheritance: Global "Applies to every device and program unless a device or program
profile overrides it.", the others "Everything you do not change here is inherited from Global (and
the device)."

The tabs are the same for every profile and the selected tab stays when you select another profile:

- **General**: for device and program profiles first the output switch of the profile ("Send scroll,
  zoom and key events while this profile is active", `ENABLED`), then the joystick mode (drop-down with
  the **i** info button). **With the output off the profile sends nothing, so the joystick mode, the buttons
  and the channel settings do not apply: the joystick mode and its information are hidden, and so are the
  Channels, Buttons and Overrides tabs**; a note says so, and switching the output on brings them back. (To
  switch only the axes off and keep the buttons, choose the joystick mode "off" and leave the output on.) A
  profile with another joystick mode also shows what that joystick mode does, read-only, with "Edit
  joystick mode file". Global has no output switch here: it is on the Settings page, and nothing is hidden for it.
- **Channels**: sensitivity, dead zone, direction, smoothing, axis mix, curve (exponent or points,
  with a live preview), zoom detection (the axes the twist must dominate, entry time, exit ratio) and
  the step trigger, in groups per channel. A filter box (the search box above the tabs) hides the
  rows that do not match; every row names its config key in the tooltip. "Restore defaults" removes
  every value of the tab from the profile; "Reset this group" and the per-row "Reset" do the same
  for a group or a row. The editor edits the selected profile, Global included.
- **Buttons**: one row per button of the device (factory button count), the effective action, where
  it comes from, and the drop-down described above. Global may assign buttons like any other
  profile. A profile whose joystick mode has its own bindings shows a note instead (the bindings
  are listed on the General tab).
- **Overrides** (advanced): the channel values this profile sets itself as raw text, with Remove
  (inherit again) and Add override. Global has nothing to override (it holds the base values).

Below the list: **Add application...** (running-program picker) and **Add device...**. What belongs to the
selected profile as a whole is the box **This profile** at the bottom of its General tab (hidden while the settings
search is in use): **Change application...** (program profiles) or **Change device...** (device profiles) opens
the same picker as "Add application..." / "Add device..." (running programs, capture the next focused program,
device list, manual entry) with the current match string filled in, and changes the program or device the
profile applies to while all its settings stay, for example when a program's name changed; **Export...**
writes the selected profile (its section, for Global the `[global]` keys the file holds) as a `.conf` file that
can be copied into another configuration; **Remove** deletes the profile after asking. Global has no program or
device and cannot be removed, it has only Export. Import of a whole profile is not offered; joystick modes and
button modes have Import.

### Inheritance

Everything (joystick mode, every channel value, every button, button modes) can be set in Global
exactly like in any other profile; a device or program profile holds only what it changes
(precedence: Global < device < joystick mode overrides < program, see
[configuration.md](configuration.md#inheritance)). The editor makes this explicit, in words (never by
colour alone), and gives every row a per-row **Reset** (Reset to inherited):

- Every setting row and button row of a device or program profile shows its state: "Inherited from
  Global: 5", "Inherited from Global (built-in default): 5", "Set in this profile (inherited: 5)" or
  `From joystick mode "timeline": 21`. A program profile also names the device profiles that set the
  same key ("; device Pro sets 9"), because the profile cannot know which device is active.
- In Global a value that device or program profiles override shows "N profiles override this" (the
  tooltip lists them), so it is clear why the value has no effect there. The same hint appears at
  the joystick mode and at the button rows.
- The Overview cards and the profile list show how many settings a profile overrides.
- The first choice of a button's drop-down is "Default (inherit from Global)" in device and program
  profiles; the Reset of a row, "Default (inherit)", "Same as Global" and Reset all remove only the
  profile's own key.

### Add application and the device list

- Add a device profile from the list of connected devices. The list and the factory button count
  (which limits the buttons you can bind) come from the daemon's live feed; without the feed the
  page falls back to the `device=` line of the status file. When the device is known, the tooltip of
  each "Button n" label says how many buttons the device has; the Device page lists the same.
- "Add application..." opens a picker of the programs that are running now: search by name, match
  string or window title, refresh, and see the exact match string that will be stored. You never
  have to type an internal name. "Capture next focused program" waits a few seconds and takes the
  program you switch to. A manual entry with validation is the fallback.
- Search: the box above the profile list shows only the programs and devices whose name contains
  the text; the box above the tabs shows only the settings of the selected profile whose label or
  key matches (button rows by "Button n", `BUTTON_n` or the action). Matching ignores ASCII case.
- Button bindings: a button is bound to a key, a joystick mode switch or navigation. Remember the
  principle: a binding that is the same everywhere and always belongs in the vendor driver, see
  [modes.md](modes.md#buttons-and-the-vendor-driver).

The program list comes from implementations of `src/platform/running_apps.hpp` for X11, Windows
(`EnumWindows`) and macOS (`NSWorkspace`); the Windows and macOS ones are only compile-checked.

### Old NAV_BUTTON / CLICK_BUTTON settings

When the interface loads a configuration that still sets the navigation or click button with
`NAV_BUTTON` / `CLICK_BUTTON` (and the matching `..._ENABLED`), it moves them once to the equivalent
`BUTTON_<n>` entries (`BUTTON_<n> = nav`, `BUTTON_<n> = click`, n = the old 0-based number + 1) in
the profile that had them, writes the file atomically, tells the daemon and says so in the footer
("Moved the old navigation/click button settings to the Buttons tab"); the Buttons tab keeps the list
of what was moved. A pair is moved all or nothing: if a button is above the button count of the
device or the profile already binds that button to something else, the old keys stay in the file
untouched and the Buttons tab lists them as "Not moved: ...". Disabled old keys (older saves wrote
`NAV_BUTTON_ENABLED = 0` everywhere) are simply dropped. The daemon still understands the old keys.

## Tray menu

Status line, "Joystick mode" submenu (the Global joystick mode; every entry has its description as tooltip), "Enabled", "Settings...", "Open config file",
"Show log...", "Open joystick modes folder", "Release all keys", "Restart daemon", "Restart interface",
"Quit". "Release all keys" starts
`dofscroll --release-keys` as a separate process (the daemon binary next to the interface, else
`dofscroll` from `PATH`); the same command works in a terminal (see
[troubleshooting.md](troubleshooting.md#a-key-or-scrolling-gets-stuck)).

"Restart daemon" releases held keys first (as "Release all keys"), then restarts the daemon:
on Linux `systemctl --user restart dofscroll.service` when the unit is active, on macOS
`launchctl kickstart -k gui/<uid>/io.github.dofscroll.daemon` when the LaunchAgent is loaded; on
Windows (and when the service manager does not manage the daemon) it stops the process (Windows:
`scripts\stop-daemon.ps1`) and starts the `dofscroll` binary next to the interface. "Restart
interface" restarts this program: under `dofscroll-ui.service` through
`systemctl --user restart dofscroll-ui.service`, otherwise it starts a copy of itself with
`--restart` (which waits up to 8 seconds for the old instance to release the single-instance
lock) and quits. No shell is involved; the command lines are built in
`src/ui/model/restart_plan.hpp`. On Windows the helper programs start with `-NoProfile
-NonInteractive -WindowStyle Hidden` and `CREATE_NO_WINDOW`, so no console window flashes up, and a
failed stop of the daemon is reported instead of starting a second one. Untested on real desktops
for Windows and macOS.

## Platform notes (Windows and macOS)

Compiled and unit-tested in CI only; what has to be checked by hand on real machines:

- **Windows console output.** `dofscroll-ui.exe` is a GUI program without a console. For `--help`,
  `--version`, `--self-test`, `--screenshots` and the "already running" message it attaches to the
  console of the parent (cmd, PowerShell) and writes there, unless the output is redirected to a
  file or pipe. Started by double click there is no console and no output. To check: run
  `dofscroll-ui.exe --version` in PowerShell and in cmd, also with `| more` and `> out.txt`.
- **macOS menu bar icon.** The tray uses a monochrome template icon
  (`icons/dofscroll-tray-template.svg`, `QIcon::setIsMask`) that macOS colors for light and dark
  menu bars; the "disabled" and "not running" states are the same icon with less opacity. macOS does
  not deliver a click on the icon when the icon has a menu, so there is no "click to open the
  window": use the **Settings...** entry of the menu.
- **macOS Dock.** The `.app` has `LSUIElement` (no Dock icon). The same Info.plist is embedded in
  the plain executable of a CMake build (linker option `-sectcreate`), so a run from the build
  directory does not show a Dock icon without a window either. Unchecked on hardware.
- **Output backend.** The "Output backend" choice (X11, uinput) on the Settings page exists on
  Linux only; Windows uses SendInput and macOS CGEvent.
- **German catalog.** `dofscroll_de.qm` is next to the executable on every system (Linux
  `/usr/bin`, Windows install folder, macOS `dofscroll.app/Contents/MacOS`). To check: set the
  language to German on the Settings page and restart the interface.

## Reload and runtime files

Saving in the settings window notifies the running daemon: SIGHUP on Linux, a named event on
Windows, a per-user local socket on macOS. The status, log and joystick-mode-request files live in one
per-user runtime directory that daemon and interface agree on (`$XDG_RUNTIME_DIR` on Linux,
`%LOCALAPPDATA%\dofscroll` on Windows, `~/Library/Caches/dofscroll` on macOS). The interface
reads the status file only when it is bounded in size, not a symlink and owned by the current
user; without a safe directory the daemon counts as not running. While the Axes page measures,
the interface sends a heartbeat so the daemon keeps its output paused. The Windows and macOS
paths are CI-verified only, not run on hardware.

## Start at login

The Settings page has one switch for the daemon and the interface. It uses systemd user units on
Linux (`dofscroll.service`, `dofscroll-ui.service`), the per-user Run key on Windows and a
LaunchAgent on macOS. If the interface is not installed, only the daemon is switched. See
[installation.md](installation.md).

## Terms

The interface says **Joystick mode** for what the axes do (config key `MODE`, files in `modes/`)
and **Button mode** for the named behavior of one button (`buttons/`, `buttonmode:<id>`); the plain
word "mode" is not used (the one exception is the sidebar entry "Modes", the category that holds both kinds as tabs). "Navigation" is the arrow-key action of a button, not a joystick mode. `scripts/check-terms.sh` (run by the lint job) fails when a translatable UI string says plain "mode". The titles of
the pages and tabs come from one place, `src/ui/model/ui_terms.hpp`; to rename a kind, change the
constant there and the texts that carry the word (search the old word in `src/ui`, `po/de.po` and
`docs`: the sentences in `src/ui/qt/pages/*.cpp`, the model texts in
`src/ui/model/{mode_catalog,mode_info_model,binding_editor_model,settings_model,status_model,
overview_model,tray_menu_model,examples_model}.hpp`, and the German catalog "Joystick-Modus" /
"Tasten-Modus"). The GTK tray (`dofscroll-tray`) is separate and keeps its own words.

## Languages

English is the source language; the German catalog (`po/de.po`, `dofscroll_de.qm`) is installed
with the interface.
