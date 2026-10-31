<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Profiles for programs and devices

Design principle: dofscroll complements the vendor driver instead of duplicating it, see [Buttons](Buttons).

A profile is one file that overrides only the keys it lists: `devices/<name>.conf` holds one `[device:<text>]` section, `apps/<name>.conf` one `[app:<text>]` section; `dofscroll.conf` holds `[global]`. The header inside the file names the device or program, the file name is only a label, and every profile exists once (a second file with the same header is ignored with a warning). Older versions kept the profiles as sections in `dofscroll.conf`; that is still read, a file wins over a section of the same name. Full text: [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md).

| Section (file) | Applies when |
|---|---|
| `[global]` (`dofscroll.conf`) | Always |
| `[device:<text>]` (`devices/<name>.conf`) | The device name from `spacenavd` contains `<text>` (case-insensitive substring) |
| `[app:<text>]` (`apps/<name>.conf`) | The active window's class contains `<text>` (case-insensitive substring) |

## Precedence

```
[global]  <  matching [device:*]  <  active joystick mode's overrides  <  matching [app:*]
```

All device profiles are applied before all app profiles, each in the order of their `PRIORITY` key (integer, default 0, ascending; ties by file name; in the legacy single file the order of the sections), so a later matching profile wins. The migration from the single file assigns 10, 20, 30 ... in the old order, so nothing changes; the profile list shows the order and has "Move up" and "Move down". If you edit a profile that is still in an old single file, the interface keeps this order when the profile moves into its own file. Details: [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md#order-of-overlapping-profiles). The joystick mode is chosen by `MODE` from the resolved profile (global, device or app). An app section can select a joystick mode and still has the last word over that joystick mode's overrides.

## Example

The sections below are shown together; each of the last four is its own file (`devices/spacemouse-pro.conf`, `apps/firefox.conf`, `apps/blender.conf`, `apps/resolve.conf`), see [examples/split](https://github.com/HoZe-Lukas/dofScroll/tree/main/examples/split).

```
[global]
SCROLL_V_SENS = 5

[device:SpaceMouse Pro]
SCROLL_V_SENS = 8

[app:firefox]
ZOOM_SENS = 12
SCROLL_V_CURVE = 0:0 200:1 350:3

[app:blender]
ENABLED = 0                # 3D programs read the device themselves

[app:resolve]
MODE = timeline
```

## Editing a profile in the interface

The editor of the selected profile on the Profiles page has the tabs General (joystick mode, output switch), Channels (scroll, zoom key, zoom detection, curve, "Invert direction of" per motion; per row the value, where it comes from and Reset), Motions and buttons (the picture of the cap for this profile; the row of every button is in the inspector next to it) and Overrides. Global has the same tabs: everything can be set in Global like in any other profile. The header says which profile is edited and that everything not changed in a device or program profile is inherited from Global; a value of Global that profiles override says "N profiles override this". There is no Save button: every change is saved by itself and an invalid value is not written. Two search boxes filter the profile list (by name) and the settings of the selected profile (by label or key). "Export..." writes the selected profile as a `.conf` file, which is a valid profile file: sharing a profile is copying a file into `devices/` or `apps/` (or "Import...").

### Inheritance

`[global]` is the base; a `[device:*]` or `[app:*]` section holds only what it changes. Precedence: Global < device < joystick mode overrides < program. `MODE`, every channel key and every `BUTTON_<n>` can be set in `[global]` too. Buttons are inherited per button (`BUTTON_<n> = none` in a profile switches an inherited button off there). See [Configuration reference](Configuration-reference). Details: [docs/ui.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/ui.md).

### Zoom off for one program, other actions, other buttons

On the Motions and buttons tab of the program profile every motion and every button can be changed on its own, and each has three states: inherited, set here, cleared here. Select the twist and press **Clear**: the profile gets `TWIST_ACTION = none` and nothing else, so the program keeps the normal mapping without zoom. The list "What this motion does in this profile" gives a motion another action (`TWIST_ACTION = keys:ctrl+minus,ctrl+plus`), **Switch on** brings the action back, **Use the inherited setting** removes the key. Push and tilt are separate motions (`V_PUSH_ACTION`, `V_TILT_ACTION`, `H_PUSH_ACTION`, `H_TILT_ACTION`), also for their tuning: the sensitivity, dead zone, direction, curve and smoothing you set for the vertical tilt are written as `SCROLL_V_TILT_*` and never change the vertical push (and the other way round; the same horizontally). A value you do not set follows the whole channel (`SCROLL_V_SENS`, ...). The first change splits the channel for you without changing what any motion does. The keys are those of the joystick mode files; they work in `[global]`, `[device:*]` and `[app:*]`, the program profile wins over the joystick mode, Global and device profiles sit below it. For a button choose another action in its row, **Clear** (`BUTTON_<n> = none`, an explicit unbind that overrides the device profile and the joystick mode of a program profile) or **Inherit**; the row says in words where the binding comes from. A version of dofscroll that does not know the new keys skips those lines with a warning and applies the rest of the profile. Recipes with the file lines: [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md).

### Take over from another profile, any action on any motion, build a profile

**Take over from ...** (next to Clear, on every motion and button of the Motions and buttons tab and in the joystick mode editor) copies what a motion or a button does in another profile, a joystick mode or (for buttons) a button mode into this profile. It is a copy of the effective assignment, written as explicit keys; later changes in the source do not follow. Push and tilt are separate motions. By default only the action is copied; the check box **Also take over the tuning** also copies sensitivity, dead zone, direction, curve, smoothing and the key engine keys (lowest and highest rate, trigger level) of the motion. A motion that is off in the source is copied as cleared.

Any motion can get any action, also **zoom** from a tilt (`V_TILT_ACTION = zoom`): the zoom keys are held and the wheel is sent, with the sensitivity and direction of the tilt's own channel (flip the direction if it zooms the wrong way); while it zooms, the other wheel motions pause. The interface explains this in words under the list of actions and greys out what cannot work in a profile (a profile below its joystick mode cannot change what the mode sets).

**New profile ...** (Profiles page, below the list) builds a profile from existing parts: program or device and its name, what to start from, a joystick mode, a button mode per button, and single motions or buttons taken over from other profiles. Per mode you choose **follows the mode** (the profile names it, `MODE = photo` or `BUTTON_3 = buttonmode:next-mode`, so a later change of the mode changes the profile) or **Make my own copy of this mode** (a file of its own in your folder). The result is a normal profile, editable afterwards. Recipes with the file lines: [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md).

### Keys, clicks and holds on a motion: analog or digital

Every motion (vertical and horizontal push and tilt, twist, lift; both directions) can press a key or a chord per direction, in a profile as well as in a joystick mode. In the editor the check box **Analog** decides how: checked, the key repeats faster the further you push (`keys:<neg>,<pos>`); unchecked, it is pressed once per push, like a click (`step:<neg>,<pos>`). Three more fields tune it: the lowest and the highest rate (presses per second) and the trigger level. A side can be `none`, and the mouse is available too: **Mouse click per push** (`step:click,none`, also `rightclick`, `middleclick`) and **Hold a key or button while pushed** (`hold:click,none`, `hold:ctrl,none`), which holds until you ease back to half of the trigger level. Only keys that are safe to repeat can be analog (arrows, page up and down, home, end, tab, volume, media next and previous, plus and minus, ctrl or shift with a navigation key); letters, Enter, Delete, Esc, Alt and Super chords, mouse clicks and holds are digital only. In the file:

```
[app:browser]
H_TILT_ACTION = keys:ctrl+pageup,ctrl+pagedown   # tabs, faster with more tilt
SCROLL_H_TILT_KEY_RATE_MAX = 8
V_TILT_ACTION = step:pageup,pagedown             # one page per push
```

The keys and their limits are in [Joystick modes](Modes) and in [docs/modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md#the-key-engine-analog-and-digital). How the thresholds feel on a real device is not verified.

### New function ... and macros

The drop-down of every motion and every button on the **Motions and buttons** tab ends with **New function ...**. The window composes one function from a kind: a key or key combination, a mouse click, a hold, scroll or zoom, moving the pointer, or a **macro**. Keys are chosen with the check boxes Ctrl, Shift, Alt and Super key plus a key list or **Press a key ...**; there is no text field for a key. There is no Save button: as soon as every field is valid the function is assigned and a line says in words where it applies; a field that is not valid is explained next to it. What a motion or button cannot do is greyed with the reason next to it.

The macro editor has **Record** (it records only inside this window while the recording area has the keyboard focus; there is no global key logger), a check box **Also record the times between the keys** (on: the measured gaps, off: 30 ms), and steps that can be edited, deleted, moved or added by hand (at most 256, a pause counts as a step). The list **Own functions of this profile** shows every function the profile holds, with the motions and buttons that use it; **Edit**, **Delete** and **Save as joystick mode ...** work from there. A macro is digital: it runs once per push or button press. The syntax is in [Joystick modes](Writing-modes#3-channel-actions) and [docs/modes.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/modes.md#macros). Not verified: the recorder and macros in real programs.

The same window offers **Move the pointer** (`pointer_x`, `pointer_y`; the speed is `MOUSE_SPEED`).

### Profile files: import, export, order

Every profile is a file, so sharing one is copying it: **Export...** on the profile (General tab, box **This profile**) writes the file, **Import profile...** (below the list) reads one after checking it completely. The import drops `PRIORITY`, `ENABLED`, `BACKEND`, `TICK_HZ` and `INPUT`, asks before accepting a very short name or bindings that can do harm (key chords with ctrl, alt or super, Delete, clicks, macros with such keys, a joystick mode or button mode of your own) and lists those bindings in the question (the first six), and applies an imported profile **before** yours, so yours win where both set the same key. **Move up** and **Move down** change the order of overlapping profiles of one kind (they change `PRIORITY`). A file that cannot be used is listed as "(defective)" with the reason and is never overwritten while the problem stands; **Remove** can be undone right after.

## Finding the program name

On X11, run `xprop WM_CLASS` and click the window. The daemon logs every profile switch (`journalctl --user -u dofscroll.service`). On Windows the key is the lower-cased executable name (for example `firefox.exe`); on macOS the bundle identifier or app name. Both are unverified on hardware.

## Limits

- Program profiles need an active-window query: always on X11, on Wayland only with `kdotool` (KDE). Otherwise only `[global]` and `[device:*]` apply.
- With two SpaceMice connected, `spacenavd` reports only one device, so `[device:*]` applies to both. With direct HID access (Windows, macOS) every device has its own profile and joystick mode, one device per model, so a Pro next to a Compact but not two Compacts (see [docs/multi-device.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/multi-device.md)); this is covered by tests but unverified on hardware.
- 3D programs that read the device directly get doubled input; switch dofscroll off for them with `ENABLED = 0` or `MODE = off`.

See also [Configuration reference](Configuration-reference), [Joystick modes](Modes).
