<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
## New mode

<!-- Use this template for a PR that only adds a joystick mode or a button mode (a plain file).
     Open it with ?template=new-mode.md. For code changes use the default template. -->

**Kind:** <!-- joystick mode (examples/modes/<id>.conf) or button mode (examples/buttons/<id>.conf) -->

**Id and name:** <!-- file name without .conf, [a-z0-9_-]{1,32}; NAME line -->

**What is it for?** <!-- program or workflow, one or two sentences. Ideas collected in #62 -->

**What does it do?** <!-- joystick mode: V_ACTION / H_ACTION / TWIST_ACTION and overrides in plain words;
                           button mode: the action and, for mode:next / mode:prev, the CYCLE list -->

## Checklist

- [ ] The file is in `examples/modes/` (joystick mode) or `examples/buttons/` (button mode) and is built into the program at build time; nothing else needs to be copied
- [ ] The file has the SPDX header, `NAME` and a short `DESCRIPTION` (shown in the interface; one line, plain words)
- [ ] The id is new and not reserved (`next`, `prev`, `normal`, `off`, `timeline`), and no existing built-in has the same behavior
- [ ] The response is analog where it makes sense: joystick modes put functions on the axes, proportional to the deflection (see the design principle in #62)
- [ ] It does not only re-implement a fixed driver mapping (a button that always sends the same key or click belongs in the vendor driver, see `docs/modes.md`)
- [ ] Safety: I read the file for key, keys, zoom and mode actions; modifier chords and anything that could be harmful are explained in `DESCRIPTION`
- [ ] `docs/modes.md` (built-in table) and `docs/wiki/Modes.md` list the new mode
- [ ] `cmake --build build --target check-fast` passes (it parses every embedded file without warnings and keeps ids unique)
- [ ] Tested: <!-- program and version, device, OS, session type, or "not tested on hardware" -->
