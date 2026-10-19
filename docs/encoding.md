<!-- SPDX-License-Identifier: GPL-3.0-or-later -->
<!-- SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka -->
# Text and encoding

All text in dofscroll is UTF-8: configuration files, mode files, device description files,
status and log files, command lines, and everything inside a path (user names, directories,
file names). Nothing is converted through the system code page or the locale.

## Rules

| Topic | Rule |
|---|---|
| Encoding | UTF-8 only. Internal strings, files and OS calls use UTF-8; Windows converts to UTF-16 at the OS boundary only. |
| BOM | A leading UTF-8 BOM (EF BB BF) is accepted when reading and never written. |
| Line ends | LF is written. LF and CRLF are accepted when reading. |
| Allowed characters | Anything valid in UTF-8: spaces, umlauts, accents, CJK, emoji, combining marks, right-to-left text. In names and values `#` `;` `=` `%` `&` `(` `)` `[` `]` and quotes work. `#` starts a comment only at the start of a line or after whitespace in a value. |
| Invalid UTF-8 in a file | A warning with `file:line` is issued and the rest of the file is still used. Configuration section names are kept byte for byte, so saving never corrupts or drops them. Display text (mode name, description, log lines) shows an invalid byte as U+FFFD. |
| Length limits | Counted in bytes. A text that is cut is cut on a character boundary, never inside a multi-byte sequence. A line over the limit is ignored as a whole, with a warning. |
| Case-insensitive matching | Section names (`[app:...]`, `[device:...]`), profile patterns and keys fold ASCII letters only. `Code` matches `CODE`; an umlaut name does not match its upper-case form (U+00FC versus U+00DC). Other characters are compared byte for byte. |
| Normalization | Text is not normalized. An accented letter as one code point (NFC) and as a base letter plus a combining accent (NFD) are different. The macOS providers (running app, active window) deliver NFC; write patterns in NFC. |
| Log and status files | Control characters and invalid bytes become `?`, valid UTF-8 is kept. |
| Scripts | PowerShell scripts are ASCII only (Windows PowerShell 5.1 reads BOM-less files as ANSI) and use `-LiteralPath`. Other tracked text files are UTF-8 without BOM (`scripts/check-utf8.sh`). |
| Paths in generated files | systemd units quote the executable and double `%` and `$`; LaunchAgent plists XML-escape; the Windows Run value is a quoted path. |

## Known limits

- Right-to-left, bidirectional control and look-alike (homoglyph) characters are valid text and
  are shown as they are; see `docs/security.md`.
- Windows cannot open a path component that ends with a dot or a space; the file layer refuses
  such paths instead of opening a different file.
- A section name cannot contain a line break or other control characters.
