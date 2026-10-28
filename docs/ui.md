<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# The dofscroll interface (`dofscroll-ui`)

`dofscroll-ui` is the Qt6 interface: a tray icon plus a settings window, one code base for Linux,
Windows and macOS. It edits the same config file as the daemon and asks the daemon to reload, so
changes apply immediately. There is no Save button: every change is saved by itself (see "Saving"
below). The daemon runs without it. The interface starts minimized to the
tray (no window opens); "Settings..." in the tray menu or a click on the tray icon shows the window. Without a
system tray the window is shown minimized instead. Two exceptions: the very first start of a new
installation shows the window with the [setup guide](#the-setup-guide), and **every start starts the
daemon when none runs** (see below), so that the first start after installing is simply opening
dofscroll from the start menu, the application launcher or the Applications folder.

**The interface starts the daemon.** It decides from the status heartbeat (the same one the status
line uses). A login autostart starts the daemon and the interface at the same moment, so the interface
waits about 6 seconds from its own start and looks again; only when there is still no heartbeat it
starts the daemon, once per run (Linux: `systemctl --user start dofscroll.service`, the daemon
binary directly if that fails; macOS: `launchctl kickstart` if the LaunchAgent is loaded, otherwise the
daemon inside the app; Windows: `dofscroll.exe` next to the interface). A daemon that is seen running
is never started again by the interface, also not after it was stopped on purpose. The daemon refuses
to run twice (single-instance lock, see [security.md](security.md)), so a race does no harm. Not run for
`--self-test` and `--screenshots`.

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

The Channels tab also has **Invert direction of** (a check box per motion, key `INVERT_AXES`; flips the
direction in every joystick mode, for example when pushing scrolls the wrong way but tilting is right) and
**Zoom key** (`ZOOM_MODIFIER`, the keys held while zooming, `super` for Cmd on macOS); both can be set per
profile, and the motion chips show the zoom key of the profile.

**One component, used everywhere (what a joystick mode or a profile does).** The six motions of the
SpaceMouse cap always appear in the same order with the same symbol and the same chip style, on every
page that shows what a joystick mode or a profile does: vertical push (symbol shows the action),
vertical tilt (rx), horizontal push (x), horizontal tilt (rz), lift / press (y, normally "Not used") and
twist. Which raw axis a motion is (z or y for the vertical push, ry or rz for the twist) comes from your
channel mixes, see below. In the 3x2 panel the top row holds only pushes (vertical, horizontal, lift / press) and the bottom
row only the tilts and the twist; the Joystick modes list shows the six symbols the same way, two lines of three. A chip starts with a marker for the kind of motion (a flat bar for a push, a
wedge for a tilt, a turn arrow for the twist), then the symbol of the action and the word; a Keys or Tap action
shows its keys instead of the word ("Left / Right", "Ctrl+Page up / Ctrl+Page down") and the Zoom action says
"Ctrl+Wheel" (the keys of `ZOOM_MODIFIER`, read from the profile, and the mouse wheel), so a zoom by keys such as Ctrl+Plus / Ctrl+Minus is not
mistaken for it and two motions with keys can be told apart. The shared view components are in `src/ui/qt/motion_view.hpp` (`MotionStrip` six small chips,
`MotionGrid` the 3x2 panel with name, symbol, action, how the cap moves and where the value comes from,
`ButtonChips` the buttons as numbered chips such as "1 Space", "2 Right click", "15 Navigation"), the
models behind them are toolkit-free (`motion_summary_model.hpp`, `button_chip_model.hpp`,
`profile_assignment_model.hpp`) and unit-tested. They are used by the Joystick modes list and detail view,
the General tab of a profile (the effective assignment: joystick mode, the profile's own `BUTTON_<n>` and
inherited ones, each with "from the joystick mode", "from this profile" or "inherited"), the Overview cards
(compact strip), the "i" popover (text) and the Button modes page (the chip of the chosen action). A chip never
relies on colour: an off or unused motion and an unassigned button have a dashed outline, a muted colour and
the word ("Off", "Not used", "Unassigned"). Chip text stays short (the keys of a key action are shown in full), the rest is the tooltip. All lengths come
from the font metrics and the chips wrap, so no page needs a horizontal scroll bar at the default size
(`--self-test` checks it). A new place that shows what a mode or profile does must use these components
instead of drawing its own text.

New pages should use plain layouts and group boxes and set no margins of their own; mark a primary
button with `setProperty("primary", true)`, a check box that is one of several choices with
`setProperty("look", "box")`, a heading label with `setProperty("heading", "page")` or `"section"`.

Every page sits in a scroll area: in a small window text stays readable and controls keep their
size, and the page scrolls (horizontally only as a last resort) instead of squashing. Info,
status and help labels wrap. The default window is 1800x1100 (clamped to 92 % of the available screen; the list | editor splitters of the Profiles, Joystick modes and Button modes pages start proportional, with at least 65 % of the width for the editor side); the saved
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
| Profiles | Profiles | The list of profiles (Global first, then devices, then programs) and the editor of the selected one with the tabs General, Channels, Motions and buttons and Overrides, see "Profiles" below |
| Modes | Editor | The visual editor at full width: choose a joystick mode, click a motion of the cap or a button and choose what it does from big action tiles; see "The Editor page" below. A built-in joystick mode is copied first ("Make my own copy") |
| Modes | Joystick modes | The available joystick modes (built-in and files in the joystick modes folder), one row each with name, origin badge, one description line and six motion chips; the detail view shows the six motions as a panel, the buttons as chips and the profiles that use it. Actions: new, new from example, duplicate, import (button or drop `.conf` files onto the page), export, rename, edit in the page, delete; for every joystick mode the profiles that use it; a reminder that joystick mode files can press any key |
| Modes | Button modes | The button mode files (`buttons/`): create, new from example, edit in the page, duplicate, import, delete; used from the button drop-downs of the Profiles page |
| Modes | User modes | Your joystick mode files and button mode files in one list, each with a switch (on / off), where it is used and Remove; "Open folder" for both folders, see "User modes" below |
| Device | Device | The connected devices and their button count (the number of button rows a profile offers), the daemon state |
| Device | Axes | Live bars of the six axes (and, while the active joystick mode splits a scroll channel, the push and tilt parts of that channel) and the axis assistant (wizard) that detects which axis does what |
| Settings | Settings | Output on/off (`ENABLED` of Global), start at login, language and theme (the box "Language and appearance"), the daemon settings (tick rate, output backend; on Windows and macOS also **Input path**: Automatic, 3Dconnexion driver or Direct (HID), key `INPUT`, with the active path shown), the read-only status of the daemon (including "Input: ..." on Windows and macOS), "Open config", "Show log...", "Documentation" and an **About** box (version, licences, Qt under the LGPL and, on Windows and macOS, the 3Dconnexion note) |

The Channels settings (sensitivity, dead zone, direction, curve, zoom detection, smoothing) are not a
page of their own: they belong to a profile and are the Channels tab of the profile editor, for
Global and for every other profile. No setting appears in two places: the output switch is on the
Settings page (not in the Global profile), the daemon settings (tick rate, backend) are on the
Settings page too (they do not belong to a profile).

## Several devices

With one lane per device (Windows and macOS, direct HID; [multi-device.md](multi-device.md)) the status line lists every device with its joystick mode ("Devices: A (Joystick mode: normal), B (Joystick mode: timeline)") and the warning "N SpaceMice connected, only 'A' is reported" does not appear. The Axes page shows the device that moved last, named in the status line; with more than one device a "Device" selector (also in the axis assistant) pins the page to one device ("All devices" = the one that moved last). While one device is pinned, the bars show its samples only, so they update when that device moves. The first device's values stay in the plain `profile=`, `mode=` keys of the status file, the others are `device2=`, `mode2=` and so on.

## Overview quick actions

- **What do you want to do?** Three buttons that open the page for a task: "Tune scroll and zoom"
  (the Channels tab of the Global profile), "Check the axes" (Axes) and "Switch joystick modes"
  (Joystick modes).
- **Show setup guide** opens the [setup guide](#the-setup-guide) again, at any time.
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
- **Release all keys** and **Restart** (daemon and interface together) as in the tray menu.
- The filter box above the profile cards matches every word against the name, the joystick mode, the kind
  ("device" or "program") and the settings of a profile, ignoring ASCII case. A card says how many settings the profile overrides ("Overrides 3 settings"; the
  profile list on the Profiles page shows the same number after the name). The tooltip of a card
  lists all its settings, not only the first ones shown in the summary.
- On a very wide window the Overview and the Channels tab stop at a maximum content width
  (1500 px, `kMaxContentWidth` in `src/ui/model/design_tokens.hpp`) and stay centered.


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

The editor has four tabs: Motions and buttons (the picture, below), Settings (name, description, the
three channels), Buttons and Overrides. Each tab scrolls on its own; the problem list and the buttons "Open in text editor" and
"Close editor" stay visible below the tabs. The selected tab is kept while you edit and when you
open another joystick mode.

### The Editor page

The **Editor** page (category Modes, first tab) is the visual editor at full width, so the picture has room. At
the top you choose the joystick mode; a built-in joystick mode is not a file, so the page says so and offers
**Make my own copy** (a new file with its own name, chosen at once). Below, the "Motions and buttons" tab of the
joystick mode editor (next section) and its Settings, Buttons and Overrides tabs for everything else. There is
one scroll bar, the window's: the tabs do not scroll on their own here. When the window is too narrow for the
picture and the inspector side by side, the inspector goes below the picture.

### Motions and buttons (the picture)

The first tab shows the cap and its six motions (push forward/back, push sideways, tilt forward/back,
tilt sideways, twist, lift/press) and the numbered buttons of the device. Every motion and button is a
box with its name and what it does in words ("Scroll", "Keys: Up / Down", "Off", "Not used",
"Unassigned"); a muted item has a dashed outline and keeps its word, so nothing is shown by colour
alone. Click an item, or move to it with the arrow keys (Enter goes to the inspector); the inspector next
to the picture shows the selected item:

- **A motion:** "What it does" is a grid of big tiles, one glance each: scroll up and down, scroll sideways,
  zoom, Page up / Page down, arrow keys (up and down on the vertical motions, left and right on the others),
  other keys (any key or chord per direction, set in the Keys box that appears: modifier check boxes and a key list, or **Press a key...**: click it and press the key or combination, which is taken over; Esc cancels, a modifier alone waits for its key, and a key dofscroll cannot send is refused in words), one key tap per push, do
  nothing and as in the profile. The tile of the motion's action now is lit (thick border, bold text). The editor hides "channel" and "split": push and tilt of one channel with the same
  action are written as `V_ACTION` / `H_ACTION`, different actions as `V_PUSH_ACTION` / `V_TILT_ACTION`
  (`H_...`), the same keys the Settings tab writes. **Reverse direction** writes the existing sign
  (`SCROLL_V_SIGN`, `SCROLL_H_SIGN`, `ZOOM_SIGN`, or `SCROLL_<V|H>_<PUSH|TILT>_SIGN` while the channel is
  split) as `-1` and removes the line again. "Same as ..." copies the action of another motion, "Swap
  with ..." exchanges two. The same works with the mouse: drag one motion in the picture onto another to
  swap them, hold Ctrl (Cmd on macOS) while dropping to copy (a dashed frame marks the motion the drop would
  land on). Lift / press and the buttons cannot be dragged; the two lists are the way without a mouse. "Tune this motion" (closed until used) edits sensitivity, dead zone, curve and
  smoothing of that motion as overrides of the joystick mode.
- **Analog or digital.** Every motion that presses keys (the "Keys" box) has the check box **Analog** (accessible name: "Analog (the key repeats faster the
  further you push)**: checked is `keys:`, unchecked is `step:` (one press per push, like a click); it only switches the
  prefix of the stored action, the keys stay. Two more check boxes, "No key for the negative / positive direction", write
  `none` for a side. Below: the fields "Lowest rate" and "Highest rate" (presses per second, analog) and the "Trigger level"
  (with "about N % of full deflection"; analog: the level of the first press, digital: the level of every press). An empty
  field means "not set": no lower limit, no upper limit below 100 presses per second, and the level `STEP_TRIGGER`; the
  placeholder shows what applies. Turning analog on starts with 3 to 25 presses per second (the two rate fields). An invalid
  number is shown next to its field and never written (same check as the file loader). The check box is greyed, with the
  reason in words, for a key that cannot repeat (letters, digits, Enter, Delete, Esc, Alt or Super combinations) and for the
  lift. The rate and trigger fields are keys of the joystick mode (`SCROLL_V_PUSH_KEY_RATE_MIN` ... `LIFT_TRIGGER`,
  [modes.md](modes.md#the-key-engine-analog-and-digital)); a tilt of a channel that is not split uses the push keys.
- **Mouse click and hold.** The list "What this motion does" (and the tiles of the joystick mode editor) offers **Mouse
  click per push** and **Hold a key or button while pushed**. Each direction has a key field built from the modifier check boxes
  and the key list; the list has a **Mouse** branch (Click, Right click, Middle click) where a click or a mouse button can be
  chosen, and "Nothing for this direction" switches a side off. A hold keeps its key or button down while you push and lets go
  when you ease back to half of the trigger level. The Analog check box stays greyed, with the reason written next to it, for a
  click and a hold (they are always digital). Everything is operable with the keyboard in the order of the screen (list, negative
  direction, positive direction, trigger level), every control has a name for screen readers, and the inspector wraps its text,
  so it reads fully at a window width of 1100 pixels and at larger fonts.
- **A channel mix written by hand** (an override such as `SCROLL_V_MIX = z`) is never changed by the
  picture: a motion it does not use says "Not used: custom channel mix" and offers "Show in Settings tab".
- **Lift / press** has its own list "What it does" with the same choices as on the Profiles page: "Nothing (the default)",
  scroll, scroll sideways, zoom and one key tap per push (written as `LIFT_ACTION`; no analog key). The inspector also adds it as a term to the mix of one channel
  (`SCROLL_V_MIX = z + rx + y`, with a weight) and says in words that it is prone to false triggers when
  the cap is grabbed or released. No built-in joystick mode uses it (`tests/test_no_lift_press.cpp`).
- **A button:** the same button row as on the Buttons tab (the actions, the button modes, "Unbind").

**Motions and buttons of a profile (Profiles page).** On the Profiles page the same picture edits the selected
profile (Global, a device or a program). Global and the device profile are the base; what the profile does not
set is inherited and said in words. Every one of the six motions can be edited on its own, push and tilt of a
direction included, and has three states: inherited ("On. Inherited from Global ..."), set here ("On. Set in this
profile (inherited: on)") and cleared ("Cleared in this profile: nothing happens for this motion here"). The box
"In this profile" has the list "What this motion does in this profile" (normal, scroll, scroll sideways, zoom, keys,
one key tap per push, nothing; a key action shows two key pickers), **Clear** (the action of the motion is "none" in
this profile only, for example the twist in a code editor profile), **Switch on** (the action the profile had, else
the inherited one, else the normal one; never left at nothing) and **Use the inherited setting** (removes what the
profile sets for the motion). The actions are the keys of the joystick mode files, `V_PUSH_ACTION` ... `TWIST_ACTION`
(see [configuration.md](configuration.md#precedence)); the program profile wins over the joystick mode, a device
profile or Global cannot beat a joystick mode that sets the motion itself (the interface says so and writes
nothing). The chips, the list texts and the Overview say "Off" for a cleared motion and never show its action. Every
button has the same three states: its row (the action drop-down, "Inherit") and **Clear** (`BUTTON_<n> = none`, nothing
happens although Global, the device or the joystick mode binds the button), with "Set in this profile", "Inherited from
Global: ..." and "device X sets ..." in words, and a note when the joystick mode binds the button too (a program profile's
binding wins). Everything is saved at once; there is no Save button; an invalid value is shown and not written.

**Lift / press in a profile.** The sixth motion is edited like the others, with a shorter list because its default is
nothing and it fires when the cap is grabbed: "Nothing (the default)", scroll, scroll sideways, zoom and one key tap per
push; an analog key repeat is not offered (the Analog check box is greyed with the reason). The note under the list says that lifting can fire when
you grab or let go of the cap and that the dead zone (`LIFT_DEADZONE`, default 80, shown under "Tuning in this profile")
is the remedy. **Clear** pins nothing for this profile (`LIFT_ACTION = none`), **Use the inherited setting** removes the
key; "Switch on" and "Take over from ..." are not offered for the lift. Which axis it is comes from `LIFT_MIX` (`y` with
spacenavd, `z` in the Windows and macOS seeds), not from the program. A lift without an action is "Not used", a cleared one
"Off". The joystick mode editor has the same list for the lift.

**Analog or digital in a profile.** A motion whose action presses keys shows the same check box, the two "No key" boxes and the
rate and trigger fields below the key pickers; they are keys of the profile (`SCROLL_H_TILT_KEY_RATE_MAX`, `ZOOM_TRIGGER`, ...) and
follow the usual inheritance. See the first bullet of "Motions and buttons (the picture)" for the words.

**Help where you work.** The line under the picture says in plain words what the selected item does, with
the real rate from the channel processing at full deflection ("Vertical tilt: presses Page up or Page
down, about 3 times a second at full deflection"); while Detect is on it follows the real deflection ("...,
now: Page down, 2 a second"). Every element (motion, action, reverse, tuning, button, the picture) has a
**?** button with a popover: what it is, the axis, an example and the file key (Esc closes it). **Start
from a template** fills the three channels (Scroll and zoom, Page up and down, Arrow keys, Timeline, Zoom
and pan); name, buttons and overrides stay. **Hints** name the cause of a likely mistake and have the fix as a
button: a motion that does nothing ("Assign it"), two motions that press the same key, a channel mix of the
joystick mode that differs from the profile's, a bound button the device does not have ("Unbind"). A
guided tour (four steps: move the cap, pick an action, press a button, try the joystick mode) runs the
first time the tab opens and from **Show me how**; with a device and a running daemon each step waits for
the real movement or button press, otherwise (or with "Click it instead") you click. It is kept in
`ui-progress.conf` (`motion_tour_seen`) and not shown again once finished or skipped. All of this works
without hardware and without the daemon; only the "wait for your real movement" steps need them.

**Detect by moving** selects the motion you move (the strongest axis wins, crosstalk and ties are ignored,
the rules of the axis assistant) and the button you press. It needs the running daemon with the live feed and
pauses the daemon's output only while it is on, with the state in words; switching it off or leaving the tab
resumes the output at once. **Try this joystick mode** switches the running daemon to this joystick mode
now, on all profiles (like the tray's menu; the saved file is applied), says "Trying: <name> on all
profiles" and offers "Back to <previous joystick mode>".

The buttons follow the device: the attached device if the daemon reports one, otherwise choose a model
under "Device:" (SpaceMouse Compact 2 buttons, Pro 15, Enterprise 31) or get a plain numbered grid. The
button positions are a schematic arrangement, not a copy of the device. The picture and the Settings tab
edit the same form, so a change in one shows in the other at once.

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

### The picture in a profile and on the Button modes page

The **Motions and buttons** tab of a profile (Global, device or program; between Channels and Overrides) shows the
same picture for that profile. The six motions say what the profile's joystick mode does with them, in words;
that action belongs to the joystick mode, so the inspector points to the Joystick modes page. What the profile
sets itself is edited here: for a motion the **tuning of its channel** (sensitivity, dead zone, direction,
curve, smoothing) as overrides of the profile, each with where its value comes from ("Inherited from Global: 5",
"Set in this profile (inherited: 5)") and an Inherit or Reset button; for a button the button row (every button
of the device, one row each, with its origin in words; in Global the plain key and click bindings sit behind a
collapsed "Advanced" section; the notes about moved old button settings are shown above the picture). A profile
whose joystick mode brings its own button bindings shows the buttons read-only. There is no separate Buttons tab
on the Profiles page.

The **Button modes** page has "Use it on a button": the buttons of the device with what the Global profile binds
to each of them, no motions. Choose a button and **Use on this button** (its tooltip says which button mode and which button) binds the
selected button mode to it in the Global profile (the same binding the Profiles page writes, saved by itself), **Unbind** removes
the binding. A line says in words which buttons use the button mode now. Without an attached device the picture
shows a numbered grid of eight buttons.

## Where the interface gets the axis meaning

The axis names depend on the driver (spacenavd: `z` is push forward/back and `y` lift/press; the 3Dconnexion
driver on Windows and the 3DxWare client API on macOS: `y` is push forward/back and `z` lift/press), so the
interface never assumes them. `src/ui/model/axis_roles_model.hpp` derives the roles from the **profile that is
shown** (global < device < program, the same inheritance as everywhere): the three channel mixes
(`SCROLL_V_MIX`, `SCROLL_H_MIX`, `ZOOM_MIX`) are matched against a small table of patterns (vertical = push +
tilt, horizontal = push - tilt, zoom = twist; signs and weights do not matter) and the axis the mixes leave out is
lift/press. A joystick mode's own mix override moves axes between channels, it does not rename them. If you or the
axis assistant change a mix, the interface follows; a device profile with its own mixes is read the same way.

- When the mixes match a pattern, the interface names the movements (push, tilt, twist, lift/press) and the axes.
- When they do not match (a hand-made mix), it says only what the file says ("Axis y scrolls vertically", "the
  vertical scroll uses the axes ..."), shows the six motions as "Not used", and shows no lift/press warning.
- With no configuration at all (fresh state) it reads the default file of this system, the one the first start
  seeds (`examples/default/<os>/dofscroll.conf`, chosen by `seedOsName()`); the values are not repeated in code.

For developers: a new operating system, driver or device needs only data. Add its default file under
`examples/default/<os>/` (CI validates it), or a device profile with its mixes; if its mixes have another shape,
add one row to `rolePatterns()` and one row to `tests/test_ui_axis_roles.cpp`. Tests supply the fresh state
explicitly (`tests/ui_fixture.hpp`), they never depend on the host system. A later extension (for example a
hint block in a `devices/*.conf` or a driver note) can feed `deriveAxisRoles()` without touching the views.

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
- The window follows the configuration: when the tray (Enabled) or a text editor changes `dofscroll.conf` or a
  profile file and the window has nothing of its own pending, the window reads it again.
- Only the files that changed are written: the file of an edited profile, `dofscroll.conf` for Global, one write and
  one reload for the whole burst. A file that was changed on disk since the window read it is never overwritten (see
  "Profiles").
- The first start after an update moves the profiles of an old single `dofscroll.conf` into one file each (the old
  file is kept as `backup/dofscroll.conf.pre-split`); the footer says so once: "Your profiles are now one file each.
  The old configuration file is kept as backup/dofscroll.conf.pre-split."

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

## The setup guide

The first start of a new installation (no configuration file existed and none was migrated) shows the
settings window with a guide of a few steps. Each step checks the live state once per second and says
in words whether it is done ("Done", "To do", "Needs attention"; a symbol before the words, never colour
alone):

1. **dofscroll is running**: the daemon heartbeat. The interface starts the daemon by itself (see
   above); while that is under way the step says so, and if it did not work it offers **Start now** and
   points to the Troubleshooting page.
2. **SpaceMouse found**: `devices_seen` of the status file. If it is not found and, on Linux, the
   `spacenavd` service is not active, the step says so in plain words and shows the fix as a command
   you can copy (`sudo systemctl enable --now spacenavd.service`), as a fallback only: the Linux
   packages enable that service themselves. If the device is held by another program the step says that.
3. **Start at login**: a check box, **on by default** (applied at once through the same switch as the
   Settings page: systemd user units, the Run key, LaunchAgents). Unticking it switches it off again.
4. **macOS only, Input Monitoring and Accessibility**: one row for each permission with the state the
   daemon reports (`permissions=` in the status file), a button that opens the matching pane of System
   Settings, and a note while macOS does not accept the events, with the path of the daemon to add if
   it is not in the list.
5. **Try it**: push the cap, the window under the mouse pointer scrolls; twist it, the window zooms
   (information only, there is nothing to tick).

**Skip the guide** and **Finish** (and the close button) only close it; they are the only buttons that
do, nothing needs saving. That the guide was seen is remembered in `ui-progress.conf`
(`setup_guide_seen = 1`), so it does not come back by itself. An existing installation, one whose
configuration existed before this start, never shows it by itself. **Show setup guide** on the
Overview opens it again. The check box is only read when the guide is reopened.

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
| Font size | The base font is the system font (plus 8%); the interface has no fixed pixel font sizes. The lengths of the style sheet (padding, heights, radii, indicator sizes) are in em, converted from the design pixels at a reference font of 14 px (`src/ui/model/qss_scale.hpp`), so the geometry follows the font size of the system; border widths stay in pixels. At 150% and 200% font size and in a window of 560 px, in the System theme and in the Light theme, no label or button is cut off: a page that is too narrow scrolls (the scroll bar appears only then); at the normal font size in the normal window no page shows a horizontal scroll bar (the normal window is the first-start size of 1800 x 1100 px; on a smaller screen it is cut to 92 % of the screen and a page may scroll, as it may in a window you made smaller) | `test_ui_qss_scale`, self-test "font scale" |
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

### Axis assistant

"Axis assistant..." on the Axes page detects the five motions (push forward/back, tilt forward/back,
push sideways, tilt sideways, twist) with the cap in your hand: **no click is needed**. The page
shows the motion to make as one short line in direction words with an arrow for the first movement
(never axis names), the five motions as chips with their state in words, and the live bars of the six
axes so you see the movement being picked up. Each step goes like this:

1. **Waiting for you to move the cap...** The step starts by itself when the cap, after a short rest,
   moves clearly (a single spike or a movement that is still going on from the step before does not
   count). **Start now** starts it at once.
2. **Measuring.** The step ends when the cap has moved out and back and has been at rest for a
   moment (400 ms by default). After only one movement it ends after a longer rest (2 s); the
   longest step is 8 s.
3. **Detected.** A valid result is shown for about a second ("Detected: Push forward/back"), then the
   next step starts by itself. **Next** goes on at once, **Back** returns to a step to look at it and
   **Measure again** throws the result away and waits for a new movement.

A result that is too weak or not clear stays on the step and says what to change in words; the
next movement measures the step again. A required step never gives up by itself (after the longest
time the page only says that no movement was seen). The two optional tilt steps skip themselves
after 5 s without movement ("skipped, no movement") and can be done later: **Back** from the result
page, or run the assistant again. The result page lists what was detected per direction ("Push
forward/back: z, positive"; the axis names appear only here, because they are what is written) and
the warnings. The warning "lift/press was strongest - probably pressed down too hard" is about the axis that
is lift/press in the configuration shown (the axis the channel mixes leave out: `y` on Linux, `z` with the
3Dconnexion driver on Windows); where the configuration does not say, the assistant falls back to a table per
system (`src/axis_convention.hpp`: Linux order, Windows driver order; macOS and the direct HID path use the
Linux order until they are measured, issue #186). **Apply** is the default button (Enter), **Cancel** and Esc leave without writing. The result page lists what
Apply changes in the Global profile ("now ..., new ..." for each mix) before anything is written, and after Apply
the Axes page offers **Undo**, which puts the old values of the same keys back (rule below). The check box
**Don't advance automatically** (off by default, remembered in `ui.conf`) keeps a detected step on screen until you
press Next and never skips an optional step by itself. The axis rows show a plain second name ("z: Push forward /
back") where the configuration says what the axis is; the Device selector exists only with two or more devices;
the Pause, Reset peaks and Axis assistant buttons are in the top row, so a low window does not cut them off. Apply
writes exactly the keys it always wrote (`SCROLL_V_MIX`, `SCROLL_H_MIX`, `ZOOM_MIX`, the three
`*_SIGN` switches and `ZOOM_LATERAL`). The state is always text ("Waiting...", "Measuring...",
"Detected"), the whole page works with the keyboard (Tab order: Apply, Next, Start/Measure again, Skip,
Back, Cancel), and the daemon output stays paused while the assistant is open.

**Undo stays available until you change something else; there is no time limit.** After removing a profile and
after Apply in the axis assistant the page keeps an Undo offer (a bar with **Undo** and **Dismiss**, both reachable with
the keyboard) for as long as you need, a minute or an hour. The offer ends only when something else makes it obsolete:
for a removed profile another change of the profile list (add, remove, change application or device, move, import,
New profile) or a reload of the configuration from disk; for Apply the next Apply or Undo, or a change of the Global
values after Apply (Undo would overwrite it); and always when you press **Dismiss** or close the window. The two kinds
are kept apart, so one does not drop the other. Ctrl+Z (Cmd+Z on macOS) on the Profiles page and on the assistant page
does the same as the visible Undo button, but only while the focus is not in a text field, a spin box or an editable
list, so the undo of the field itself keeps working; the button's tooltip shows the shortcut. A screen reader is told what can be
undone and how (button and shortcut). No timer, animation or other visual element ever hides or disables the Undo control.

The thresholds and times are keys in `ui.conf` (next to `language`, `theme`); there is no control
for them in the interface. They are for people whose device needs other values: delete a line to get
the default back. A value that is not a number is ignored (the default stays); a number outside the
range is pulled to the nearest limit. The interface keeps your lines when it saves another setting.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `axis_assistant_start_threshold` | `40` | 10 to 200 | Raw deflection above which the cap counts as moving (starts a step, and counts the movements) |
| `axis_assistant_rest_ms` | `400` | 100 to 3000 | Rest in milliseconds after out and back that ends a step |
| `axis_assistant_max_s` | `8` | 3 to 60 | Longest step in seconds; also the time after which a waiting step says that no movement was seen |
| `axis_assistant_auto_skip_s` | `5` | 2 to 60 | Seconds without movement (counted from the last movement) after which an optional tilt step is skipped |
| `axis_assistant_manual_advance` | `0` | 0 or 1 | 1: a detected step stays until Next and no step is skipped by itself (the check box "Don't advance automatically" writes it) |

The keys are read when the assistant is opened. The analysis (`analyzeStep`, `evaluate` in
`src/axis_wizard.hpp`) and the written keys did not change; the flow is in `WizardController`
(`src/ui/model/axes_model.hpp`) and tested with synthetic samples and a fake clock
(`tests/test_ui_axes.cpp`). The feel of the defaults on a real SpaceMouse (does a step start too
early on a touch, end too early on a pause) needs a person with a device.

## User modes

The User modes page (category Modes, fourth tab) lists the mode files of yours: the joystick mode files
(`modes/*.conf`) and the button mode files (`buttons/*.conf`). A user mode is a plain file that the daemon's own
loaders read with their checks and warnings; nothing is executed. Each row has

- a **switch**: off renames `<id>.conf` to `<id>.conf.disabled` (the loaders only read names that end in `.conf`),
  on renames it back. The file is kept, nothing is overwritten (if the other name is taken, the page says so and
  leaves both files alone). A switched-off file with the id of a built-in joystick mode or button mode lets the
  built-in one apply again. The change is written at once, there is no Save button;
- the name and description of the file, and **where it is used** (profiles that select it, buttons that switch to
  it) so you see what a switch changes;
- a note **"Copy of a shipped example, not used: the built-in one applies"** when a switched-on file is
  byte-identical to a shipped example (a built-in text, or one of the texts that earlier versions copied into
  `modes/` on first start, including the very first release candidates). The loader ignores such a file, so it
  changes nothing; it is listed so you can see it and delete it. Change a single byte and it is your own file
  again;
- **Remove**, which asks first and deletes the file, switched on or off.

"Open folder" opens `modes/` or `buttons/`. Device description files (`devices/*.conf`) are not on this page:
the daemon does not load them from the configuration folder, only `dofscroll-hidprobe` reads them (see
`docs/devices.md`). The examples that ship with dofscroll are installed with "New from example..." on the Joystick
modes and Button modes pages.

## Joystick modes and button modes

**Key picker.** Wherever a key is chosen by clicking (the key of a button action, the keys of a motion), the choice is a tree in columns, like the Finder column view: the button shows the current key; clicking it opens a popup whose first column lists the categories (Keyboard, Media), choosing one shows its subcategories (Letters, Digits, Function keys, Navigation, Editing, Keypad, Punctuation, Modifiers, System keys; Volume, Playback) in the next column, and a subcategory shows its keys. The text that is stored in the file ("Stored as: pageup") is only in the tooltip of a key, not in the window. Columns are as wide as their labels and scroll when the screen is smaller than the popup; a scroll bar shows only where a column is longer than the popup. Up and Down move in a column, Right opens the selected category, Left goes back, Enter chooses a key (on a category it opens it), Esc closes the popup without a choice, and a click on a key chooses it. The open popup has the current key selected. The tree is data (`src/ui/model/key_tree_model.hpp`, tested in `tests/test_ui_key_tree.cpp`) and the control is `KeyTreePicker` (`src/ui/qt/key_tree_picker.hpp`); later stages can add branches such as Mouse or Functions without changing the control. "Press a key..." stays next to the picker for the keys of a motion.

The key of a button action can be a chord: the "Press key" action shows the check boxes ctrl, shift, alt and super next to the key picker. With a modifier ticked the action is `key:<mod>+<key>` (for example `key:ctrl+0`), which is tapped once per press; without one it is a plain key that is held while the button is held. The built-in button mode "Reset zoom" is `key:ctrl+0`.

There are two kinds of modes. A **joystick mode** says what the axes (and optionally buttons)
do: scroll, zoom, keys. A **button mode** is a named behavior of a single button (a key, a click,
navigation, a joystick mode switch). Config keys and file names do not change with the words
(`MODE`, `modes/*.conf`, `buttons/*.conf`, `buttonmode:<id>`).

**Descriptions everywhere.** Every joystick mode and every button mode shows its short description
(`DESCRIPTION`; all built-in joystick modes have one; "No description" when a file has none) wherever it can be
picked or listed: as tooltip of every entry of the joystick mode drop-downs (profile General tab,
the joystick mode of a button, the Overview's Active joystick mode, the tray's Joystick mode menu)
and of the button mode entries of the button drop-downs; as a second line (muted, wrapped) under the
name in the lists of the Joystick modes and Button modes pages (on the Joystick modes page the row is one description line, an origin badge and six motion chips; the full description is the tooltip); and in the detail view. Next to
the joystick mode drop-down of a profile is an **i** button: it opens a popover with the name, where
the joystick mode comes from (Built in / Your file), the description, what each of the six motions does
("Vertical push: Scroll", "Horizontal push: Keys: Left / Right", "Lift / press: Not used", ...) and the bound buttons. The description field of both editors is always available.

**Origin hints.** Every entry of the lists says where it comes from: "Built in", "Your file", "Your
file, replaces the built-in joystick mode" (button modes: "Built in", "Your file", "Your file, replaces the
built-in button mode"). A built-in button mode has no file: "Create editable copy" makes a file with the same
id that replaces it (a built-in joystick mode offers the same on "Edit"). **New from example...** (both pages) offers the
examples that ship with dofscroll and are not in your folder yet ("Example, not installed yet"); the
chosen one is copied like an import (checked, never overwrites) and selected. Delete removes only
your own file (a file that replaces a built-in one brings the built-in back).

The Profiles page has a "Joystick mode" drop-down per profile on the General tab (Global, device,
program): Global chooses the joystick mode that applies unless a device or program profile chooses
another one; the others have "Same as Global" as first choice. Every button row on the Motions and buttons tab
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
files that still use the button mode) and "Open button modes folder". With the action
"Next joystick mode" or "Previous joystick mode" the editor shows the section **Cycle through**: the
choice "All joystick modes" (alphabetical order of their ids) or "Only these joystick modes", a check list of every
loaded joystick mode (built in and yours; the ticked ones come first, numbered in cycle order), "Move
up" / "Move down" for the selected ticked one, and the result in words ("Order: 1 timeline, 2
normal"). Ticking a mode switches to "Only these"; no tick or "All" writes no `CYCLE` line (all
joystick modes). An id in the file that matches no loaded joystick mode is shown as "missing: <id>"
and kept (the daemon only warns and skips it); unticking removes it. It is saved by itself like the
other fields. A built-in button mode has no file, so its section is read-only and says that
and prev-mode go through all joystick modes while cycle-favorites uses timeline, then normal; "Create
editable copy" makes it editable. The tooltip of a button mode in the list shows "Cycles: timeline,
normal". The `CYCLE` list is kept when the name or description changes. The logic is in
`src/ui/model/cycle_editor_model.hpp` (tests: `tests/test_ui_cycle_editor.cpp`), the view in
`src/ui/qt/pages/cycle_editor_widget.cpp`.

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
  Channels, Motions and buttons and Overrides tabs**; a note says so, and switching the output on brings them back. (To
  switch only the axes off and keep the buttons, choose the joystick mode "off" and leave the output on.) A
  profile with another joystick mode also shows what that joystick mode does, read-only, with "Edit
  joystick mode file". Global has no output switch here: it is on the Settings page, and nothing is hidden for it.
- **Channels**: sensitivity, dead zone, direction, smoothing, axis mix, curve (exponent or points,
  with a live preview), zoom detection (the axes the twist must dominate, entry time, exit ratio) and
  the step trigger, in groups per channel. A filter box (the search box above the tabs) hides the
  rows that do not match; every row names its config key in the tooltip. "Restore defaults" removes
  every value of the tab from the profile; "Reset this group" and the per-row "Reset" do the same
  for a group or a row. The editor edits the selected profile, Global included.
- **Motions and buttons**: the picture of the cap for this profile (described below). Choosing a button
  shows its row in the inspector: one row per button of the device (factory button count), the effective
  action, where it comes from, and the drop-down described above. Global may assign buttons like any other
  profile. A profile whose joystick mode has its own bindings shows a note instead (the bindings
  are listed on the General tab).
  Next to every drop-down sits the same button chip as on the other pages (the number and the action in
  a few words, "1 Space"; muted with the word "Unassigned" when nothing is bound); it follows the
  drop-down at once and wraps under it with the font size.
  The search box above the tabs also applies here: while it holds text, only the buttons whose label ("Button 3"), key
  (`BUTTON_3`) or action text contains it are drawn in the picture; if none matches, a note says so.
- **Overrides** (advanced): the channel values this profile sets itself as raw text, with Remove
  (inherit again) and Add override. Global has nothing to override (it holds the base values).

Every profile lives in a file of its own (`devices/<name>.conf`, `apps/<name>.conf`; `dofscroll.conf` holds
Global), and the interface writes only the file of the profile you changed. The list shows the profiles of a kind in the
order the daemon applies them (see "Order of the profiles" below). A profile file that cannot be used (a
second profile in it, a header of the wrong kind, a line it cannot read, the same profile name as another file) is
listed as an entry of its own with the file name and "(defective)", explained in words under the list; it is
never overwritten while the problem stands. If an old `dofscroll.conf` still holds a section of the same name, that
section stays active and the profile's header says so ("This profile is still read from dofscroll.conf because its
own file ... is defective ..."). A profile that is still in an old single `dofscroll.conf` (or in a symlinked one) moves
into its own file when you edit it, unless its file is defective; with a symlinked `dofscroll.conf` the interface
keeps writing the single file and new profiles stay in it.

Below the list: **Add application...** (running-program picker), **Add device...**, **Import profile...** and **New profile...** (see
[Take over and the profile builder](#take-over-and-the-profile-builder)). What belongs to the
selected profile as a whole is the box **This profile** at the bottom of its General tab (hidden while the settings
search is in use): **Change application...** (program profiles) or **Change device...** (device profiles) opens
the same picker as "Add application..." / "Add device..." (running programs, capture the next focused program,
device list, manual entry) with the current match string filled in, and changes the program or device the
profile applies to while all its settings stay, for example when a program's name changed (the new profile
file is written first, then the old one is removed; the profile keeps its place in the order); **Move up** and
**Move down** change the order (below); **Export...** saves the profile as a `.conf` file (a copy of its profile
file; for Global the `[global]` keys the file holds); **Remove** deletes the profile (and its file) after asking
once (Cancel is the default button, the destructive button is called "Remove profile"), nothing asks about unsaved
changes; afterwards **Undo** under the list brings the profile back with all its settings and its place in
the order (rule below). Results and errors that appear in the status bar are also announced to screen readers. Global has no program or device and cannot be removed, it has only Export.
**Import profile...** takes such a `.conf` file: the header of the file decides the kind (`[device:...]` becomes a
device profile, `[app:...]` a program profile); the file is checked completely before anything changes, and a
Global file, a file with several profiles or with an invalid value is refused with a message. If a profile of that
name exists already you are asked once in words: "Replace the existing profile" or "Keep the existing profile"
(nothing is ever overwritten without that choice; the footer says what happened). The `PRIORITY` of the imported
file is not taken over: a new imported profile gets a `PRIORITY` below all of your own (10 below the lowest), so it
is applied first and never wins over one of yours; a replaced one keeps its place. `ENABLED`, `BACKEND`,
`TICK_HZ` and `INPUT` are dropped too (the dialog lists what was dropped). A very short name (4 characters or fewer,
it matches almost every program or device) and bindings that can do harm are shown in words and need your
confirmation before the profile is added: key chords with Ctrl, Alt or Super (however they are spaced, as the daemon
reads them), other chords with a key that is not a navigation key (for example Shift+Delete), plain Delete, Backspace,
Enter, Esc, F4 or a modifier key, mouse clicks as button action (also the old `CLICK_BUTTON`) and a `MODE` or
`mode:` binding that points to a Joystick mode of your own. The values are written in their canonical form (what the
editor would write), not as typed in the file. A profile that binds such things shows the same warning in its header
("Only keep profile files from sources you trust ..."). A file whose profile name contains control, zero-width,
bidirectional or BOM characters is refused. If the profiles change while the question is open (the configuration was
reloaded), nothing is imported and the footer asks you to import again. Names, file names and messages from files are
shown without control, zero-width and bidirectional characters. If a profile folder cannot be read, the list says "Could not read the
folder ..." and nothing is written until it can be read again. Joystick modes and button modes have their own Import.

If a profile file was changed by hand while the window had an edit of that profile, the file is not overwritten:
the profile is reloaded from the file, the footer says "Could not save: ..." and the same message stays in the
header of that profile ("The file ... was changed outside the settings window ..."). The same happens when a file
to be removed was changed.

### Order of the profiles

When two profiles of the same kind match the same device or program, the one applied later wins (device profiles are
always applied before program profiles). The order of a kind is the optional key `PRIORITY` (a whole number from
-1000 to 1000, default 0, lower is applied first); profiles with the same number are applied in the order of their
file names (the profiles of an old single file first, in file order). The list shows exactly this order, and the box
"This profile" says it in words: "Applied after "x", so it wins over "x" where both match. "y" is applied after it and
wins over it." **Move up** applies the profile earlier, **Move down** later. The buttons change the `PRIORITY` of the
two profiles and leave the gaps; only when the order would stay ambiguous (equal numbers, hand-written numbers) the
profiles of that kind are numbered again (10, 20, 30, ...). A profile you add is always applied last (its number is
10 above the highest one, also when all numbers are negative or 0); an imported profile is applied first (see
Import). When a number would leave the range -1000 to 1000, the kind is numbered again first.

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

### Take over and the profile builder

**Take over from ...** sits next to Clear on every motion and every button of the Motions and buttons tab of a profile
(Global, device, program) and in the inspector of the joystick mode editor. It asks for a source (any other profile, any
joystick mode; for a button also a button mode), says in words what would be copied and then copies it. It is a copy: the
effective assignment of the source becomes explicit keys of the target, later changes in the source do not follow. Push and
tilt are separate motions. A source that is off is copied as "cleared". For a motion the dialog has the check box **Also take
over the tuning** (off by default: only the action is copied); on, it also copies the effective sensitivity, dead zone,
direction, curve and smoothing of that motion as explicit keys, and the sentence in the dialog lists them. Profiles and
joystick modes use the same copy. A refused copy (a profile below its joystick mode
cannot beat the mode) says why and writes nothing. Like every edit it is saved by itself.

The list of actions of a motion offers every action for every motion, zoom for a tilt included; the words under the list say
what the choice does ("Zoom from this motion holds the zoom keys and sends the wheel ... flip the direction there if it
zooms the wrong way ..."), and a choice that cannot work in this profile is greyed out with the reason in its tooltip.
See [the recipes in configuration.md](configuration.md#recipe-use-the-tilt-as-zoom-in-one-program).

**New profile ...** opens the profile builder: a program or a device with its match name, what to start from (nothing, or the
settings an existing profile holds itself), a joystick mode, a button mode per button and single motions or buttons taken over
from other profiles. For the joystick mode and every button mode it is your choice between **follows the mode** (the profile
names it; a later change of the mode changes the profile) and **Make my own copy of this mode** (a file of its own in your
folder, named "<mode> (<profile>)"). A sentence under the form says what will be created; a name that exists is refused with
the reason next to the field; while the name is empty the line says "Enter the name of the program or device" and no summary is shown. New rows pick the next motion or button that is not used yet; **Add a button mode ...** is off (with the reason in the text) when there are no button modes; the lift is not offered yet. The dialog is sized from its content and the screen, and its Tab order follows the screen, also for added rows. A copy of a joystick or button mode gets a free name even for long profile names. The result is a normal profile, selected afterwards, editable on its Motions and buttons tab.

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
("Moved the old navigation/click button settings to the Motions and buttons tab"); the Motions and buttons tab keeps the list
of what was moved. A pair is moved all or nothing: if a button is above the button count of the
device or the profile already binds that button to something else, the old keys stay in the file
untouched and the Motions and buttons tab lists them as "Not moved: ...". Disabled old keys (older saves wrote
`NAV_BUTTON_ENABLED = 0` everywhere) are simply dropped. The daemon still understands the old keys.

## Tray menu

Status line, "Joystick mode" submenu (the Global joystick mode; every entry has its description as tooltip), "Enabled", "Settings...", "Open config file",
"Show log...", "Open joystick modes folder", "Release all keys", "Restart",
"Quit". "Release all keys" starts
`dofscroll --release-keys` as a separate process (the daemon binary next to the interface, else
`dofscroll` from `PATH`); the same command works in a terminal (see
[troubleshooting.md](troubleshooting.md#a-key-or-scrolling-gets-stuck)).

"Restart" releases held keys first (as "Release all keys"), then restarts the daemon and then the interface. The daemon:
on Linux `systemctl --user restart dofscroll.service` when the unit is active, on macOS
`launchctl kickstart -k gui/<uid>/io.github.dofscroll.daemon` when the LaunchAgent is loaded; on
Windows (and when the service manager does not manage the daemon) it stops the process (Windows:
`scripts\stop-daemon.ps1`) and starts the `dofscroll` binary next to the interface. The
interface is restarted under `dofscroll-ui.service` through
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
- **Removing the driver's axis settings (Windows).** The 3Dconnexion driver keeps one XML file per
  program in `%APPDATA%\3Dconnexion\3DxWare\Cfg\`. An axis that is switched on there (for example twist
  to the mouse wheel) makes that program receive every movement twice, from the driver and from
  dofscroll. The **3Dconnexion driver** box on the General page opens **Remove driver axis settings...**:
  a list of the programs with active driver axes (program, executable, which axis does what). Programs
  whose axes only pass the motion on to the program (3D navigation of CAD and 3D programs) are not
  ticked; everything else is. Tick as many programs as you want (click each box, or select rows with
  Ctrl or Shift and press Space; **Tick all** and **Tick none** set every box) and choose **Switch off the ticked programs**: the
  `<Enabled>` flag of every axis in those files is set to `false` (nothing else in the file changes),
  after the original was copied to `Cfg\dofscroll-backup\<date>-<time>\`. **Restore the last backup**
  puts those files back. Global.xml and the driver's installed defaults are not touched; a program
  without a file of its own keeps whatever the driver does for it. If a change does not take effect,
  restart the driver or sign out and in. Not available on macOS yet (the driver's settings location
  there is not verified). Unchecked on hardware.
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
LaunchAgent on macOS. If the interface is not installed, only the daemon is switched. The
[setup guide](#the-setup-guide) switches it on at the first start (the user can untick it). See
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
"Tasten-Modus").

## Languages

English is the source language; the German catalog (`po/de.po`, `dofscroll_de.qm`) is installed
with the interface.
