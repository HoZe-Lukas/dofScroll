# Sicherheit: Review, systemd-Härtung, Commit-Hook, Repository-Einstellungen

Stand: 2026-10-12, Quellen unter `src/`, `packaging/`, `systemd/`. Der Review selbst war rein
dokumentierend; die Korrekturen unter „Status“ kamen danach. Zeilennummern wurden entfernt
(Verweise nennen Datei und Funktion); Befunde sind mit dem Status „behoben“ markiert, wo
der Code bzw. die Regel inzwischen angepasst wurde (Stand der Korrekturen: 2026-10-15).

Inhalt:

1. Bedrohungsmodell
2. Befunde (mit Datei:Zeile, Schweregrad, Empfehlung)
3. systemd-Härtung der User-Units (was gesetzt ist, was bewusst nicht, wie geprüft)
4. Pre-commit-Hook (`scripts/pre-commit`, `scripts/install-hooks.sh`)
5. Repository-Einstellungen (Hinweis: stammt aus der GitLab-Zeit, auf GitHub entsprechend „Branch protection rules“ mit Pflicht-Checks und Pflicht-Review für `main` einrichten)

---

## 1. Bedrohungsmodell

**Was geschützt werden soll:** die grafische Sitzung des Nutzers (Tastatur-/Maus-Eingaben,
also *Integrität* der Eingaben und *Vertraulichkeit* dessen, was getippt wird), die
Verfügbarkeit des Desktops (kein „hängendes Ctrl“, keine Event-Fluten) und die
Konfigurationsdatei.

**Komponenten und Vertrauensgrenzen:**

| Komponente | Läuft als | Spricht mit | Eingaben, die von außen kommen |
|---|---|---|---|
| `dofscroll` (Daemon) | Nutzer, systemd-User-Unit | `spacenavd` über Unix-Socket `/run/spnav.sock` (root, `0777`), X-Server (XTest) oder `/dev/uinput`, optional `kdotool` (per `popen`) | Achswerte/Tasten von `spacenavd`, `~/.config/dofscroll/dofscroll.conf` (Fallback: Legacy-Pfade `~/.config/spnav-scroll/spnav-scroll.conf`, `~/.config/spnav-scroll.conf`), `WM_CLASS` fremder X-Clients, Umgebung (`XDG_*`, `HOME`, `PATH`, `DISPLAY`), SIGHUP |
| `dofscroll-tray` | Nutzer, systemd-User-Unit | GTK/X11, DBus (AppIndicator), startet `systemctl --user` und `xdg-open` | GUI-Eingaben, Config-Datei, Umgebung |
| `spacenavd` | root (Systemdienst) | USB-HID-Gerät | Gerät (inkl. Gerätename aus USB-Deskriptor) |
| udev-Regel `packaging/70-dofscroll-uinput.rules` | – | – | legt die Rechte von `/dev/uinput` fest |

**Angreifer, die betrachtet wurden:**

- **A1 – anderer lokaler Nutzer / Systemdienst** (anderer Login, Dienstkonto wie `ollama`,
  Container-Prozess). Darf die Sitzung des Nutzers weder mitlesen noch fernsteuern.
  Relevant: Rechte auf `/dev/uinput`, `/dev/input/event*`, `/run/spnav.sock`, Config-Pfad.
- **A2 – Prozess desselben Nutzers mit eingeschränkten Rechten** (z.B. kompromittiertes
  Programm, Browser-Exploit außerhalb einer Sandbox). Kann ohnehin die Config schreiben, den
  Daemon per SIGHUP neu laden und – unter X11 – selbst per XTest Eingaben erzeugen. Hier zählt
  vor allem, dass der Daemon durch eine manipulierte Config **nicht** zu undefiniertem
  Verhalten oder zum Lahmlegen der Sitzung gebracht werden kann und dass die Dienste durch
  systemd so eingeschränkt sind, dass ein Fehler im Daemon nicht zu mehr führt.
- **A3 – Angreifer mit physischem Zugang** (präpariertes USB-Gerät): kontrolliert
  Gerätenamen und Achswerte, die über `spacenavd` ankommen.
- **A4 – manipulierte Config von außen** (die README empfiehlt, die Datei beim Umzug per
  `scp` zu übernehmen; Profile werden ggf. geteilt).

**Einordnung von XTest/uinput:** Unter X11 kann *jeder* X-Client per XTest beliebige
Eingaben erzeugen – der Daemon schafft dort keine neue Fähigkeit. `/dev/uinput` dagegen
wirkt **unterhalb** des Display-Servers (auch auf Text-Konsolen, im Login-Manager und in
fremden Sitzungen am selben Seat). Wer schreibend auf `/dev/uinput` zugreifen kann, kann
damit Tastatureingaben in jede dort aktive Sitzung einspeisen – das ist der kritischste
Punkt des ganzen Aufbaus (siehe S1).

**SIGHUP:** löst nur einen Config-Reload aus (Flag im Handler, Arbeit im Haupt-Thread).
Senden können es nur Prozesse desselben Nutzers bzw. der User-Manager – unkritisch, zumal
der Daemon die Datei ohnehin jede Sekunde auf Änderungen prüft.

---

## 2. Befunde

Schweregrade: **Hoch** = ermöglicht Angreifer A1 Mitlesen/Einspeisen von Eingaben;
**Mittel** = undefiniertes Verhalten oder Lahmlegen der Sitzung durch erreichbare Eingaben;
**Niedrig** = nur unter Zusatzbedingungen ausnutzbar bzw. Robustheit; **Info** = kein
Handlungsbedarf, zur Einordnung.

### Übersicht

| ID | Schwere | Thema | Ort |
|---|---|---|---|
| S1 | **Hoch** (Regel/README behoben; `0666`-Rechner offen) | `/dev/uinput` world-writable auf dem Entwicklungsrechner; Empfehlung „Nutzer in Gruppe `input`“ erlaubt Keylogging | `packaging/70-dofscroll-uinput.rules`, `packaging/README-uinput.md` |
| S2 | **Mittel** (behoben) | Unbegrenzte Zahlen in Config → Inf/Überlauf → UB bei `double`→`int`, Event-Fluten | `src/config.cpp`, `src/dsp.cpp`, `src/output_x11.cpp` |
| S3 | **Mittel** (behoben) | „Hängende“ Strg-Taste: blockierende Aufrufe im Tick-Thread, Absturz/Kill unter X11, verworfene Key-Up-Events bei uinput | `src/activewin.cpp`, `src/dofscroll.cpp`, `src/output_uinput.cpp` |
| S4 | Niedrig | `loadProfiles`: keine Grenzen für Dateigröße, Zeilenlänge, Sektionen; FIFO/Gerätedatei blockiert den Daemon | `src/config.cpp` |
| S5 | Niedrig | Speichern: vorhersagbarer Temp-Name ohne `O_EXCL`/`O_NOFOLLOW`, kein `fsync`, Dateirechte per umask, Lost Update (TOCTOU) | `src/config.cpp`, `src/dofscroll_tray.cpp` |
| S6 | Niedrig (behoben) | `popen("kdotool …")`: Shell + PATH-Suche, blockiert den Tick-Thread | `src/activewin.cpp` |
| S12 | Niedrig (behoben) | `/dev/uinput`-fd ohne `O_CLOEXEC`: von `kdotool`-Kindprozess erbbar | `src/output_uinput.cpp` (`init`) |
| S7 | Niedrig | Shutdown-Rennen mit detachtem Reader-Thread; Signal-Handler erst spät installiert | `src/dofscroll.cpp` |
| S8 | Niedrig | Binaries unter `/usr/local/bin` sind root-eigene Symlinks in ein nutzerbeschreibbares Home | `readme.md` (Install), `/usr/local/bin/dofscroll*` |
| S9 | Niedrig | `spacenavd`-Socket `0777`: fremde Nutzer können Bewegungen mitlesen/Gerät umkonfigurieren | `/run/spnav.sock` (Systemkonfiguration) |
| S10 | Niedrig | Tray: PATH-Suche für `systemctl`/`xdg-open`; Editor erbt die Sandbox | `src/dofscroll_tray.cpp` |
| S11 | Niedrig | Config-Pfad aus Umgebung ohne Prüfung (relatives `XDG_CONFIG_HOME`, fehlendes `HOME`) | `src/config.cpp` |
| I1–I6 | Info | Positives und Einordnung (Reader-Thread-Atomics, `findIconDir`, Puffer, WM_CLASS-Spoofing, Log-Injektion) | s.u. |

### S1 (Hoch) – Rechte auf `/dev/uinput` und Gruppe `input`

**Status (2026-10-15):** Befund b und c **behoben** – `packaging/70-dofscroll-uinput.rules`
lautet jetzt `KERNEL=="uinput", MODE="0600", OPTIONS+="static_node=uinput", TAG+="uaccess"`
(kein `GROUP="input"` mehr; Zugriff nur per ACL für den Nutzer der aktiven lokalen Sitzung),
`packaging/README-uinput.md` entfernt den `usermod -aG input`-Schritt und erklärt das
Keylogging-Risiko der Gruppe `input`; die Fehlermeldung in `UinputOutput::init` verweist nicht
mehr auf die Gruppe. Befund a (der `0666`-Zustand auf dem Entwicklungsrechner) ist eine
Systemkonfiguration außerhalb des Repos und **bleibt manuell zu prüfen**.

- **Befund a:** `packaging/README-uinput.md` hält fest, dass `/dev/uinput` auf dem
  Entwicklungsrechner `0666` ist; am 2026-10-12 verifiziert: `crw-rw-rw- root input
  /dev/uinput`. Damit kann **jeder lokale Account und jeder Systemdienst** (A1) ein
  virtuelles Keyboard anlegen und Tastendrücke in die gerade aktive Sitzung, in eine
  root-Konsole oder in den Sperrbildschirm einspeisen (z.B. Befehle in ein offenes
  Admin-Terminal tippen). Das ist faktisch eine lokale Rechteausweitung.
- **Befund b:** `packaging/README-uinput.md` empfiehlt `usermod -aG input "$USER"`. Die
  Gruppe `input` besitzt aber auch **Lesezugriff auf alle `/dev/input/event*`** (`root:input
  0660`, verifiziert). Jeder Prozess des Nutzers (A2) kann damit alle Tastaturen systemweit
  mitlesen – auch Passworteingaben in `sudo`, im Sperrbildschirm oder auf der Konsole,
  unabhängig von X11/Wayland-Isolation. Der Nutzer des Entwicklungsrechners ist bereits in
  `input` (verifiziert per `id`).
- **Befund c:** Die Regel `packaging/70-dofscroll-uinput.rules` kombiniert
  `GROUP="input", MODE="0660"` mit `TAG+="uaccess"`. `uaccess` allein genügt bereits (ACL
  für den Nutzer der aktiven Sitzung am Seat); die Gruppe macht den Zugriff zusätzlich
  sitzungsunabhängig dauerhaft (auch per SSH, auch für Hintergrundprozesse).
- **Empfehlung:**
  1. Auf dem Entwicklungsrechner `/dev/uinput` wieder auf `0660`/`root:root` bzw. per Regel
     setzen (Ursache des `0666` suchen: andere udev-Regel unter `/etc/udev/rules.d` oder
     `/usr/lib/udev/rules.d`, z.B. von Steam/Input-Remapping-Tools).
  2. Regel auf `KERNEL=="uinput", OPTIONS+="static_node=uinput", TAG+="uaccess"` reduzieren
     (ohne `GROUP`/`MODE` – udev-Standard ist `0600 root`), damit nur der Nutzer der aktiven
     lokalen Sitzung Zugriff erhält. Wer zwingend eine Gruppe braucht: eigene Gruppe nur für
     `/dev/uinput` (z.B. `spnav-uinput`), **nicht** `input`.
  3. In `packaging/README-uinput.md` den `usermod -aG input`-Schritt durch einen Hinweis auf
     das Risiko ersetzen und die `0666`-Bemerkung als Fehlkonfiguration kennzeichnen.
  4. Grundsätzlich unter X11 das Backend `x11` bevorzugen (Standard bei `BACKEND=auto`), dann
     wird `/dev/uinput` gar nicht gebraucht.

### S2 (Mittel) – Unbegrenzte Config-Zahlen: Überlauf/UB und Event-Fluten

**Status (2026-10-15):** Empfehlung (3) ist in den Ausgabe-Backends umgesetzt:
`X11Output::wheelV/wheelH` und `UinputOutput::wheelV/wheelH` klemmen `n` auf ±200, bevor
negiert, geschleift oder mit 120 multipliziert wird (kein `INT_MIN`-UB, kein
`n*120`-Überlauf, begrenzte Klickzahl). Config-/DSP-Grenzen sind ebenfalls umgesetzt
(Review 2026-10-15 bestätigt): Mix-Gewichte ≤ 100, Kurvenexponent 0,1–10, Kurvenpunkte
streng aufsteigend (Eingabe ≤ 100000, Faktor ≤ 100), `ZOOM_EXIT_RATIO` ≥ 1, `SENS` 0–255,
`SMOOTH_MS` 0–500; `StepAccumulator::add` verwirft nicht endliche Raten und klemmt die
Schritte vor dem Cast auf ±200. Die Tabelle unten zeigt den **ursprünglichen** Befund.

- **Befund:** Mehrere Werte sind nur nach unten oder gar nicht begrenzt:
  - Mix-Gewichte (`src/config.cpp`): nur `isfinite` pro Gewicht, z.B. `1e308*z`.
  - Kurvenexponent (`src/config.cpp`): `std::max(0.1, v)`, keine Obergrenze.
  - Kurvenpunkte (`src/config.cpp`): Faktor nur `>= 0`.
  - `ZOOM_EXIT_RATIO` (`src/config.cpp`), `DEADZONE` bis 1e9 (harmlos).

  In `src/dsp.cpp` (`pow(a/200, exp)*200` bzw. `a*f`) und `src/dsp.cpp` wird
  daraus eine Schrittrate, die `StepAccumulator::add` (`src/dsp.cpp`) per
  `static_cast<int>(std::trunc(acc_))` in `int` wandelt. Liegt der Wert außerhalb von `int`
  (oder ist er `inf`), ist das **undefiniertes Verhalten**. Nachgestellt mit `config.cpp` +
  `dsp.cpp` (Vollausschlag 350, 8 ms-Tick):

  | Config-Zeile | Rate | Schritte pro Tick |
  |---|---|---|
  | `SCROLL_V_CURVE = 60` | 1,6e33/s | `-2147483648` (UB, x86 liefert INT_MIN) |
  | `SCROLL_V_MIX = 1e308*z + 1e308*rx` | `inf` | `-2147483648` (UB) |
  | `SCROLL_V_CURVE = 0:0 350:1e6` | 1,7e8/s | 1 340 000 |
  | `SCROLL_V_CURVE = 4` (Maximum des Tray-Reglers) | 6 300/s | 50 |

  Folgen: `wheelV(INT_MIN)` negiert `n` in `src/output_x11.cpp` (`-n` bei INT_MIN
  ist wieder UB) bzw. in `src/output_uinput.cpp`; bei großen, aber gültigen Werten
  erzeugt `click()` (`src/output_x11.cpp`) bzw. `tapArrows()`
  (`src/dofscroll.cpp`) Millionen XTest-Events in einer Schleife – der X-Server
  und die Sitzung sind dann praktisch blockiert. Schon über die GUI erreichbar sind
  Exponent 4 + Empfindlichkeit 255 ≈ 2 500 Klicks pro Tick (≈ 320 000/s).
  Auslösbar durch A2 (Datei schreiben), A4 (fremde Config übernommen) und durch Bedienfehler.
- **Empfehlung:** (1) In `applyChannel`/`parseMix`/`parseCurve` Obergrenzen setzen (z.B.
  |Gewicht| ≤ 100, Exponent ≤ 10, Kurvenfaktor ≤ 100, `ZOOM_EXIT_RATIO` ≤ 100) und
  Verletzungen als Warnung melden. (2) Unabhängig davon in `StepAccumulator::add` nicht
  endliche Raten verwerfen und die Schritte pro Tick hart begrenzen (z.B. ±50), bevor nach
  `int` gewandelt wird; den Akkumulator dabei klemmen. (3) In den Backends `n` vor der
  Negation begrenzen. (4) Einen Test in `tests/` mit genau diesen Werten ergänzen.

### S3 (Mittel) – „Hängende“ Strg-Taste und verlorene Key-Up-Events

**Status (2026-10-15):** Befund a (kdotool-Anteil) **behoben** durch S6 (Zeitlimit 200 ms).
Befund b **behoben** (siehe dort). Befund c **behoben**: `UinputOutput::doFlush` behält bei `EAGAIN` ungesendete Ereignisse im
Puffer und sendet sie beim nächsten Flush erneut; bei Überlauf (> 4096 Ereignisse) werden nur
die ältesten Wheel-Ereignisse verworfen, nie Tasten-/Button-Ereignisse. Das
Config-Lesen blockiert nicht mehr an FIFOs (`loadProfiles` lehnt nicht reguläre Dateien ab, S4).
Die Fensterabfrage (X11-Roundtrips und `kdotool`) läuft jetzt in einem eigenen Hintergrund-
Thread (`ActiveWindow`, Intervall 100 ms bzw. 250 ms für `kdotool`); `poll()` liest nur den
Zwischenspeicher und blockiert den Tick-Thread nie. Dafür ruft `main` `XInitThreads()` auf,
der Fehlerhandler ist thread-lokal und wird einmal gesetzt. **Befund a damit behoben.**

- **Befund a (Blockieren im Tick-Thread):** Während des Zooms hält der Daemon Strg gedrückt
  (`src/dofscroll.cpp`, `setCtrl`). Alle Profil-Eingaben laufen im selben
  Thread: `ActiveWindow::poll()` startet unter KDE/Wayland per `popen` `kdotool` und wartet
  in `pclose` auf dessen Ende (`src/activewin.cpp`; DBus-Timeouts von bis zu 25 s
  sind möglich), unter X11 macht es mehrere `XSync`-Roundtrips (`src/activewin.cpp`),
  und `loadConfig()` liest die Datei synchron (siehe S4: FIFO blockiert unbegrenzt).
  Blockiert einer dieser Aufrufe, bleibt Strg in der Sitzung gedrückt – jede normale
  Tastatureingabe wird dann zum Shortcut (`Strg+W`, `Strg+Q` …).
- **Status Befund b: behoben.** Der Daemon gibt beim Start (`forceReleaseAll`) Strg, Pfeiltasten
  und linke Maustaste unabhängig vom eigenen Zustand frei, und die Unit ruft per
  `ExecStopPost=-/usr/local/bin/dofscroll --release-keys` dasselbe nach jedem Stopp
  (auch nach SIGKILL/Absturz) auf. Nicht behebbar bleibt die Zeit zwischen Kill und
  Neustart/StopPost (≈ `RestartSec=1s`).
- **Befund b (Absturz/Kill unter X11):** Bei `SIGTERM` werden Tasten freigegeben, nicht aber
  bei `SIGKILL` (z.B. systemd nach `TimeoutStopSec`), Absturz (S2) oder Xlib-IO-Fehler
  (`quietIOErrorHandler` kehrt zurück, Xlib beendet den Prozess dann selbst,
  `src/output_x11.cpp`). Per XTest gedrückte Tasten werden beim Verbindungsabbruch
  nicht zuverlässig losgelassen. (Beim uinput-Backend gibt der Kernel beim Entfernen des
  Geräts gedrückte Tasten frei.)
- **Befund c (uinput verwirft Events):** `UinputOutput::flush` (`src/output_uinput.cpp`)
  verwirft bei `EAGAIN` (nicht blockierender fd) den gesamten Rest des Puffers – darin kann
  das Key-Up für Strg oder `BTN_LEFT` liegen. Der interne Zustand `held_`/`btnLeft_` gilt dann
  als „losgelassen“, die Taste bleibt im System aber gedrückt.
- **Empfehlung:** Fensterabfrage in einen eigenen Thread (oder `posix_spawn` mit
  nicht blockierender Pipe, Timeout und `kill`) auslagern; Config nur lesen, wenn sie eine
  reguläre Datei ist (S4). In `flush` bei `EAGAIN` Release-Events behalten und im nächsten
  Tick erneut senden. Optional `ExecStopPost=` mit einem kleinen Helfer, der Strg freigibt
  (z.B. `xdotool keyup ctrl`, falls installiert), und einen Watchdog, der Strg freigibt, wenn
  ein Tick deutlich zu spät kommt.

### S12 (Niedrig, behoben) – `/dev/uinput`-Dateideskriptor ohne `O_CLOEXEC`

- **Befund:** `UinputOutput::init` öffnete `/dev/uinput` (und den Fallback
  `/dev/input/uinput`) mit `O_WRONLY | O_NONBLOCK` ohne `O_CLOEXEC`. Der per `popen`
  gestartete `kdotool`-Kindprozess (S6) erbte damit einen **schreibbaren** fd auf
  `/dev/uinput` und hätte (bzw. jedes von ihm nachgestartete Programm) darüber Eingaben
  einspeisen können.
- **Behebung:** beide `open()`-Aufrufe nutzen jetzt `O_CLOEXEC`; zusätzlich schließt das
  `kdotool`-Kind in `runCapture` alle fds ≥ 3 und die Pipe wird mit `pipe2(O_CLOEXEC)` angelegt.

### S4 (Niedrig) – `loadProfiles` ohne Eingabegrenzen

- **Befund:** `src/config.cpp` öffnet den Pfad mit `std::ifstream` und liest per
  `std::getline` ohne jede Grenze: keine Prüfung auf reguläre Datei, keine Maximalgröße,
  keine maximale Zeilenlänge, keine Höchstzahl an Sektionen/Termen. Ein Symlink auf
  `/dev/zero` lässt den Daemon bis zum OOM Speicher allozieren (eine einzige endlose Zeile),
  ein FIFO blockiert schon `open()` – im Haupt-Thread, also mit S3a. `ps.findOrAdd`
  (`src/config.cpp`) vergleicht jede neue Sektion per `lower()` mit allen
  vorhandenen (quadratisch), bei vielen tausend Sektionen spürbar. Jede Zeile kopiert
  zusätzlich ein komplettes `Settings`-Objekt (`src/config.cpp`, plus Kopie in
  `applyKey`).
  Erreichbar nur für jemanden, der `~/.config/dofscroll` (bzw. beim Legacy-Fallback `~/.config`) schreiben kann (A2) – daher Niedrig.
  Ganzzahlen selbst sind sauber begrenzt (`parseInt` lehnt |v| > 1e9 ab, `src/config.cpp`;
  Button-Nummern werden vor dem Shift auf `< 32` geprüft, `src/dofscroll.cpp`).
- **Empfehlung:** Datei mit `open(O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)` öffnen,
  `fstat` → nur `S_ISREG` akzeptieren, Größe ≤ 1 MiB, Zeilen > 4 KiB verwerfen, höchstens
  z.B. 256 Sektionen und 32 Mix-Terme. (Wenn die Config bewusst ein Symlink in ein
  Dotfile-Repo sein soll, statt `O_NOFOLLOW` nur `S_ISREG` des Ziels prüfen.)

### S5 (Niedrig) – Speichern der Config (Temp-Datei, Rechte, TOCTOU)

- **Befund a (Temp-Datei):** `saveProfiles` schreibt nach `path + ".tmp." + PID`
  (`src/config.cpp`) mit `std::ofstream(…, trunc)` – also `O_CREAT|O_TRUNC`
  ohne `O_EXCL`/`O_NOFOLLOW`. Liegt die Config in einem für andere beschreibbaren
  Verzeichnis (z.B. `XDG_CONFIG_HOME=/tmp/…`), kann A1 dort vorab einen Symlink mit dem
  vorhersagbaren Namen anlegen; der Tray kürzt/überschreibt dann eine beliebige Datei des
  Nutzers. Im Standardfall (`~/.config/dofscroll`, `0700`, nutzereigen) nicht ausnutzbar.
- **Befund b (Rechte):** Die Datei entsteht mit den Rechten aus der umask (aktuell `0644`,
  verifiziert). Die Config enthält keine Geheimnisse – das ist Info, aber `0600` ist der
  sauberere Standard. Mit der neuen Unit (`UMask=0077`) wird beim nächsten Speichern `0600`
  geschrieben; `rename` ersetzt die alte Datei inklusive ihrer Rechte.
- **Befund c (Haltbarkeit):** Kein `fsync` vor `rename` (`src/config.cpp`) und kein
  `fsync` des Verzeichnisses. Nach Stromausfall kann auf XFS/ext4 eine leere Datei stehen
  bleiben; der Daemon fällt dann still auf Defaults zurück.
- **Befund d (Symlink-Config):** `rename` ersetzt einen Symlink an `path` durch eine reguläre
  Datei – wer die Config als Link in ein Dotfile-Repo pflegt, verliert die Verbindung still.
- **Befund e (Lost Update/TOCTOU):** Der Tray liest die Datei nur beim Öffnen des Fensters
  (`src/dofscroll_tray.cpp`), hält sie im Speicher und schreibt nach jeder
  Änderung (250 ms entprellt) die **gesamte** Datei. Was zwischenzeitlich von Hand
  (z.B. über „Konfigurationsdatei öffnen“) oder von einer zweiten Tray-Instanz
  geändert wurde, wird ohne Warnung überschrieben. `access(F_OK)` vor dem Anlegen
  ist ebenfalls ein (harmloses) TOCTOU.
- **Befund f (Leser):** Der Daemon liest alle 1 s per `stat`-Stempel neu
  (`src/dofscroll.cpp`). Schreibt ein Editor nicht atomar, kann er eine
  halb geschriebene Datei lesen – Folge: kurzzeitig falsche, aber gültig geparste Werte.
- **Empfehlung:** Temp-Datei per `mkstemp` im Zielverzeichnis (`O_EXCL`, Modus `0600`),
  `fsync(fd)` + `rename` + `fsync(dirfd)`. Vor dem Speichern `stat`-Stempel (mtime/Inode/Größe)
  mit dem beim Laden gemerkten vergleichen und bei Abweichung neu laden bzw. nachfragen;
  optional `flock` auf eine Lock-Datei. Für Symlink-Configs das Ziel (`realpath`) ersetzen.

### S6 (Niedrig) – `popen` in `activewin.cpp`

**Status (2026-10-15): behoben.** `ActiveWindow` löst `kdotool` in `init()` einmalig über
`findInPath` auf (nur absolute `PATH`-Einträge, relative/leere werden ignoriert) und startet
es in `runCapture` per `fork`/`execl` ohne Shell (feste Argumente, stdin/stderr auf
`/dev/null`, alle übrigen fds im Kind geschlossen, Pipe mit `O_CLOEXEC`). Die Ausgabe wird mit
`poll` und 200 ms Zeitlimit gelesen; bei Überschreitung wird das Kind per `SIGKILL` beendet und
eingesammelt (`waitpid`), Exit-Status ≠ 0 oder Fehler ergeben einen leeren Treffer
(kein Profil-Match) statt eines Blockierens. Die Beschreibung darunter ist der Zustand vor der Korrektur.

- **Befund:** `popen("kdotool getactivewindow getwindowclassname 2>/dev/null", "r")`
  (`src/activewin.cpp`) – die Kommandozeile ist fest, also **keine** Shell-Injektion.
  Aber: `popen` startet `/bin/sh` und sucht `kdotool` über `PATH` (und `inPath`,
  `src/activewin.cpp`, prüft nur Ausführbarkeit). Ein beliebiges `kdotool` früher im
  PATH (z.B. `~/.local/bin`, falls es im PATH des User-Managers vor `/usr/bin` steht) wird
bis zu 4×/s ausgeführt – relevant nur
  für A2, der ohnehin Code als Nutzer ausführt. Gelesen werden höchstens 255 Bytes der
  ersten Zeile (`fgets` in `buf[256]`) – Puffer korrekt begrenzt. Hauptproblem ist
  das Blockieren (S3a). Die Kindprozesse erben die systemd-Sandbox des Daemons (z.B.
  `MemoryDenyWriteExecute`, nur `AF_UNIX`), was für `kdotool` (DBus) passt.
- **Empfehlung:** `posix_spawn` mit absolutem Pfad (beim Start einmal auflösen und loggen),
  Pipe nicht blockierend mit Timeout lesen, Kind bei Zeitüberschreitung beenden – oder die
  Abfrage direkt per DBus (KWin-Scripting) in einem eigenen Thread.

### S7 (Niedrig) – Reader-/Haupt-Thread, Signale, Shutdown

- **Befund a (Datenaustausch):** Achsen, Tastenmaske, Zeitstempel und Generation sind
  `std::atomic`, der Gerätename wird unter Mutex übergeben (`src/dofscroll.cpp`) –
  **kein Datenrennen**. Die sechs Achsen werden einzeln gespeichert/gelesen; der Haupt-Thread
  kann einen „gemischten“ Satz aus zwei Events sehen (für einen Tick). Das ist fachlich
  unkritisch (Info). Die libspnav-Aufrufe erfolgen ausschließlich im Reader-Thread, Xlib
  ausschließlich im Haupt-Thread (daher ist fehlendes `XInitThreads` korrekt);
  `XSetErrorHandler` ist prozessglobal, wird aber nur im Haupt-Thread umgesetzt
  (`src/activewin.cpp`, `src/output_x11.cpp`).
- **Befund b (Shutdown):** Der Reader-Thread ist detached und blockiert in
  `spnav_wait_event` (`src/dofscroll.cpp`). Nach `return 0` aus `main` laufen die
  statischen Destruktoren (u.a. `std::cerr`), während der Thread noch Events loggen kann
  – theoretisch UB beim Beenden. Das absichtlich geleakte `Daemon`-Objekt
  verhindert nur den Zugriff auf freigegebenen `DeviceReader`-Speicher.
- **Befund c (Signale):** Die Handler werden erst am Ende von `initialize()` installiert
  (`src/dofscroll.cpp`), nach Backend-Init (uinput wartet dort 300 ms). Ein SIGHUP
  des Trays in diesem Fenster beendet den Prozess (Default-Aktion); `Restart=always` fängt das
  auf. Die Handler selbst sind korrekt minimal (`volatile sig_atomic_t`); `std::signal`
  statt `sigaction` ist unter glibc unproblematisch.
- **Empfehlung:** Signale als Erstes in `main` setzen (bzw. per `sigaction` mit `SA_RESTART`);
  beim Beenden nach `releaseAll()`/`flush()` `_exit(0)` statt `return` oder den Reader per
  `shutdown(spnav_fd(), SHUT_RDWR)` aufwecken und `join`en.

### S8 (Niedrig) – Root-eigene Symlinks in ein nutzerbeschreibbares Verzeichnis

- **Befund:** `/usr/local/bin/dofscroll` und `…-tray` sind root-eigene Symlinks in
  `~/private/dofscroll-cpp/` (verifiziert; so von der README vorgesehen). Wer das
  Repo-Verzeichnis schreiben kann (der Nutzer, also auch A2), bestimmt, was jeder andere
  Nutzer – auch root – beim Aufruf von `/usr/local/bin/dofscroll` ausführt. Ebenso sucht
  `findIconDir` Icons neben dem aufgelösten Binary (`src/dofscroll_tray.cpp`).
  Auf einem Einzelplatzrechner ohne fremde Aufrufer ist das akzeptabel.
- **Empfehlung:** Auf Mehrbenutzersystemen `make install` (Kopie nach `/usr/local`, root-eigen)
  statt Symlinks verwenden oder die Binaries/Units nur im Nutzerkontext ablegen
  (`~/.local/bin`) und `/usr/local/bin` nicht anfassen.

### S9 (Niedrig) – `spacenavd`-Socket für alle beschreibbar

- **Befund:** `/run/spnav.sock` ist `srwxrwxrwx root` (verifiziert, Standard von `spacenavd`).
  Jeder lokale Nutzer (A1) kann Bewegungen/Tasten der SpaceMouse mitlesen und – mit dem
  libspnav-1.x-Protokoll – Geräteeinstellungen von `spacenavd` (Empfindlichkeit, Achsen,
  LED) ändern und damit die Bedienung stören. Den Socket fälschen kann A1 nicht (`/run` ist
  root-only). Der Daemon vertraut den Achswerten von `spacenavd` ungeprüft; extreme Werte
  (A3 über präpariertes Gerät, falls `spacenavd` sie durchreicht) laufen in denselben Pfad
  wie S2 – die Härtung aus S2 deckt das mit ab.
- **Empfehlung:** Für Einzelplatzrechner akzeptieren; sonst `spacenavd` so konfigurieren bzw.
  per Drop-in (`RuntimeDirectory`/`chmod` in `ExecStartPost`) auf eine Gruppe beschränken.

### S10 (Niedrig) – Tray: Hilfsprozesse

- **Befund:** Positiv: `spawnDetached` nutzt `g_spawn_async` mit argv-Array, keine Shell
  (`src/dofscroll_tray.cpp`); der Config-Pfad wird als einzelnes Argument
  übergeben. GUI-Werte werden vor dem Schreiben mit demselben Parser validiert
  (`parseMix`/`applyKey`), Profilmuster dürfen weder `]` noch Zeilenumbrüche enthalten
  – **keine Config-Injektion über die GUI**. Aber: `G_SPAWN_SEARCH_PATH` sucht
  `systemctl`/`xdg-open` über `PATH`. Mit der gehärteten Unit erben
  `xdg-open` und der dadurch gestartete Editor die Tray-Sandbox (nur `~/.config/dofscroll` und
  `/run/user/UID` schreibbar, nur `AF_UNIX`/`AF_NETLINK`). Ein neu gestarteter Editor kann
  dann z.B. nicht unter `~/Dokumente` speichern; läuft der Editor schon (Single-Instance per
  DBus, z.B. Kate), öffnet er die Datei außerhalb der Sandbox.
- **Empfehlung:** Absolute Pfade (`/usr/bin/systemctl`, `/usr/bin/xdg-open`) verwenden; für
  „Datei öffnen“ statt `xdg-open` als Kind die DBus-Portal-/`org.freedesktop.FileManager1`-
  bzw. `gtk_show_uri_on_window`-Variante nutzen, die den Editor über den Session-Bus außerhalb
  der Sandbox startet.

### S11 (Niedrig) – Config-Pfad aus der Umgebung

- **Befund:** `defaultConfigPath` (`src/config.cpp`) übernimmt `XDG_CONFIG_HOME`
  ungeprüft (die XDG-Spezifikation verlangt, relative Werte zu ignorieren) und fällt bei
  fehlendem `HOME` auf `"."` zurück – dann hängt der Config-Pfad vom Arbeitsverzeichnis ab.
- **Empfehlung:** Nur absolute `XDG_CONFIG_HOME` akzeptieren; ohne `HOME` per `getpwuid(getuid())`
  das Home ermitteln statt `"."`.

### Info / Positives

- **I1 Puffer:** `spnav_dev_name(name, sizeof(name))` mit expliziter Nullterminierung
  (`src/dofscroll.cpp`); `readlink("/proc/self/exe", exe, sizeof(exe)-1)` mit
  `n > 0`-Prüfung und Terminierung (`src/dofscroll_tray.cpp`; bei Pfaden ≥ 4095
  Zeichen würde still abgeschnitten → falscher Icon-Ordner, harmlos); `strncpy` des
  uinput-Namens mit `UINPUT_MAX_NAME_SIZE - 1` nach `memset` (`src/output_uinput.cpp`);
  `fmtNum` mit `snprintf(buf, sizeof buf)` (`src/config.cpp`). Keine Formatstring-Fehler
  (`fprintf`/`gtk_message_dialog_new` immer mit `"%s"`).
- **I2 uinput-Gerät minimal:** Das virtuelle Gerät meldet nur `BTN_LEFT`, Strg, vier
  Pfeiltasten und Wheel (`src/output_uinput.cpp`) – ein kompromittierter Daemon kann
  über dieses Gerät keine beliebigen Tasten tippen (wohl aber über ein neues, solange
  `/dev/uinput` beschreibbar ist, siehe S1).
- **I3 WM_CLASS-Spoofing:** Jeder X-Client wählt seine `WM_CLASS` selbst und kann so ein
  fremdes `[app:*]`-Profil auslösen – Auswirkung nur auf Scroll-Parameter (begrenzt durch S2).
- **I4 Log-Injektion:** Der Gerätename (aus dem USB-Deskriptor, A3) wird unescaped ins Journal
  geschrieben (`src/dofscroll.cpp`); mit Zeilenumbrüchen ließen sich Logzeilen
  vortäuschen. Kosmetisch; ggf. Steuerzeichen ersetzen.
- **I5 Tastendrücke im Journal:** Jeder SpaceMouse-Tastendruck wird geloggt – keine
  Geheimnisse, aber Journal-Rauschen.
- **I6 Netz:** Weder Daemon noch Tray brauchen IP-Netz; die Units sperren `AF_INET/AF_INET6`.

---

## 3. systemd-Härtung der User-Units

Dateien: `systemd/dofscroll.service`, `systemd/dofscroll-tray.service`.
Zielplattform RHEL 9 = **systemd 252**; es wurden nur dort verfügbare Direktiven verwendet.

> **Achtung, wirksam ab dem nächsten Login/`daemon-reload`:** Auf dem Entwicklungsrechner
> sind `~/.config/systemd/user/dofscroll*.service` **Symlinks in dieses Repo** (siehe
> README). Die geänderten Units werden beim nächsten `systemctl --user daemon-reload` plus
> Neustart der Dienste bzw. beim nächsten Login aktiv. Bei der Erstellung wurde weder ein
> `daemon-reload` noch ein Neustart ausgeführt. Rückweg bei Problemen: die
> `[Service]`-Härtungszeilen auskommentieren oder per Drop-in
> (`systemctl --user edit dofscroll.service`) einzelne Direktiven zurücksetzen, z.B.
> `MemoryDenyWriteExecute=no`.

### Gesetzt (beide Units, sofern nicht anders vermerkt)

| Direktive | Zweck / Hinweis |
|---|---|
| `NoNewPrivileges=yes` | kein Rechtegewinn über setuid/File-Caps, auch nicht für Kindprozesse |
| `UMask=0077` | neu geschriebene Dateien (Config!) nur für den Nutzer lesbar |
| `PrivateUsers=yes` | **Pflicht im User-Manager von systemd 252**, siehe unten |
| `CapabilityBoundingSet=` | leere Bounding-Set (funktioniert nur zusammen mit `PrivateUsers=yes`) |
| `ProtectSystem=strict` | gesamtes Dateisystem read-only (außer `/dev`, `/proc`, `/sys`-API) |
| `ProtectHome=read-only` | Home nur lesbar; Daemon liest die Config, schreibt dort nichts |
| `ConfigurationDirectory=dofscroll`, `ConfigurationDirectoryMode=0700` | **nur Tray**: legt `~/.config/dofscroll` (0700) vor dem Aufbau der Sandbox an, damit das `ReadWritePaths`-Ziel existiert |
| `ReadWritePaths=%t` | **nur Daemon**: `/run/user/UID` beschreibbar; der Daemon legt dort ausschließlich `dofscroll-status` (Temp-Datei + rename) und `dofscroll.log` an, siehe „Laufzeitdateien“ unten |
| `ReadWritePaths=%E/dofscroll %t` | **nur Tray**: Config schreiben (temp + rename im selben Verzeichnis), `/run/user/UID` für dconf/at-spi. Der Rest von `~/.config` (u.a. `systemd/user`, `autostart`, `environment.d`) bleibt read-only; die früheren `ReadOnlyPaths`-Ausnahmen sind entfallen |
| `ProtectKernelTunables=yes`, `ProtectKernelModules=yes`, `ProtectKernelLogs=yes`, `ProtectControlGroups=yes`, `ProtectClock=yes`, `ProtectHostname=yes` | Kernel-/cgroup-/Uhr-/Hostname-Schutz |
| `PrivateIPC=yes` | **nur Daemon** (kein SysV-IPC nötig) |
| `KeyringMode=private` | eigener Kernel-Keyring |
| `RestrictAddressFamilies=AF_UNIX` (Daemon) / `AF_UNIX AF_NETLINK` (Tray) | X11, `spacenavd`, DBus sind Unix-Sockets; GIO nutzt ggf. Netlink |
| `RestrictNamespaces=yes`, `RestrictRealtime=yes`, `RestrictSUIDSGID=yes`, `LockPersonality=yes` | Standardhärtung |
| `MemoryDenyWriteExecute=yes` | **nur Daemon** (libX11/libXtst/libspnav brauchen kein W+X) |
| `SystemCallArchitectures=native` | keine 32-bit-Syscalls |
| `SystemCallFilter=@system-service` + `~@privileged` + `~@resources`, `SystemCallErrorNumber=EPERM` | Syscall-Allowlist; `~@privileged` sperrt u.a. `@module`, `@clock`, `@raw-io`, `@reboot`, `@swap`; `~@resources` (seit 2026-10-15) sperrt `setpriority`, `sched_set*`, `setrlimit`, `ioprio_set`, `mbind` ... (`prlimit64` zum Abfragen bleibt erlaubt). Empirisch wirksam (EPERM) und ohne Funktionsverlust: weder Daemon noch Tray rufen diese Syscalls auf |

### Warum `PrivateUsers=yes` nötig ist (geprüft)

Ohne `PrivateUsers=yes` verhielt sich der systemd-252-User-Manager auf dem
Entwicklungsrechner so (Test mit transienten Units, siehe „Prüfung“):

- `ProtectSystem=strict`, `ProtectHome=read-only` usw. wurden **stillschweigend ignoriert** –
  die Testunit konnte trotzdem `~/.config` und `~` beschreiben; `systemd-analyze security`
  meldete dennoch „2.5 OK“ (die Offline-Analyse prüft nur die Unit-Datei, nicht die Wirkung).
- `CapabilityBoundingSet=`, `ProtectKernelModules=`, `ProtectKernelLogs=`, `ProtectClock=`
  ließen den Start mit `status=218/CAPABILITIES` scheitern (der User-Manager darf die
  Bounding-Set ohne eigenen User-Namespace nicht verkleinern).

Mit `PrivateUsers=yes` greifen alle Einschränkungen. Nebeneffekt: Zusatzgruppen erscheinen im
Dienst als `nobody` (`id` zeigt `65534(nobody)`), die Rechteprüfung des Kernels nutzt aber
weiter die echten Gruppen – Zugriff auf eine `root:input 0660`-Gerätedatei funktionierte im
Test. `systemctl --user` aus der Sandbox (Tray: SIGHUP/Restart) funktioniert ebenfalls, da der
User-Manager den Aufrufer über `SO_PEERCRED` mit der echten UID sieht. Voraussetzung:
unprivilegierte User-Namespaces sind erlaubt (`/proc/sys/user/max_user_namespaces` > 0; RHEL 9
Standard).

### Grenze der Sandbox: DBus/`systemd-run --user`

Die Härtung begrenzt nur den Prozess der Unit selbst, **keine** Dienste, die er über die
Session-Bus-Verbindung erreicht. Da `AF_UNIX` und der DBus-Session-Socket erlaubt sein
müssen (AppIndicator, `kdotool`, X11), kann ein kompromittierter Daemon oder Tray – wie jeder
Prozess mit Zugriff auf den Session-Bus bzw. den User-Manager – per DBus (`org.freedesktop.systemd1`)
oder `systemd-run --user` eine **neue, ungehärtete Transient-Unit** starten, die weder
`ProtectSystem`, `PrivateUsers`, `MemoryDenyWriteExecute` noch die Syscall-/Adressfamilien-
Filter erbt und mit den vollen Rechten des Nutzers läuft. Die Sandbox schützt also gegen
Fehler im Daemon (Speicherfehler, Dateizugriffe), ist aber **keine Grenze gegen einen
Angreifer mit Code-Ausführung** im Dienst. Gegenmaßnahmen wären ein gefilterter Bus
(`xdg-dbus-proxy`) bzw. der Verzicht auf den Session-Bus im Daemon (er braucht ihn nur für
`kdotool`); beides ist hier bewusst nicht umgesetzt.

### Bewusst NICHT gesetzt

| Direktive | Grund |
|---|---|
| `PrivateTmp=yes` | verdeckt `/tmp/.X11-unix/X0` → Xlib könnte nur noch über den abstrakten Socket verbinden, der je nach X-Server-Konfiguration (`-nolisten local`) fehlt; außerdem nutzen manche Setups `/tmp` für `XAUTHORITY`. Lesender Zugriff auf `/tmp` bleibt durch `ProtectSystem=strict` ohnehin read-only. |
| `PrivateNetwork=yes` | kappt abstrakte Unix-Sockets (X11 `@/tmp/.X11-unix/X0`, ggf. DBus/at-spi); Netzwerkzugriff ist über `RestrictAddressFamilies` bereits gesperrt |
| `PrivateDevices=yes`, `DevicePolicy=`/`DeviceAllow=` | `PrivateDevices` verdeckt `/dev/uinput`; Device-ACLs (cgroup-BPF) kann der User-Manager nicht durchsetzen – die Direktive würde angenommen, aber wirkungslos bleiben |
| `ProtectProc=invisible`, `ProcSubset=pid` | in systemd 252 verfügbar und startbar (beide Units, Tray 3 s + Daemon `--release-keys` getestet), aber in einer User-Unit mit `PrivateUsers=yes` **wirkungslos** (erneut geprüft 2026-10-15: weiterhin alle 776 PIDs, `/proc/sys`, `/proc/acpi` sichtbar). Nicht gesetzt, um keine Scheinsicherheit zu erzeugen |
| `MemoryDenyWriteExecute=yes` (Tray) | GLib-`GRegex` nutzt PCRE2-JIT, Mesa/GL im GTK-Stack braucht ggf. W+X-Speicher; zudem erbt der per `xdg-open` gestartete Editor die Einschränkung |
| `PrivateIPC=yes` (Tray) | GTK/cairo nutzen MIT-SHM (SysV-Shared-Memory gemeinsam mit dem X-Server) |
| `ReadWritePaths=` (Daemon) | nur `%t` (Runtime-Dir) für Status-/Log-Datei, nichts unter `~` oder `/etc`; `/run/user/UID` ist ohnehin privat (0700) |
| `IPAddressDeny=any` | startet in beiden Units, ist aber im User-Manager **wirkungslos** (geprüft 2026-10-15: `connect()` auf `127.0.0.1:22` gelang trotz `IPAddressDeny=any`; cgroup-BPF-Firewall nur für root-Manager). Der Daemon ist durch `RestrictAddressFamilies=AF_UNIX` ohnehin abgedeckt; der Tray braucht `AF_NETLINK` und bleibt daher bewusst ohne IP-Sperre |
| `User=`/`DynamicUser=`, `RootDirectory=` | nicht sinnvoll für User-Units, die in der Sitzung des Nutzers arbeiten |
| `ProtectSystem`-Ausnahme für `/tmp` | nicht nötig: `connect()` auf Unix-Sockets funktioniert auch auf read-only Mounts (X11, `spacenavd`, DBus getestet) |
| `RestrictFileSystems=`, `ProtectHome=tmpfs` | `RestrictFileSystems` braucht BPF-LSM (im User-Manager nicht verfügbar); `tmpfs` würde die Binaries verdecken, die per Symlink im Home liegen |

### Nachhärtung 2026-10-15 (Kandidaten ProtectProc/ProcSubset/IPAddressDeny/`~@resources`)

Jeder Kandidat wurde einzeln und kombiniert per `systemd-run --user --wait --collect` mit
allen übrigen Direktiven der jeweiligen Unit getestet (Daemon: `dofscroll --release-keys`,
das ist auch der `ExecStopPost`-Pfad in derselben Sandbox; Tray: 3 s unter `timeout`, Exit
124 = lief ohne Absturz, stderr nur eine Gtk-Locale-Warnung). Ergebnis: alle vier starten;
wirksam ist nur `~@resources` (gesetzt). `ProtectProc`/`ProcSubset`/`IPAddressDeny` sind unter
`PrivateUsers=yes` im User-Manager no-ops (Beleg in der Tabelle oben) und deshalb nicht gesetzt.
Das offline berechnete `systemd-analyze security`-Ergebnis (`--offline=true systemd/<unit>`)
stieg dadurch von 2.2 auf 2.1 (beide Units; die drei no-op-Direktiven würden es kosmetisch auf
1.7 drücken, ohne die Sandbox zu verbessern). Nicht getestet: Einstellungsfenster/Editor-Start
des Tray unter `~@resources` (Hardware-/GUI-Test durch den Menschen).

### Prüfung (ohne Installation, ohne Neustart der laufenden Dienste)

```sh
systemd-analyze --user verify    "$PWD/systemd/dofscroll.service"       # keine Ausgabe = ok
systemd-analyze --user verify    "$PWD/systemd/dofscroll-tray.service"
systemd-analyze --user security --offline=yes "$PWD/systemd/dofscroll.service"
systemd-analyze --user security --offline=yes "$PWD/systemd/dofscroll-tray.service"
```

Ergebnis (systemd 252): `verify` ohne Meldungen; Exposure-Score **9.8 UNSAFE → 2.2 OK** für
beide Units.

Da die Offline-Analyse die tatsächliche Wirkung nicht prüft (siehe `PrivateUsers`), wurden
die `[Service]`-Härtungszeilen zusätzlich als **transiente** Units angewendet
(`systemd-run --user --wait --pipe --collect -p … <Testbefehl>`; `%h`/`%t` dabei von Hand
ersetzt) – ohne die echten Dienste zu berühren. Ergebnis:

| Prüfung | Daemon-Profil | Tray-Profil |
|---|---|---|
| `connect()` auf `/tmp/.X11-unix/X0`, `xprop -root`, `xdpyinfo` (XTEST vorhanden) | ok | ok |
| `connect()` auf `/run/spnav.sock` | ok | ok |
| `open("/dev/uinput", O_WRONLY)` | ok | ok |
| `open()` einer `root:input 0660`-Gerätedatei (Gruppenrecht unter `PrivateUsers`) | ok | ok |
| Config lesen | ok | ok |
| in `~/.config` schreiben | verweigert (EROFS) | verweigert (EROFS), nur `~/.config/dofscroll` ok (Anlegen + rename getestet) |
| in `~` schreiben | verweigert | verweigert |
| in `/run/user/UID` schreiben | ok (seit Status-/Log-Datei, nicht gesondert getestet) | ok |
| DBus-Session-Socket verbinden | ok | ok |
| `socket(AF_INET)` | verweigert (EAFNOSUPPORT) | verweigert |
| `systemctl --user kill -s HUP <Testunit>` | – | ok |
| `/usr/local/bin/dofscroll --version` (lädt alle Bibliotheken unter MDWE) | ok | – |
| `dofscroll-tray` 5 s laufen lassen | – | ok (nur bekannte Locale-Warnung, tritt auch ohne Sandbox auf) |

Nicht geprüft werden konnte – das muss am echten System passieren:
Scrollen/Zoomen/Nav-/Klick-Button mit SpaceMouse im gehärteten Dienst, Wayland/uinput-Pfad
mit `kdotool`, Öffnen des Einstellungsfensters und Speichern per GUI, „Konfigurationsdatei
öffnen“ mit dem Standard-Editor. Vorgehen:

```sh
systemctl --user daemon-reload
systemctl --user restart dofscroll.service dofscroll-tray.service
journalctl --user -u dofscroll.service -u dofscroll-tray.service -f
```

---

## 4. Pre-commit-Hook

- `scripts/pre-commit` (POSIX sh):
  1. **Secret-Scan** über die hinzugefügten Zeilen von `git diff --cached` (nur gestagte
     Änderungen). Muster (in der Datei mit `[Y]`/`[t]`/`[A]`/`[d]` geschrieben, damit sich der
     Hook nicht selbst meldet):
     - private Schlüssel: Kopfzeile `-----BEGIN … PRIVATE KE[Y]-----`
     - GitLab Personal Access Token: `glpa[t]-` + mindestens 20 Zeichen `[A-Za-z0-9_-]`
     - AWS Access Key ID: `AKI[A]` + 16 Zeichen `[0-9A-Z]`
     - Passwort-Zuweisung: `passwor[d]`, optionale Leerzeichen, `=`, nicht leerer Wert
       (Groß/Klein egal)

     Bei Treffer: Abbruch mit Datei und Zeile. Umgehung nur bewusst per
     `git commit --no-verify`.
  2. `make check-fast`, falls das Ziel im Makefile existiert (Erkennung über `make -qp`,
     führt nichts aus), sonst `make test`. Ohne Makefile wird übersprungen. Hinweis: `make`
     prüft den Arbeitsbaum, nicht exakt den Index.
- `scripts/install-hooks.sh [--force]`: verlinkt `.git/hooks/pre-commit` →
  `../../scripts/pre-commit` (relativ, überlebt Verschieben des Repos; bei Worktrees bzw.
  abweichendem Hook-Verzeichnis absolut). Idempotent; ein vorhandener fremder Hook wird nur
  nach Rückfrage (interaktiv) bzw. mit `--force` ersetzt und vorher nach
  `pre-commit.bak.<Zeitstempel>` gesichert. Warnt, wenn `core.hooksPath` gesetzt ist.

Der Hook ist ein Komfort-Gate, kein Sicherheitsmechanismus – er läuft nur lokal und lässt
sich umgehen. Verbindlich ist die CI-Pipeline (folgt als GitHub-Actions-Workflow) zusammen mit den Repository-Einstellungen unten.

---

## 5. Repository: Merge-Regeln und Branch-Schutz (manuell einzustellen)

Diese Einstellungen lassen sich nicht über eine Datei im Repo setzen, sondern nur in der
Oberfläche der Hosting-Plattform (Maintainer-/Admin-Rolle) oder per API. Der folgende Text stammt aus der GitLab-Zeit des Projekts („Pipelines must succeed“ entspricht auf GitHub den erforderlichen Status-Checks).

### Merge-Checks

**Settings → Merge requests → Merge checks:**

1. **„Pipelines must succeed“** aktivieren. Ein MR lässt sich dann nur mergen, wenn die
   letzte Pipeline erfolgreich war.
   - **„Skipped pipelines are considered successful“** *nicht* aktivieren.
2. **„All threads must be resolved“** aktivieren. Offene Review-Diskussionen blockieren den
   Merge.
3. Speichern („Save changes“).

Empfohlen auf derselben Seite: Merge-Methode „Merge commit with semi-linear history“ oder
„Fast-forward merge“ (dann läuft die Pipeline auf genau dem Stand, der gemergt wird), sowie
„Enable 'Delete source branch' option by default“.

### Branch-Schutz für `main`

**Settings → Repository → Protected branches:**

1. Branch `main` wählen (bzw. die bestehende Regel bearbeiten).
2. **Allowed to merge:** *Maintainers*.
3. **Allowed to push and merge:** *No one* – Änderungen nur noch über Merge Requests.
4. **Allowed to force push:** aus.
5. Falls verfügbar (Premium): **Require approval from code owners** und unter
   *Settings → Merge requests → Merge request approvals* mindestens eine Freigabe verlangen;
   „Prevent approval by author“ aktivieren.

### Alternative per API (zum Nachvollziehen, z.B. mit `glab api`)

```sh
# Merge-Checks
glab api -X PUT "projects/:id" \
  -f only_allow_merge_if_pipeline_succeeds=true \
  -f allow_merge_on_skipped_pipeline=false \
  -f only_allow_merge_if_all_discussions_are_resolved=true

# main schützen (0 = No one, 40 = Maintainer)
glab api -X POST "projects/:id/protected_branches" \
  -f name=main -f push_access_level=0 -f merge_access_level=40 -f allow_force_push=false
```

(Existiert für `main` schon eine Schutzregel, vorher mit
`glab api -X DELETE "projects/:id/protected_branches/main"` entfernen oder in der Oberfläche
bearbeiten.)

Prüfen: Einen Test-MR mit absichtlich fehlschlagender Pipeline bzw. einem offenen Thread
anlegen – der „Merge“-Knopf muss gesperrt sein; ein direkter `git push` auf `main` muss
abgelehnt werden.

---

## 6. Zweiter Audit (2026-10-15) und Korrekturen

Sechs parallele Reviews (Fensterabfrage/X11, Config, Tray, Daemon, systemd/CI/Supply-Chain,
Werkzeuge). Keine hohen oder kritischen Befunde; alle unten genannten sind **behoben**, sofern
nicht anders vermerkt.

| Bereich | Befund | Korrektur |
|---|---|---|
| Config (mittel) | O(n²)-Laden (50 000 Abschnitte: 29,6 s) | Index in `ProfileSet`, max. 256 Abschnitte, 512 Keys je Abschnitt (10 000 Abschnitte: ~0,05 s) |
| Config (mittel) | Keine Datei-/Zeilenlimits (200-MB-Zeile: 3,9 GB RAM) | Datei ≤ 1 MiB, Zeile ≤ 8 KiB, Mix ≤ 12 Terme, Kurve ≤ 64 Punkte, `ZOOM_LATERAL` ≤ 6 |
| Config | stat/open-TOCTOU (FIFO) | einmal `open(O_NONBLOCK)`, dann `fstat` auf dem Fd |
| Config | Pfad-Races beim Speichern, vorhersagbarer Temp-Name | `openat`/`renameat` am Verzeichnis-Fd, Temp-Name aus `getrandom`, `O_EXCL` |
| Config | Symlink-Schleife ersetzt den Link; setuid/sticky-Bits kopiert | ELOOP-Fehler; Modus auf 0777 maskiert |
| Config | Unvalidierte `kv` beim Speichern; relativer `XDG_CONFIG_HOME`; weltlesbares Config-Verzeichnis | Validierung, relative Pfade ignoriert, Verzeichnis 0700, `HOME`-Fallback über `getpwuid` |
| Daemon | Suspend-Datei ohne Eigentümerprüfung | `st_uid == geteuid()`, Modus `& 077 == 0`, `XDG_RUNTIME_DIR` muss privat sein |
| Daemon | Log-Injektion (Gerätename, `WM_CLASS`) | `sanitizeForLog()` (`src/log_sanitize.hpp`) |
| Daemon | Log-Flut durch Tastendrücke | nur noch mit `--debug` |
| X11 | IO-Fehlerhandler kehrt zurück (danach `exit()` aus Nebenthread) | gemeinsamer Handler in `src/xerrors.hpp`, `_exit(1)`; Installation idempotent |
| X11 | viele Kindfenster verlangsamen die Abfrage | höchstens 32 Kinder; `WM_CLASS` auf 256 Bytes gekürzt |
| Tray | Doppelte Keys, Groß-/Kleinschreibung im Merge, Speichern nach fehlgeschlagenem Einlesen, Beenden bei offenem Dialog | `setKey`/Entfernen auf alle Einträge, normalisierter Vergleich, Speichern übersprungen, `quitNow()` |
| systemd | zusätzliche Direktiven | `SystemCallFilter=~@resources` gesetzt; `ProtectProc`, `ProcSubset`, `IPAddressDeny` wirkungslos im User-Manager (`PrivateUsers=yes`), bewusst nicht gesetzt |

**Folgefehler und Korrektur (2026-10-15):** Mit dem eingeschränkten Schreibrecht scheiterte
„Konfigurationsdatei öffnen“: `xdg-open` (→ `kde-open5`) schreibt `~/.config/kde-open5rc` und
bekam „Configuration file … not writable“; ein in der Sandbox gestarteter Editor könnte
zudem nichts außerhalb des Config-Ordners speichern. Der Tray startet `xdg-open` deshalb über
`systemd-run --user --collect -p KillMode=process` als eigenen, ungesandboxten transienten
Dienst. Das nutzt bewusst dieselbe Fähigkeit wie der akzeptierte DBus-Escape (Abschnitt 3);
es kommt keine neue Möglichkeit hinzu, und Kommando und Pfad sind fest (kein Shell-Aufruf).
Geprüft: aus einer Sandbox (`ProtectHome=read-only`) ist `~/.config` nicht beschreibbar, im
transienten Dienst schon.

**Offen bzw. bewusst nicht geändert:**

- `/dev/uinput` ist auf dem Entwicklungsrechner `0666` (Altregel `/etc/udev/rules.d/99-uinput.rules`,
  siehe `packaging/README-uinput.md`) – Systemkonfiguration, nicht im Repo.
- **Behoben (2026-10-15):** Die Tray-Unit schrieb bisher in ganz `~/.config` (außer `systemd`,
  `autostart`, `environment.d`). Die Config liegt jetzt in `~/.config/dofscroll/`
  (`0700`); die Tray-Unit darf nur noch dieses Verzeichnis (+ `/run/user/UID`) beschreiben
  (`ConfigurationDirectory=` + `ReadWritePaths=%E/dofscroll %t`, empirisch geprüft:
  `touch ~/.config/zz-test` und Schreiben nach `~/.config/systemd/user` scheitern mit EROFS).
  Der Tray migriert die alte Datei `~/.config/spnav-scroll.conf` beim Start (Kopie, Legacy-Datei
  bleibt unangetastet); bis dahin liest der Daemon den Legacy-Pfad als Fallback.
- `WM_CLASS` ist für jeden X-Client fälschbar; das wirkt nur auf die Wahl eines bereits
  konfigurierten `[app:*]`-Profils.
- DBus-Escape (`systemd-run --user`), siehe Abschnitt 3: akzeptiert.
- Die CI soll bewusst den Floating-Tag `rockylinux:9` nutzen (GitHub-Actions-Workflow folgt).
- Nicht verfügbar lokal: `cppcheck`, `clang-tidy`, `valgrind`, ThreadSanitizer-Runtime,
  `libasan` (CI führt `cppcheck` und ASAN aus).

## Laufzeitdateien im Runtime-Dir (Daemon schreibt, Tray liest)

Der Daemon schreibt in `$XDG_RUNTIME_DIR` (privat, `0700`, dem Benutzer gehörig) zwei Dateien
(Format: `src/common.hpp`): `dofscroll-status` (Profil, Gerät, Programm, `enabled`, `paused`,
`devices_seen`, `updated`) und `dofscroll.log` (Kopie der Logzeilen, ≤ 300 Zeilen (im `--debug`-Modus ≤ 2000), rollend:
bei Überschreitung bleiben nur die neuesten Zeilen, zusätzlich ≤ 256 KiB).

- **Schreibrechte:** `ReadWritePaths=%t` in der Daemon-Unit. Das Runtime-Dir ist ohnehin nur
  für den Benutzer zugänglich; der Daemon legt dort nur diese Dateien (und eine kurzlebige
  `*.tmp.<PID>` der Status-Datei) an. Alles unter `~` und das restliche Dateisystem bleiben read-only.
- **Härtung im Code:** `XDG_RUNTIME_DIR` muss dem effektiven Benutzer gehören und darf nicht
  group/other-schreibbar sein, sonst wird nichts geschrieben. Status: Temp-Datei mit
  `O_CREAT|O_EXCL|O_NOFOLLOW`, `0600`, danach `rename`. Log: `O_APPEND|O_NOFOLLOW`, `0600`,
  Typ-/Besitzerprüfung per `fstat`. Schreibfehler sind nie fatal (Status: einmal loggen; Log: Spiegeln
  wird abgeschaltet) und blockieren den Tick-Thread nicht. Alle Fremdwerte (Gerätename,
  `WM_CLASS`, Profilbeschreibung) laufen durch `sanitizeForLog` (keine Zeilenumbrüche/Steuerzeichen).
- **Vertrauensmodell:** Der Tray liest die Dateien und behandelt ihren Inhalt wie fremde Daten
  (Größen-/Rechte-/Besitzerprüfung, Anzeige nur als Text). Wer die Dateien manipulieren kann,
  kann bereits als der Benutzer handeln (A2); sie enthalten keine Geheimnisse außer dem
  Namen des aktiven Programms.
- Beim sauberen Beenden entfernt der Daemon die Status-Datei; die Log-Datei bleibt (tmpfs, geht beim Abmelden weg).
- `devices_seen` liest `/proc/bus/input/devices` (reiner Lesezugriff, kein `/sys`).
