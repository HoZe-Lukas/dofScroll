# Plan: a better macro engine

Status: decisions made (see "Decisions"), implementation in waves. Follows stage 7b of `plan-motion-functions.md`.

## Goals

1. **Simpler to use.** One place to build a function, one vocabulary, and the UI says which stored form it chose and why.
2. **More capable.** Tap, repeat, mouse, mode switches, typed text, a "run only while held" switch.
3. **Cheap at run time.** No copies per trigger, no re-parsing on edges.
4. **Compatible.** Old files keep their meaning. An old daemon ignores a line it does not know, with its usual warning.

## Findings that drive the plan

Engine (`src/macro.*`, `src/mode_runner.cpp`):

- Steps are only Down, Up and Wait. A key press is two steps, so typing N letters costs about 2N of the 256 steps.
- A running macro always runs to completion. The release of the button never stops it. The release part is dropped when the press part is still running (`requestMacro` refuses while one is active or pending). Nothing tells the user.
- One macro at a time, further triggers are ignored silently.
- `requestMacro` copies the step vector on every trigger; `ButtonBinding` already holds a `shared_ptr`.
- Pauses are rounded up to the next tick (up to 4 ms at 250 Hz); the remainder carries over.
- To verify: the action text cap. The docs say 32 KiB, the 7a table in the old plan says 256 bytes (`kMaxActionTextBytes`, `modes.cpp`). Typed text and repeat make this matter.

UI:

- A macro is only reachable through "New function...". There is no Macro entry in the button list, the motion list, the mode editor or the action tiles.
- The release part exists in the daemon, but the dialog locks such a macro (`macroNotEditable`) and says "edit the configuration file".
- Three surfaces name the same stored text differently ("Keys" tile, "Keys (one key per direction)" with an Analog box, "Keys" and "Step" in the mode editor).
- The same effect has several paths: a tap on a motion is `step:`, or a macro `+a -a`; a chord on a button is "Press key" or New function, both writing `key:<chord>`. Nothing compares `key:` and `macro:`.
- The recorder only appends, records keys only, and cannot record mouse or mode changes.
- An invalid pause value in the editor is ignored without a message.
- A button macro shows as the chip "Macro" in every case; the steps are only in the tooltip.
- "Own functions" are not named objects, just action text that several keys share. No duplicate, no reuse on another button except through a button mode.

## Design decisions to settle first (stage 0)

These shape the grammar, so they are decided before code:

| # | Question | Proposal |
|---|---|---|
| D1 | Tap step grammar | `tap:<chord>` (one tick: down, then up in reverse order). `+a -a` stays valid. |
| D2 | Repeat grammar | A count on a block: `repeat:<n>` ... `end`, no nesting, `n` 1 to 1000. Total expanded steps and total time are bounded (see D8). |
| D3 | Mouse steps | `click`, `rightclick`, `middleclick` as taps; `+click` and `-click` for hold, mirroring keys. Mouse buttons held by a macro are released on abort like keys. |
| D4 | Mode steps | **Dropped.** A macro cannot switch modes; the parser has no such step. |
| D5 | Typed text | `type:"abc"` with quotes and `\"`, `\\` escapes, using the **keyboard layout of the machine** (X11 keymap on Linux; the platform's layout API elsewhere, with a clear fallback). A character with no key is an error with its position. |
| D6 | "Only while held" switch | See below. Stored as a prefix word on the side, default off to keep current behavior. |
| D7 | Second trigger while running | Keep "ignore" as the default. Add `restart` later only if there is demand. Show the behavior in the UI. |
| D8 | Bounds | Keep 256 steps per part **after** expansion, and add a total time bound (proposal 10 minutes) so a repeat cannot run unattended for hours. |

### D6: "run only while the button is held"

The user asked for a switch: on means the macro runs only as long as the trigger is held; off means the whole macro always runs (today's behavior).

- **On:** when the button is released (a motion: back at half of the trigger), the running macro is aborted at once and everything it holds is released (`abortMacro`, but only the macro's part, not other state). The release part, if any, then runs.
- **Off (default):** unchanged, the macro runs to the end. A release part that arrives while the press part still runs is still dropped, and the UI says so.
- Syntax: `macro:while_held <steps>` (the word is a prefix of the side, so it can differ per direction and per press/release part is not needed: it only affects the press part).
- Combined with repeat, "while held" gives "repeat as long as the button is held": a `repeat` without a count, only valid together with `while_held` (bounded by the time limit of D8 and by the release).
- Tests: stuck keys after abort, abort in a pause, abort between a down and its up, motion hysteresis, the release part after an abort, interaction with `mode:` as last step.

## Stages

Each stage is one PR, keeps `check-fast` green, updates the matching docs, `po/*.po` and screenshots in the same change.

### Stage 1: UI clarity without a format change

- Add "Macro..." to the button drop-down, the motion drop-down and the mode editor, opening the same dialog (today it is only under "New function...").
- One vocabulary across the three surfaces; the labels and the stored form in one table in `docs/ui.md`.
- A one-line "what you get" under the Action field, with the stored form named in words, and a hint where a simpler kind does the same ("a single key or combination does not need a macro: use Press key").
- Edit the release part in the dialog (lift `macroNotEditable`): a second step list per direction, composed with ` | `.
- Show why a release part may not run (press part still running) and that further triggers are ignored while a macro runs.
- Visible message for an invalid pause value.
- Chips and summaries show the first steps ("Macro: Ctrl+C, 30 ms, ...") instead of "Macro".
- Recorder can insert at the cursor, not only append.

### Stage 2: engine without a grammar change

- Share the step list instead of copying (`shared_ptr<const MacroSteps>` in the runner, no vector copy per trigger).
- Check `edgeBindings` for re-parsing on a button edge; cache the parsed bindings per profile load if it does.
- Optionally count pauses with sub-tick accuracy by running the macro at the first tick after the due time; measure first, only do it if it is visible.
- Benchmark in a test (steps per tick, copy cost) so a regression shows.

### Stage 3: new steps

- `tap:<chord>`; recorder emits it for a quick down and up pair; the risk text counts it like a Down.
- `repeat:<n>` ... `end` (D2, D8).
- Mouse steps (D3), including release on abort.
- Order inside the stage: tap, mouse, repeat, because repeat needs the bounds.
- Old daemons ignore these lines with a warning; the docs say so, and a new macro written by the UI uses the old form whenever it can (`+a -a` has no reason to become `tap:a` automatically unless the user chose Tap).

### Stage 4: "only while held" (D6)

- Parser, serializer, `operator==`, runner abort path, UI check box per macro editor, summary line, docs, tests as listed above.
- Includes the "repeat while held" form.

### Stage 5: typed text

- `type:"..."` (D5) with the machine's layout, a text field step in the editor and an optional "merge typed letters" in the recorder.
- Risk text: typed text counts as tier B.
- No mode step (D4).

### Stage 6: naming and reuse (the old stage 7e)

- Named functions: `FUNC_<name>` table in the config, used by `BUTTON_<n> = func:<name>` and the motion keys. Edit once, used everywhere.
- Own-functions list gets create and duplicate; "used by ..." stays.
- This is the largest change to the config format and the one with the most migration questions; decide after stages 1 to 5 are used.

## Order and dependencies

1 and 2 are independent and safe, do them first. 3 and 4 need D1 to D3 and D6 settled. 5 needs D4 and D5. 6 is last.

## What needs a human

Timing, recording, the abort on release, typed text on a real keyboard layout and the tray and dialog behavior need real hardware, a live X11 or Wayland session and `spacenavd`. Tests cover the parser, the model and the runner with a fake output; they do not prove behavior in a real program.

## Decisions (maintainer)

- `repeat:<n>` ... `end` blocks are accepted: a macro can have parts that repeat and parts that do not. The block makes clear what repeats.
- `type:` uses the keyboard layout of the machine.
- No mode switch inside a macro.
- Stage 6 (named functions) stays in this plan.
- The action text limit is raised so nobody perceives it as a limit (value chosen and documented in stage 2; the step limit stays bounded by D8).
