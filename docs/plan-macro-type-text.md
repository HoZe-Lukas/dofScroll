<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Plan: macro step `paste:"text"` (issue #386, replaces decision D5 of `plan-macro-engine.md`)

Maintainer decision: the text is not typed as keys. When the step runs, the text is put on the clipboard and pasted with the
paste shortcut. No keyboard layout is read, so there are no layout, AltGr or dead-key cases, and every character works
(also `€` on a US keyboard). To keep the grammar simple, a few characters are refused instead of escaped.

## Grammar

    paste:"any text"

- The text is between double quotes and is taken as written: **no escape sequences**.
- **Refused at load** (with the position of the first offender): every control character (ESC, tab, newline, carriage return,
  NUL, DEL, C1 controls), the backslash `\`, the double quote `"`, `#` (the config readers cut a line at ` #`), and the Unicode characters that `displayText` /
  `utf8::sanitizeDisplay` treat as invisible or direction-changing (the same rule as for profile names). Invalid UTF-8 is refused.
- Length: 1 to 1024 bytes (proposal; a paste is one step, so it needs no relation to the 256 steps).
- Case is kept and blanks inside the quotes are kept. `,` and `|` are allowed inside the quotes.
- One `paste:` counts as **one step** against the 256 per part and takes a short fixed time budget (see below) against the
  10 minute bound.
- Risk text: pasted text is tier B. `macroRiskText` names it `paste "first 20 characters..."` (through `displayText`), the
  import question shows the same.

## What the code forces

1. `parseMacroSteps` lower-cases the whole text and `splitTokens` splits at blanks: the tokenizer becomes quote-aware and
   leaves a quoted text alone.
2. `parseMacroAction` splits the sides at the first `,` and `parseMacroSide` splits the parts at `|`: both skip quoted text.
   The config line reader (comments with `#`) and the `macro:` checks in `parseButtonAction` / `parseChannelAction` are checked
   with a text that holds `, |`. The canonical text (`macroStepText`) writes the quotes back.
3. The step keeps the text: `MacroStep::Kind::Paste`, `std::string text`. The file only holds the text.

## Interface

New in `Output` (`output.hpp`), default "not supported" so fakes and `output_mux.hpp` keep working:

    // Puts the text on the clipboard (UTF-8). Returns false and sets *why if this backend cannot.
    virtual bool setClipboardText(const std::string &utf8, std::string *why);
    // Restores what setClipboardText replaced, as far as the platform allows (text only).
    virtual void restoreClipboard();

`ModeRunner` runs `Kind::Paste` as: save the clipboard text, set the new text, tap the paste chord (`Ctrl+V`, `Cmd+V` on
macOS), wait about 150 ms (budget of the step), restore the clipboard. A modifier the user holds (Shift, Ctrl) is
handled like `tapChord` does. If `setClipboardText` fails the step logs `macro: paste: <why>` once and does nothing. A paste in a
`repeat` block is allowed and bounded by the same limits. The paste chord is Ctrl+V; a terminal that wants Ctrl+Shift+V is
handled by writing `+shift paste:"..." -shift` (the held Shift is kept by the runner).

## Per platform

| Platform | Clipboard | Notes |
|---|---|---|
| Linux X11 (`output_x11.cpp`) | The daemon owns the `CLIPBOARD` selection (`XSetSelectionOwner`) and answers `SelectionRequest` (`TARGETS`, `UTF8_STRING`, `STRING`) from the event loop it already has (`followKeymap` reads events in `flush`) until the paste is taken, a new owner appears or about 2 s pass. About 150 lines of Xlib; no new dependency | The old clipboard content is not readable without an `xclip`-like round trip; the restore is limited to text read through `XConvertSelection` (best effort, otherwise the clipboard holds the pasted text afterwards, documented). |
| Linux uinput / Wayland (`output_uinput.cpp`) | `wl-copy` through `exec_posix.hpp` if it is installed, otherwise "paste needs the x11 backend or wl-clipboard" | A first version can refuse; decided at review. |
| Windows (`win_output.cpp`) | `OpenClipboard`, `EmptyClipboard`, `SetClipboardData(CF_UNICODETEXT)`; the old text is read with `GetClipboardData` first | Short retry loop when another program holds the clipboard open. |
| macOS (`mac_output.cpp`, helper in a `.mm` like `mac_activewin.mm`) | `NSPasteboard generalPasteboard` (`clearContents`, `setString:forType:NSPasteboardTypeString`) | Paste chord is Cmd+V. |

## Work packages (one PR, three commits)

1. **Core and Linux.** Grammar, tokenizer, forbidden-character check, canonical text, risk text, `Output` interface,
   `ModeRunner` step, X11 selection owner, uinput/Wayland message. Tests: grammar (case and blanks kept, `, | #` inside,
   every forbidden class with its position, unterminated quote, length limits, text round trip), runner with a fake
   `Output` (order: save, set, chord, wait, restore; failure path; held Shift; abort during the wait restores the clipboard),
   the X11 selection answer as a pure function over request and stored text. Tests run with `env -u DISPLAY`.
2. **Windows.** Clipboard code and test through the existing fake; CI builds it, not run on hardware.
3. **macOS.** `.mm` helper; CI builds it, not run on hardware.

Every commit: `docs/modes.md` (grammar, forbidden characters, clipboard side effect, per-OS support), `docs/configuration.md`
if the grammar table lives there, `docs/security.md` (tier B, import question), `po/de.po` (informal "du") and `.pot`. The macro
editor is **not** changed (that is #387); check that it shows a `paste:` step as plain text without breaking.

## What only a human can verify

The paste really arrives in a text editor, a terminal (Ctrl+Shift+V case) and a browser field on X11, Windows and macOS; the
previous clipboard text is back afterwards; a text with `€ ö` pastes correctly on every layout; the macro refuses a text with
`\`, `"` or ESC at load with a position. Nothing of this is claimed as tested by the unit tests.

## Open decisions

- X11 selection owner in the daemon (proposed, no dependency) or `xclip` as external helper.
- Wayland: refuse in the first version (proposed) or `wl-copy`.
- Restore the clipboard (proposed, text only) or leave the pasted text there.
