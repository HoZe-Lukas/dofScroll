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

All device profiles are applied before all app profiles, each in the order of their `PRIORITY` key (integer, default 0, ascending; ties by file name; in the legacy single file the order of the sections), so a later matching profile wins. The migration from the single file assigns 10, 20, 30 ... in the old order, so nothing changes; the profile list shows the order and has "Move up" and "Move down". Details: [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md#order-of-overlapping-profiles). The joystick mode is chosen by `MODE` from the resolved profile (global, device or app). An app section can select a joystick mode and still has the last word over that joystick mode's overrides.

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

On the Motions and buttons tab of the program profile every motion and every button can be changed on its own, and each has three states: inherited, set here, cleared here. Select the twist and press **Clear**: the profile gets `TWIST_ACTION = none` and nothing else, so the program keeps the normal mapping without zoom. The list "What this motion does in this profile" gives a motion another action (`TWIST_ACTION = keys:ctrl+minus,ctrl+plus`), **Switch on** brings the action back, **Use the inherited setting** removes the key. Push and tilt are separate motions (`V_PUSH_ACTION`, `V_TILT_ACTION`, `H_PUSH_ACTION`, `H_TILT_ACTION`). The keys are those of the joystick mode files; they work in `[global]`, `[device:*]` and `[app:*]`, the program profile wins over the joystick mode, Global and device profiles sit below it. For a button choose another action in its row, **Clear** (`BUTTON_<n> = none`, an explicit unbind that overrides the device profile and the joystick mode of a program profile) or **Inherit**; the row says in words where the binding comes from. A version of dofscroll that does not know the new keys skips those lines with a warning and applies the rest of the profile. Recipes with the file lines: [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md).

### Take over from another profile, any action on any motion, build a profile

**Take over from ...** (next to Clear, on every motion and button of the Motions and buttons tab and in the joystick mode editor) copies what a motion or a button does in another profile, a joystick mode or (for buttons) a button mode into this profile. It is a copy of the effective assignment, written as explicit keys; later changes in the source do not follow. Push and tilt are separate motions. Only the action is copied, not the tuning of the channel. A motion that is off in the source is copied as cleared.

Any motion can get any action, also **zoom** from a tilt (`V_TILT_ACTION = zoom`): the zoom keys are held and the wheel is sent, with the sensitivity and direction of the tilt's own channel (flip the direction if it zooms the wrong way); while it zooms, the other wheel motions pause. The interface explains this in words under the list of actions and greys out what cannot work in a profile (a profile below its joystick mode cannot change what the mode sets).

**New profile ...** (Profiles page, below the list) builds a profile from existing parts: program or device and its name, what to start from, a joystick mode, a button mode per button, and single motions or buttons taken over from other profiles. Per mode you choose **follows the mode** (the profile names it, `MODE = photo` or `BUTTON_3 = buttonmode:next-mode`, so a later change of the mode changes the profile) or **Make my own copy of this mode** (a file of its own in your folder). The result is a normal profile, editable afterwards. Recipes with the file lines: [docs/configuration.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/configuration.md).

## Finding the program name

On X11, run `xprop WM_CLASS` and click the window. The daemon logs every profile switch (`journalctl --user -u dofscroll.service`). On Windows the key is the lower-cased executable name (for example `firefox.exe`); on macOS the bundle identifier or app name. Both are unverified on hardware.

## Limits

- Program profiles need an active-window query: always on X11, on Wayland only with `kdotool` (KDE). Otherwise only `[global]` and `[device:*]` apply.
- With two SpaceMice connected, `spacenavd` reports only one device, so `[device:*]` applies to both. With direct HID access (Windows, macOS) every device has its own profile and joystick mode, one device per model, so a Pro next to a Compact but not two Compacts (see [docs/multi-device.md](https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/multi-device.md)); this is covered by tests but unverified on hardware.
- 3D programs that read the device directly get doubled input; switch dofscroll off for them with `ENABLED = 0` or `MODE = off`.

See also [Configuration reference](Configuration-reference), [Joystick modes](Modes).
