# uinput access for dofscroll

XTest cannot be used under Wayland. Instead, the uinput backend creates a virtual input
device via `/dev/uinput`. For this the user needs write access to that device, which by
default only root has.

## Installation

```sh
sudo cp packaging/70-dofscroll-uinput.rules /etc/udev/rules.d/   # from a package: /usr/share/dofscroll/udev/
sudo udevadm control --reload && sudo udevadm trigger
```

The rule sets `MODE="0600"` and `TAG+="uaccess"`: systemd-logind grants access via ACL
only to the user of the currently active local session at the seat. Afterwards log out and
back in once (or wait for `udevadm trigger` to finish). Pure SSH sessions and background
processes of other users get no access.

If `/dev/uinput` is missing: `sudo modprobe uinput` (permanently via
`/etc/modules-load.d/uinput.conf` containing `uinput`).

Verify: `ls -l /dev/uinput` shows `crw------- root root` plus an ACL (`+`); `getfacl
/dev/uinput` lists the logged-in user with `rw-`.

## Warning: do not add yourself to the `input` group

It used to be recommended to add the user with `usermod -aG input`. This is
**no longer necessary and not recommended**: the `input` group has read access to all
`/dev/input/event*` (typically `root:input 0660`). Any process of the user can then read
all keyboards system-wide (keylogging, including passwords in `sudo`, the lock screen or
the console), regardless of X11/Wayland isolation. If you are already in the group, you
should remove yourself (`sudo gpasswd -d "$USER" input`), unless something else needs it.
If a group is absolutely required (headless/SSH): create a dedicated group only for
`/dev/uinput`, not `input`.

## Note

If `/dev/uinput` is `0666` on a machine, that is a misconfiguration (any local account
could inject keystrokes into other sessions). Look for the cause in other udev rules under
`/etc/udev/rules.d` or `/usr/lib/udev/rules.d` and fix it. Under X11 the `x11` backend is
sufficient (the default with `BACKEND=auto`); `/dev/uinput` is then not needed at all.

## Note: other rules can override this rule

udev applies rule files in lexical order, later ones win. A custom rule such as
`/etc/udev/rules.d/99-uinput.rules` with `KERNEL=="uinput", MODE="0666"` makes
`/dev/uinput` writable for **all** local users and services and overrides the rule
`70-dofscroll-uinput.rules`. Check: `ls -l /dev/uinput` (must not be `crw-rw-rw-`)
and `grep -rl uinput /etc/udev/rules.d /usr/lib/udev/rules.d`. Under X11 uinput is not
needed at all (BACKEND=auto uses XTest); the rule is only needed for Wayland.
