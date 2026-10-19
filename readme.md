# SpaceMouse C++ Zero-Lag Bridge – Technische Spezifikation & Dokumentation

Dieses Dokument dient als vollständiger technischer Kontext für die Entwicklung, den Betrieb, das Umziehen auf ein neues System und die Weiterentwicklung der SpaceMouse-Brücke unter Red Hat Enterprise Linux 9 (RHEL 9).

Der Quellcode selbst (`src/`) ist die einzige Quelle der Wahrheit für das *Verhalten* – hier im Dokument steht bewusst kein Code-Abzug, sondern nur Architektur, Verträge (Config-Keys, Dateipfade) und Betriebsanleitung, damit dieses Dokument nicht wieder von der Implementierung abdriften kann.

---

## 1. Überblick

Zwei getrennte Prozesse, ein gemeinsames Config-Format:

| Prozess | Quelle | Aufgabe |
|---|---|---|
| `dofscroll` | `src/dofscroll.cpp` (+ `config`, `dsp`, `output*`, `activewin`) | Liest die SpaceMouse über `spacenavd`/`libspnav` und erzeugt daraus Scroll-/Zoom-/Pfeiltasten-/Maustasten-Events über X11/XTest **oder** uinput (Wayland). Zeitkritisch, läuft dauerhaft im Hintergrund. |
| `dofscroll-tray` | `src/dofscroll_tray.cpp` | Tray-Icon (AppIndicator/StatusNotifierItem) mit Einstellungsfenster (GTK3). Schreibt `~/.config/dofscroll/dofscroll.conf` und stößt beim Daemon per SIGHUP einen Live-Reload an. |

Systemarchitektur:

- Betriebssystem: RHEL 9 (getestet auf Release 9.6).
- Hardware-Treiber: `spacenavd` (System-Dienst, aus EPEL) für Low-Level-Hardware-Kommunikation und LED-/Gerätezustände.
- Event-Prozessor (User-Space): `dofscroll`, klinkt sich über `libspnav` an `spacenavd`.
- Event-Injektion: X11/XTest (`XTestFakeButtonEvent`/`XTestFakeKeyEvent`) für Maus-Scroll (Buttons 4/5/6/7), Zoom (Strg+Scroll), Pfeiltasten (Nav-Modus) und linke Maustaste (Klick-Button).
- Architektur-Vorteil: Trennung von System-Treiber (`spacenavd`, root, systemweit) und User-Space-Übersetzer (`dofscroll`, `--user`-Dienst), was granulare Feinabstimmung pro Benutzer ohne Root-Rechte für die Logik erlaubt.

---

## 2. `src/dofscroll.cpp` – Scroll-/Zoom-Daemon

Modulaufteilung (Header in `src/*.hpp` sind der Vertrag zwischen den Teilen):

| Datei | Aufgabe |
|---|---|
| `config.*` | Dateiformat, `Settings`, Profile (`[device:*]`, `[app:*]`), Auflösung – von Daemon **und** Tray genutzt |
| `dsp.*` | Mix → Vorzeichen → Glättung → Deadzone → Kurve → Geschwindigkeit; Schritt-Akkumulator; Zoom-Erkennung (unit-getestet) |
| `output.*`, `output_x11.cpp`, `output_uinput.cpp` | Ereignis-Ausgabe: XTest oder uinput; `BACKEND = auto` wählt unter Wayland uinput, sonst X11 |
| `activewin.*` | aktives Programm (`WM_CLASS`) für `[app:*]`-Profile; unter KDE/Wayland optional über `kdotool` |
| `dofscroll.cpp` | Reader-Thread + Tick-Loop, Profilwechsel, Signale, `--debug` |


Architektur (Echtzeit-fähig, entkoppelt von der Event-Rate des Treibers):

- Ein **Reader-Thread** blockiert auf `spnav_wait_event` und schreibt nur den jeweils aktuellsten Achsen-/Button-Rohwert in lock-free `std::atomic`-Variablen. Er sendet selbst keine Events.
- Der **Haupt-Thread** läuft in einer festen Tickrate (`TICK_HZ`, Default 250 Hz, `std::chrono::steady_clock`) unabhängig von eingehenden Events und liest jedes Mal den aktuellen Zustand. Dadurch werden Scroll-Schritte gleichmäßig über die Zeit verteilt gesendet statt in Bursts – das behebt ruckeliges/springendes Scrollverhalten.
- Die Akkumulation ist **zeitbasiert** (`* dt`), nicht pro-Event, sodass die Scroll-Geschwindigkeit linear (bzw. über `RESPONSE_CURVE` nichtlinear) proportional zur Auslenkung bleibt.
- Ein Gleitkomma-Akkumulator (double, Schwelle 1.0) sorgt dafür, dass auch bei niedrigen Empfindlichkeiten keine Nachkommastellen verschluckt werden.
- Der Prozess reagiert auf **SIGHUP** und lädt die Config im Haupt-Thread neu (kein Neustart nötig) – genutzt vom Tray-Tool via `systemctl --user kill -s HUP dofscroll.service`.

### Nav-Button- und Klick-Button-Modus

- **Nav-Button**: solange `NAV_BUTTON` gehalten wird, werden X/Y-Auslenkungen als Pfeiltasten (Rauf/Runter/Links/Rechts) statt als Scroll-Events gesendet; Zoom ist währenddessen deaktiviert.
- **Klick-Button**: wird 1:1 (Press + Release) als linke Maustaste durchgereicht, inkl. Klicken-und-Halten zum Draggen.
- Beide Features haben einen eigenen Enable-Schalter (`NAV_BUTTON_ENABLED`, `CLICK_BUTTON_ENABLED`), unabhängig vom konfigurierten Button-Index – so lässt sich die Funktion komplett abschalten, statt nur einen unbenutzten Button-Index zu wählen.

### Zoom (Drehen)

Verdrehen der Kappe (`ry`) zoomt per Strg+Scroll. Achsenzuordnung (an einer SpaceMouse Pro mit einem Messprogramm bestätigt, libspnav-Konvention x rechts / y oben / z zum Nutzer): vertikal scrollen = `z + rx` (vor/zurück schieben oder kippen), horizontal = `x - rz` (seitlich schieben oder kippen), `y` (Heben/Drücken) wird ignoriert, weil es bei jedem Schieben stark mitschlägt. Strg wird während des Zoomens gehalten (nicht pro Schritt gedrückt). Der Zoom-Modus rastet ein: Einstieg, wenn die Drehung ≥ ~40 ms am Stück stärker ist als jede andere Einzelachse (x/z/rx/rz, Maximum statt Summe); danach bleibt er aktiv, bis die Drehung unter `ZOOM_DEADZONE` fällt oder eine andere Achse sie um das 1,5-fache übertrifft. Bei SIGTERM/SIGINT werden Strg und die Klick-Taste freigegeben. `dt` ist auf 50 ms begrenzt (kein Scroll-Burst nach Suspend). Diagnose: Dienst stoppen und `dofscroll --debug` im Terminal starten – gibt ~10×/s h/v/twist/lateral und die Zoom-Entscheidung aus.

### SpaceMouse Compact vs. Pro

Beide liefern dieselben 6 Achsen, deshalb passen die Standard-Mixes für beide; die Tasten unterscheiden sich (Compact 2, Pro 15; die umgebaute Pro des Autors hat keine). Beim Verbinden loggt der Daemon Gerätename/Tastenzahl, warnt bei nicht existierenden `NAV_BUTTON`/`CLICK_BUTTON` (nur wenn die jeweilige `*_ENABLED`-Option an ist) und loggt jeden Tastendruck mit Index (`journalctl --user -u dofscroll.service -f`) – so findet man die passende Nummer.

Belegung im Compact-Profil (`examples/dofscroll-compact.conf`):

| Taste | Index | Wirkung |
|---|---|---|
| links | `NAV_BUTTON = 0` | Gedrückt halten: Pfeiltasten statt Scroll (Nav-Modus), Loslassen: wieder normales Scrollen. |
| rechts | `CLICK_BUTTON = 1` | Wird 1:1 als linke Maustaste durchgereicht (Klicken, Halten, Ziehen). |

Beide sind dort über `NAV_BUTTON_ENABLED = 1` / `CLICK_BUTTON_ENABLED = 1` aktiv; bei der Pro bleiben sie aus. Die Zuordnung links = 0 / rechts = 1 ist eine Annahme und an der Hardware zu bestätigen – sind die Tasten vertauscht, die beiden Nummern tauschen.

### Verbindungs-Resilienz (kein `spacenavd` nötig zum Starten, Hot-Plug-fest)

- `dofscroll` verlässt sich für den Hardware-Zugriff bewusst komplett auf `spacenavd` – es kann und soll `spacenavd` nicht ersetzen. Was es aber tut: **niemals hart abstürzen**, nur weil `spacenavd` beim Start noch nicht (oder gerade nicht mehr) erreichbar ist.
- **Start ohne laufenden `spacenavd` / ohne angeschlossene SpaceMouse:** `initialize()` öffnet nur das X11-Display; die Verbindung zu `spacenavd` übernimmt der Reader-Thread über `connectWithRetry()` mit exponentiellem Backoff (0.5s → 1s → 2s → 4s, gedeckelt bei 5s). Der systemd-Dienst wird dadurch sofort `active (running)`, statt wiederholt mit `status=1/FAILURE` zu terminieren und in `StartLimitBurst` zu laufen.
- **Verbindungsabbruch zu `spacenavd`** (Daemon neugestartet/abgestürzt): `spnav_wait_event` liefert `0`, der Reader-Thread schließt die Verbindung, setzt den Auslenkungszustand sofort auf 0 zurück und ruft erneut `connectWithRetry()` auf – vollautomatisch, kein Prozess-Neustart nötig.
- **Physisches An-/Abstecken der SpaceMouse** ist Aufgabe von `spacenavd` (Hotplug per udev/`AF_NETLINK`) – die Client-Socket-Verbindung von `dofscroll` bleibt davon unberührt, es kommen einfach keine/wieder Motion-Events.
- Zusätzliches Sicherheitsnetz: ein **Freshness-Watchdog** (`last_motion_ns`, `STALE_NS` = 250ms) behandelt die Auslenkung als 0, sobald seit über 250ms kein neues Motion-Event mehr kam. Verhindert "hängendes" Endlos-Scrollen bei abgezogenem Kabel, `spacenavd`-Absturz oder einem einzelnen verlorenen Event.
- **Backoff über Verbindungsverluste hinweg:** der Wartezeit-Zähler (0,5 s → 5 s) bleibt zwischen Verbindungsversuchen erhalten und wird nur zurückgesetzt, wenn die Verbindung Events geliefert hat oder ≥ 10 s hielt – eine sofort wieder schließende Verbindung führt so nicht zu einer Schleife ohne Pause.
- **Fensterabfrage (`[app:*]`) entkoppelt:** X11-Roundtrips und `kdotool` laufen in einem Hintergrund-Thread (alle 100 ms, `kdotool` 250 ms mit 200-ms-Limit); der Tick-Thread liest nur den Zwischenspeicher und kann dadurch nie blockieren (kein hängendes Strg). `main` ruft dafür `XInitThreads()` auf.
- **Gerät entfernt:** meldet `spacenavd` `SPNAV_DEV_RM`, werden Achsen und Tastenmaske gelöscht (gehaltene Klick-Taste wird losgelassen). Bewusst kein Timeout: eine gehaltene Taste bei ruhigem Puck erzeugt keine Events und darf nicht loslassen.
- **Hängende Tasten nach Kill/Absturz:** Beim Start gibt der Daemon Strg, Pfeiltasten und die linke Maustaste frei (`forceReleaseAll`), und die Unit führt als `ExecStopPost` `dofscroll --release-keys` aus (öffnet nur die Ausgabe, gibt alles frei, endet).
- **Config-Reload:** ein Reload ohne tatsächlich geänderte Werte (Datei nur berührt, identischer Inhalt, fehlgeschlagenes Laden) lässt gehaltene Tasten unberührt; ändert sich das Profil, wird nur Strg freigegeben, ein gehaltener Klick bleibt gehalten.
- Alle Verbindungsereignisse landen auf `stderr`, unter systemd automatisch im Journal: `journalctl --user -u dofscroll.service`.
- **Status- und Log-Datei (für den Tray):** der Daemon schreibt in `$XDG_RUNTIME_DIR` die Datei `dofscroll-status` (`key=value`: `profile`, `device`, `app`, `enabled`, `paused`, `devices_seen`, `updated`; atomar ersetzt bei Änderung, mindestens 1x/s für `updated`; beim sauberen Beenden entfernt) und `dofscroll.log` (Kopie der Logzeilen mit Zeitstempel, rollend: höchstens 300 Zeilen, danach bleiben nur die neuesten 200; im `--debug`-Modus bis 2000 Zeilen). Ohne `XDG_RUNTIME_DIR` oder bei Schreibfehlern passiert nichts (nie fatal). Format: `src/common.hpp`, Rechte: `docs/security.md`.
- **Mehrere SpaceMice:** `spacenavd` meldet bei zwei angesteckten Geräten nur eines (gemessen: Pro + Compact → nur „SpaceMouse Pro“); `[device:*]` gilt dann für beide, und die Tasten beider Geräte teilen sich die Indizes. Der Daemon zählt angesteckte 3Dconnexion-Geräte aus `/proc/bus/input/devices` (nach jedem (Re-)Connect und alle ~10 s, `src/device_count.*`), loggt bei ≥ 2 einmal je Änderung einen Hinweis und schreibt die Zahl als `devices_seen` in die Status-Datei.

---

## 3. `src/dofscroll_tray.cpp` – Tray-Icon & Einstellungsfenster

Bewusst ein **getrennter Prozess**, damit GTK-Mainloop und Tray-Overhead nie den zeitkritischen Scroll-Tick-Thread des Daemons beeinflussen. Technologie: GTK3 + libappindicator3 (funktioniert unter KDE Plasma nativ über StatusNotifierItem, unter GNOME ggf. AppIndicator-Extension nötig).

- Rechtsklick-Menü am Tray-Icon: "Einstellungen…" öffnet ein Fenster mit Reglern/Spinboxes für alle Config-Werte (Sensitivities, Deadzones, Invert, Response-Kurve, Nav-Button, Klick-Button inkl. je einer Enable-Checkbox), "Konfigurationsdatei öffnen" (der Editor wird per `systemd-run --user` außerhalb der Tray-Sandbox gestartet), "Daemon neu starten" und "Beenden" (schließt nur die Tray-App).
- Jede Änderung wird (debounced, 250ms nach der letzten Eingabe) in `~/.config/dofscroll/dofscroll.conf` geschrieben und der laufende `dofscroll`-Dienst per `systemctl --user kill -s HUP dofscroll.service` zum Live-Reload angestoßen – ohne Neustart, ohne Scroll-Unterbrechung.
- Die Tray-App besitzt keine eigene Config-Logik-Duplikation im Sinne einer zweiten Quelle der Wahrheit – sie liest/schreibt exakt dasselbe Dateiformat wie der Daemon.
- Eigenes Icon (`icons/dofscroll-tray.svg`): dunkler Puck (Draufsicht auf die SpaceMouse-Kappe) mit hellem 4-Wege-Pfeil in der Mitte – symbolisiert, dass die Auslenkung in jede Richtung zu Scroll/Pan wird. Der Akzent-Punkt in der Mitte steht für die Nullstellung.
  - Der Icon-Ordner wird zur Laufzeit gesucht: `icons/` neben dem aufgelösten Binary (Repo), dann `~/.local/share/dofscroll-tray/icons`, dann `/usr/local/share/dofscroll-tray/icons`. Ein toter Symlink unter `/usr/local` ist damit kein Problem mehr. Das Fensteric­on wird per `gtk_window_set_default_icon_from_file()` gesetzt (`_NET_WM_ICON`), das Design ist ein heller Puck mit dunklem Rand/Pfeil, damit es im Dark- und Light-Mode erkennbar bleibt.
  - Für den Tray selbst über `app_indicator_set_icon_theme_path()`/`app_indicator_set_icon_full()` geladen, mit `input-mouse` als Fallback-Namen falls das Custom-Icon fehlt.
  - Für das Einstellungsfenster wird derselbe Icon-Theme-Pfad zusätzlich per `gtk_icon_theme_append_search_path()` registriert und mit `gtk_window_set_default_icon_name()` als Standard-Fenstericon gesetzt – ohne das würde das Fenster in der Taskleiste das generische GTK-Logo zeigen, obwohl der Tray bereits das eigene Icon nutzt.

Das Einstellungsfenster hat zwei Reiter:

- **Kanäle:** Profilwahl und alle Werte je Kanal (Mix, Empfindlichkeit, Deadzone, Richtung, Kurve mit Vorschau, Glättung), Backend, Tickrate, Tasten. Die Zoom-Erkennung (`ZOOM_LATERAL`, `ZOOM_ENTER_MS`, `ZOOM_EXIT_RATIO`) hat keine Regler und wird in der Datei gepflegt.
- **Achsen:** (Solange dieser Reiter im **aktiven** Tray-Fenster offen ist oder der Assistent läuft, pausiert der Daemon seine Ausgabe – kein Scrollen, Zoomen, keine Pfeiltasten, keine Klicks –, damit die Bewegungen nur gemessen werden. Mechanismus: der Tray berührt ca. alle 150 ms die Datei `$XDG_RUNTIME_DIR/dofscroll-suspend`, der Daemon prüft sie ~20×/s und pausiert, solange sie jünger als 600 ms ist; Tab-Wechsel, Fensterwechsel oder Schließen entfernen sie sofort, ein abgestürzter Tray hebt die Pause nach spätestens 600 ms auf. Ohne `XDG_RUNTIME_DIR` gibt es keine Pause. Der Daemon loggt „Ausgabe pausiert/wieder aktiv“.) Skizze der Kappe mit den Achsen `x`, `y`, `z`, `rx`, `ry`, `rz` (der gerade gewählte Kanal wird hervorgehoben), Live-Balken aller sechs Achsen (der Tray öffnet dafür selbst eine lesende libspnav-Verbindung, nur solange das Fenster offen ist) und der Assistent **„Achsen einlernen“**: Man führt fünf Bewegungen aus (je ca. 3 s Messzeit; die beiden Kipp-Schritte sind optional und überspringbar; zu dir ziehen, vor/zurück kippen, seitlich schieben, seitlich kippen, drehen), der Assistent wählt je Bewegung die betragsmäßig stärkste Achse samt Vorzeichen und trägt `SCROLL_V_MIX`, `SCROLL_H_MIX` und `ZOOM_MIX` im gewählten Profil ein. Die Auswertung steckt in `src/axis_wizard.hpp` (unit-getestet). Welche Zahl physisch welcher Bewegung entspricht, meldet das Gerät nicht – deshalb wird gemessen.

---

## 4. Konfigurationsdatei (`~/.config/dofscroll/dofscroll.conf`)

Liegt im eigenen Verzeichnis `~/.config/dofscroll/` (Modus `0700`; die Tray-Unit darf nur dieses Verzeichnis beschreiben, siehe `docs/security.md`). Solange diese Datei fehlt, fällt `defaultConfigPath()` auf die alten Pfade `~/.config/spnav-scroll/spnav-scroll.conf` bzw. `~/.config/spnav-scroll.conf` zurück (siehe „Migration“ unten). Wird von **beiden** Prozessen über dieselbe Bibliothek (`src/config.cpp`) gelesen/geschrieben, ist **nicht** Teil dieses Repos (rein benutzerspezifisch). Fehlt die Datei, starten beide mit Defaults. Alte flache Dateien (`SCROLL_SENSITIVITY_Y` usw.) bleiben lesbar; beim ersten Speichern über den Tray werden sie auf die neuen Namen umgestellt (Kommentare gehen beim Speichern verloren).

### Migration aus der Zeit vor `dofscroll` (Projektname `spnav-scroll`)

Das Projekt hieß früher `spnav-scroll`; die Config lag zuerst direkt als `~/.config/spnav-scroll.conf`, danach als `~/.config/spnav-scroll/spnav-scroll.conf`. Der aktuelle Pfad `~/.config/dofscroll/dofscroll.conf` erlaubt der Tray-Unit, nur noch ein eigenes Verzeichnis statt ganz `~/.config` zu beschreiben. Es ist nichts manuell zu tun (der vorherige Ort hat Vorrang vor dem ältesten):

- Der **Tray** übernimmt die alte Datei beim Start: er kopiert sie in den neuen Pfad (Verzeichnis `0700`). Ist die alte Datei ein Symlink (z.B. in ein Dotfile-Repo), wird der Symlink im neuen Verzeichnis neu gesetzt statt Inhalt zu kopieren.
- Die **Legacy-Datei bleibt unangetastet**. Nach einer Kontrolle (Einstellungen im Tray prüfen) kann sie gelöscht werden: `rm -r ~/.config/spnav-scroll ~/.config/spnav-scroll.conf`.
- Der **Daemon** liest bis dahin den Legacy-Pfad als Fallback und folgt dem Wechsel auf den neuen Pfad innerhalb von ca. 1 s, ohne Neustart. Auch ohne laufenden Tray (oder wenn die Migration scheitert) funktioniert er damit unverändert.
- Beim ersten Start nach dem Update muss die Tray-Unit neu geladen werden (`systemctl --user daemon-reload && systemctl --user restart dofscroll-tray.service`), damit die geänderten Sandbox-Direktiven (`ConfigurationDirectory=`, `ReadWritePaths=%E/dofscroll %t`) gelten; sonst kann der alte Prozess nicht in das neue Verzeichnis schreiben.

### Kanäle und Achsen

Drei Kanäle, je mit eigenem Achsen-Mix, Empfindlichkeit, Deadzone, Richtung, Kurve und Glättung: `SCROLL_V` (vertikal), `SCROLL_H` (horizontal), `ZOOM`.

| Key (je Kanal `SCROLL_V_`, `SCROLL_H_`, `ZOOM_`) | Default V / H / Zoom | Bedeutung |
|---|---|---|
| `*_MIX` | `z + rx` / `x - rz` / `ry` | Achsen-Ausdruck: Summe aus `[+-][Gewicht*]Achse`, Achsen `x y z rx ry rz`, z.B. `0.5*rx + z`. |
| `*_SENS` | `5` / `3` / `5` | Empfindlichkeit 0–255, `0` = Kanal aus. (Zoom: `ZOOM_SENS`) |
| `*_DEADZONE` | `30` / `20` / `40` | Deadzone in Rohsensor-Einheiten. |
| `*_SIGN` | `1` | `1` oder `-1` – Richtung. |
| `*_CURVE` | `1.0` | Exponent (`>1`: feiner bei leichtem Druck, schneller bei starkem) **oder** frei definierte Punktliste `"0:0 50:0.1 200:1 350:3"` (Eingabe nach Deadzone : Faktor auf die lineare Geschwindigkeit, linear interpoliert). |
| `*_SMOOTH_MS` | `0` | Glättung (Tiefpass-Zeitkonstante in ms, tickratenunabhängig), `0` = aus. |

Achsen am Gerät (an einer SpaceMouse Pro gemessen, libspnav-Konvention): Schieben vor/zurück = `z`, seitlich = `x`, Heben/Drücken = `y`; Kippen vor/zurück = `rx`, seitlich = `rz`; Drehen der Kappe = `ry`. Bei anderer Hardware oder anderem `spnavrc` (z.B. `swap-yz`) einfach die `*_MIX`-Zeilen anpassen; `dofscroll --debug` bzw. ein Messlauf zeigt, welche Achse was liefert.

### Zoom-Erkennung, Backend, Buttons

| Key | Default | Bedeutung |
|---|---|---|
| `ZOOM_LATERAL` | `x z rx rz` | Achsen, gegen die der Zoom-Twist dominieren muss (Maximum, nicht Summe). |
| `ZOOM_ENTER_MS` | `40` | So lange muss der Twist dominieren, bevor der Zoom einrastet. |
| `ZOOM_EXIT_RATIO` | `1.5` | Zoom endet, wenn eine andere Achse den Twist um diesen Faktor übertrifft. |
| `BACKEND` | `auto` | `auto` / `x11` / `uinput`. `auto` wird beim Start des Daemons aufgelöst (Wayland → uinput, sonst X11/XTest; schlägt das gewählte Backend fehl, wird das andere versucht und geloggt); nur ein Wechsel dieses Keys braucht einen Neustart des Daemons, alle anderen Keys wirken live. |
| `TICK_HZ` | `250` | Tickrate 30–1000. |
| `NAV_BUTTON`, `NAV_BUTTON_ENABLED` | `0`, `0` | Taste für Pfeiltasten-Modus (solange gehalten); an der Compact aktivieren, an einer Pro ohne Tasten aus lassen. |
| `CLICK_BUTTON`, `CLICK_BUTTON_ENABLED` | `1`, `0` | Taste, die als linke Maustaste durchgereicht wird. |

### Profile pro Gerät und Programm

Wertebereiche: `*_SENS` 0–255, `*_SMOOTH_MS` 0–500, `*_CURVE`-Exponent 0,1–10; Kurvenpunkte streng aufsteigend (Eingabe ≤ 100000, Faktor ≤ 100); Mix-Gewichte ≤ 100; `ZOOM_EXIT_RATIO` ≥ 1,0. Alle `[device:*]`-Abschnitte werden vor allen `[app:*]`-Abschnitten angewandt (je in Dateireihenfolge).

Fertige Beispieldateien: `examples/dofscroll-pro.conf` (SpaceMouse Pro, Tasten aus) und `examples/dofscroll-compact.conf` (SpaceMouse Compact, beide Tasten aktiv: links = Pfeiltasten-Modus, rechts = Linksklick). `examples/dofscroll-multi-device.conf` ist eine kombinierte Datei mit beiden Geräteprofilen für Betrieb mit wechselnden Mäusen.

    [device:SpaceMouse Pro]     # greift, wenn der Gerätename diesen Text enthält
    SCROLL_V_SENS = 8

    [app:firefox]               # greift, wenn "instanz klasse" (WM_CLASS) diesen Text enthält
    ZOOM_SENS = 12
    SCROLL_V_CURVE = 0:0 200:1 350:3

**Alles für ein Programm ausschalten:** `ENABLED = 0` (Default `1`, in jeder Sektion überschreibbar). Der Daemon erzeugt dann keinerlei Ausgabe (Scroll, Zoom, Pfeiltasten, Klick) und lässt gehaltene Tasten los, auch beim Programmwechsel; jeder Wechsel wird geloggt („Steuerung deaktiviert (Profil …)“ / „wieder aktiv“). Sinnvoll für 3D-Programme wie Blender oder CAD, die die SpaceMouse selbst auswerten und sich sonst mit dieser Bridge überlagern würden:

    [app:blender]
    ENABLED = 0

**Timeline-Modus (Videoschnitt, z. B. DaVinci Resolve):** `MODE = timeline` (Default `normal`, in jeder Sektion überschreibbar, im Tray unter „Allgemein“ → „Modus“). Seitliches Schieben/Kippen (Kanal `SCROLL_H_*`) = **Scrubben**, Drehen der Kappe (Kanal `ZOOM_*`) = **Einzelbilder**; beides sendet Pfeil links/rechts, die Frames pro Sekunde folgen analog der Auslenkung (wie im Nav-Modus: Deadzone, Kurve, Glättung und Empfindlichkeit der jeweiligen Kanäle gelten). Kein Scrollen, kein Zoom (Strg bleibt los), vertikales Schieben wird ignoriert, Nav-Button ist aus. Die Summe beider Kanäle wird als Pfeiltasten gesendet. Die Richtung der Drehung lässt sich mit `ZOOM_SIGN = -1` drehen. Als Programm-Profil (Beispiel in `examples/dofscroll-pro.conf`):

    [app:resolve]
    MODE = timeline
    SCROLL_H_SENS = 4     # Scrubben
    ZOOM_SENS = 1         # Einzelbilder (feiner als der Standard)

Das Muster (`WM_CLASS`-Teilstring) steht im Tray-Menü unter „Programm:“. Voraussetzung in Resolve: die Timeline (bzw. der Viewer) hat den Tastaturfokus, dort springen Pfeil links/rechts um ein Bild.

**Warum `[global]` bleibt:** Geräteabschnitte enthalten nur Überschreibungen; alles andere (Mixes, Empfindlichkeiten, Kurven …) kommt aus `[global]`. Es ist außerdem der Fallback für ein Gerät ohne passenden Abschnitt und die Basis für `[app:*]`. `BACKEND` wird beim Start aus `[global]` gelesen und gilt für den ganzen Daemon; `TICK_HZ` wird bei jedem Profilwechsel neu aufgelöst und kann daher pro Gerät oder Programm abweichen.

**Gerät wechseln:** Es gibt nichts umzuschalten – der Daemon wählt das `[device:*]`-Profil anhand des Gerätenamens, den `spacenavd` meldet (Teilstring, case-insensitiv). Compact anstecken genügt; der Profilwechsel wird geloggt (`journalctl --user -u dofscroll.service -f`), dort steht auch der tatsächliche Gerätename. Neu einlernen ist nicht nötig, solange die Achsen-Mixes stimmen; sonst im Tray unter „Achsen“ den Assistenten mit dem Compact-Profil laufen lassen.

**Beispieldateien** (`examples/`, alle von `make check-config` bzw. CI validiert; zum Übernehmen nach `~/.config/dofscroll/dofscroll.conf` kopieren, vorher `mkdir -p ~/.config/dofscroll`):

| Datei | Inhalt |
|---|---|
| `dofscroll-pro.conf` | SpaceMouse Pro, Tasten aus. |
| `dofscroll-compact.conf` | SpaceMouse Compact, beide Tasten aktiv. |
| `dofscroll-multi-device.conf` | Kombinierte Datei mit beiden Geräteprofilen für wechselnde Mäuse. |

**Grenzen beim Laden:** Datei ≤ 1 MiB, Zeile ≤ 8 KiB, ≤ 256 Abschnitte, ≤ 512 Keys je Abschnitt, ≤ 12 Mix-Terme, ≤ 64 Kurvenpunkte; doppelte Keys in einem Abschnitt werden zusammengeführt (der letzte gewinnt); nicht reguläre Dateien (FIFO, Verzeichnis) werden abgelehnt. Ein relativer `XDG_CONFIG_HOME` wird ignoriert.

**Speichern (Tray):** schreibt atomar (temp + `fsync` + `rename`), löst einen Symlink auf und schreibt am echten Ziel, behält den Dateimodus (neue Datei `0600`). Wurde die Datei zwischenzeitlich von Hand geändert, lädt der Tray sie vor dem Speichern neu und wendet nur die eigenen Änderungen darauf an. Profilnamen dürfen kein `]` und keine Zeilenumbrüche enthalten; ein UTF-8-BOM am Dateianfang wird ignoriert.

**Format:** Maßgeblich ist das neue Format mit `[global]` und den Kanal-Keys (`SCROLL_V_SENS` …). Die alte flache Datei (`SCROLL_SENSITIVITY_Y`, `INVERT_Y`/`INVERT_X` (nur `1` oder `-1`, Alias für `*_SIGN`) …) wird weiter gelesen; `ZOOM_INVERT = 1` entspricht `ZOOM_SIGN = -1`. Beim ersten Speichern im Tray wird sie ins neue Format umgeschrieben (Kommentare gehen verloren).

Auflösung: `[global]` → passende `[device:*]` → passende `[app:*]` (Dateireihenfolge, spätere gewinnen; Vergleich case-insensitiv per Teilstring). Sektionen enthalten nur die überschriebenen Keys. Den Programmnamen ermittelt man mit `xprop WM_CLASS` (Fenster anklicken); der Daemon loggt jeden Profilwechsel. Der Daemon lädt die Datei bei SIGHUP **und** automatisch bei Änderung der Datei neu.

Programm-Profile brauchen eine Möglichkeit, das aktive Fenster zu lesen: unter X11 immer, unter Wayland nur mit `kdotool` (KDE) – sonst gelten nur `[global]` und `[device:*]`.

Am einfachsten über das Tray-Einstellungsfenster pflegen (Profil-Auswahl, Kurvenvorschau); manuelles Editieren ist gleichwertig.

---

## 5. Build

Benötigte Pakete (RHEL 9 + EPEL):

    sudo dnf install -y epel-release   # falls EPEL noch nicht aktiv ist
    sudo dnf install -y gcc-c++ make spacenavd libspnav-devel \
        libX11-devel libXtst-devel \
        gtk3-devel libappindicator-gtk3-devel

Bauen mit `make` (Ausgabe wie bisher im Repo-Root: `dofscroll`, `dofscroll-tray`):

    make check-deps   # prüft Abhängigkeiten, nennt ggf. das dnf-Kommando
    make -j4          # beide Binaries
    make daemon       # nur der Daemon (ohne GTK)
    make test         # Unit- und Szenario-Tests (config, dsp, axis_wizard, scenarios)
    make clean

Weitere Ziele: `install`/`uninstall` (PREFIX, DESTDIR), `install-udev` (uinput-Regel, siehe `packaging/README-uinput.md`), `install-user-links` (gibt nur die Symlink-Befehle aus Abschnitt 6 aus). CI: ein GitHub-Actions-Workflow ist geplant (Image `rockylinux:9`, Jobs `build` und `test`).

---

## 6. Installation / Einrichtung

Grundprinzip: **nichts wird nach `/usr/local` oder `~/.config/systemd/user` kopiert** – überall liegen nur Symlinks zurück ins geklonte Repo. Damit ist das Repo-Arbeitsverzeichnis die einzige Quelle der Wahrheit; ein Rebuild oder ein `git pull` wirkt sofort, ohne dass irgendwo eine veraltete Kopie zurückbleiben kann.

`/usr/local/bin` und `/usr/local/share` gehören root – daher mit `sudo`:

    sudo ln -s "$(pwd)/dofscroll" /usr/local/bin/dofscroll
    sudo ln -s "$(pwd)/dofscroll-tray" /usr/local/bin/dofscroll-tray
    sudo mkdir -p /usr/local/share/dofscroll-tray/icons
    sudo ln -s "$(pwd)/icons/dofscroll-tray.svg" /usr/local/share/dofscroll-tray/icons/dofscroll-tray.svg

systemd-Units (liegen als Vorlage in `systemd/` in diesem Repo) verlinken statt kopieren:

    mkdir -p ~/.config/systemd/user
    ln -s "$(pwd)/systemd/dofscroll.service" ~/.config/systemd/user/dofscroll.service
    ln -s "$(pwd)/systemd/dofscroll-tray.service" ~/.config/systemd/user/dofscroll-tray.service

`spacenavd` (System-weiter Treiber-Dienst, kommt mit dem Paket) einmalig aktivieren:

    sudo systemctl enable --now spacenavd

Die beiden User-Dienste aktivieren – sie starten danach automatisch mit jeder grafischen Sitzung:

    systemctl --user daemon-reload
    systemctl --user enable --now dofscroll.service
    systemctl --user enable --now dofscroll-tray.service

Config-Datei `~/.config/dofscroll/dofscroll.conf` anlegen (Verzeichnis legt die Tray-Unit per `ConfigurationDirectory=` selbst an, manuell: `mkdir -p -m 0700 ~/.config/dofscroll`) (Werte nach Bedarf anpassen, siehe Abschnitt 4) – entweder manuell oder einfach über das Tray-Einstellungsfenster einmal durch die Regler klicken; das Fenster schreibt die Datei beim ersten Ändern eines Werts automatisch an. Diese Datei bleibt bewusst eine echte Kopie, kein Symlink – sie ist rein benutzerspezifisch und nicht Teil des Repos (siehe Abschnitt 4).

**Aktualisieren nach einer Code-Änderung:** dank der Symlinks reicht ein Rebuild im Repo-Verzeichnis – kein erneutes `cp`/`ln` nötig. Der laufende Prozess hält die Binärdatei aber weiter offen (`Text file busy`), daher vor dem Neubauen stoppen:

    systemctl --user stop dofscroll.service dofscroll-tray.service
    make -j4
    systemctl --user start dofscroll.service dofscroll-tray.service

---

## 7. Umzug auf ein neues System

Checkliste für ein frisches RHEL-9-System (oder eine Neuinstallation):

1. **Repo klonen**

       git clone https://github.com/HoZe-Lukas/dofScroll.git ~/dofscroll
       cd ~/dofscroll-cpp

2. **Abhängigkeiten installieren** und **beide Binaries bauen** (siehe Abschnitt 5).

3. **Installieren** (Symlinks für Binaries/Icon/systemd-Units setzen, `spacenavd` aktivieren; siehe Abschnitt 6). Da alles nur verlinkt statt kopiert wird, muss der Klon-Pfad aus Schritt 1 danach stabil bleiben – wird das Repo-Verzeichnis später verschoben oder umbenannt, zeigen alle Symlinks unter `/usr/local` und `~/.config/systemd/user` ins Leere und müssen neu gesetzt werden.

4. **Config übernehmen.** `~/.config/dofscroll/dofscroll.conf` ist bewusst nicht Teil des Repos (rein benutzerspezifisch). Optionen:
   - Alte Datei vom bisherigen System nach `~/.config/dofscroll/dofscroll.conf` kopieren (z.B. per `scp`; vorher `mkdir -p -m 0700 ~/.config/dofscroll`; eine alte Datei `~/.config/spnav-scroll/spnav-scroll.conf` bzw. `~/.config/spnav-scroll.conf` wird vom Tray beim Start automatisch übernommen), dann `systemctl --user kill -s HUP dofscroll.service` – **oder**
   - Ohne Kopie starten: beide Prozesse laufen dann mit den Default-Werten aus Abschnitt 4, Feinabstimmung anschließend über das Tray-Einstellungsfenster.

5. **User-Dienste aktivieren** (siehe Abschnitt 6, letzter Block) und einmal ab-/anmelden bzw. `graphical-session.target` erneut erreichen, damit `WantedBy=graphical-session.target` greift.

6. **Verifizieren:**
   - SpaceMouse anschließen, `systemctl --user status dofscroll.service` → `active (running)`.
   - Tray-Icon sichtbar (unter GNOME ggf. AppIndicator-Extension installieren/aktivieren), Icon identisch zu dem im Einstellungsfenster/Taskleiste.
   - Scrollen/Zoomen/Nav-Modus/Klick-Button in einem beliebigen Fenster testen.
   - Bei Problemen: `journalctl --user -u dofscroll.service -f` und `journalctl --user -u dofscroll-tray.service -f`.

7. **Autostart-Falle vermeiden:** `dofscroll-tray.service` hat `After=... dofscroll.service` und `PartOf=graphical-session.target`, braucht also eine laufende grafische Sitzung – ein reiner SSH-Login ohne grafische Session startet ihn nicht (das ist beabsichtigt, nicht anpassen, ohne die `After`/`PartOf`-Kette zu verstehen).

---

## 8. Qualität und Sicherheit („Quality Gate“)

- `make check` ist das zentrale Gate (lokal ohne Root): `-Werror`-Build, Unit-/Szenario-Tests, Sanitizer (ASAN/UBSAN, lokal übersprungen wenn libasan fehlt), `cppcheck`, Validierung von `examples/dofscroll-pro.conf`, gehärteter Build und Hardening-Check (`scripts/check-hardening.sh`: PIE, Full RELRO, NX, Stack-Protector, FORTIFY). `make check-fast` ist die schnelle Variante (~4 s).
- Alle Binaries werden gehärtet gebaut (`HARDEN_CXXFLAGS`/`HARDEN_LDFLAGS` im Makefile). Nach einem Wechsel der Flags einmal `make clean`.
- CI: ein GitHub-Actions-Workflow ist geplant (`build`, `test`, `check` mit `REQUIRE_ASAN=1`, Konfigurations-Selbsttests, Secret-Scan; als Pflicht-Check für `main`, siehe `docs/security.md`).
- Pre-commit-Hook: `scripts/install-hooks.sh` (führt `make check-fast` und einen Secret-Scan aus).
- `docs/security.md`: Bedrohungsmodell, Befunde mit Schweregrad, systemd-Härtung (die Units in `systemd/` sind gehärtet; wirksam erst nach `systemctl --user daemon-reload` + Neustart, Rückweg per Drop-in siehe dort) und `uinput`-Rechte.

## 9. Windows und macOS

Dort läuft der offizielle 3Dconnexion-Treiber; die Möglichkeiten (Anwendungsprofile, Achsen, Empfindlichkeit) und offene Punkte stehen in `docs/windows-mac.md`.

## 10. Zukünftige Verbesserungsmöglichkeiten

1. Erweiterte Tastenbelegung für SpaceMouse-Zusatztasten (z.B. für Shortcuts) – die Compact nutzt ihre zwei Tasten bereits für Nav-Modus und Klick; die Pro hat 15 Tasten, die noch unbelegt sind.
2. Aktives-Fenster-Erkennung für native Wayland-Fenster ohne `kdotool` (z.B. über KWin-Scripting).

## Lizenz

Noch nicht festgelegt (keine `LICENSE`-Datei; ohne Lizenz gilt "alle Rechte vorbehalten"). Die Kennung
steht zentral in `packaging/LICENSE_ID`; Vorbereitung und offene Punkte vor einer Veroeffentlichung:
`docs/lizenz-vorbereitung.md`. "3Dconnexion" und "SpaceMouse" sind Marken ihrer Inhaber; dieses Projekt
ist nicht mit 3Dconnexion verbunden.

## Lizenz und Marken

dofscroll steht unter der **GNU General Public License, Version 3 oder (nach Wahl) jeder späteren Version** (SPDX: `GPL-3.0-or-later`), siehe `LICENSE`.

„3Dconnexion“ und „SpaceMouse“ sind Marken der 3Dconnexion GmbH bzw. ihrer Rechtsinhaber. dofscroll ist ein unabhängiges Projekt und wird von 3Dconnexion weder unterstützt noch ist es mit dem Unternehmen verbunden; die Namen werden nur beschreibend verwendet, um die unterstützte Hardware zu benennen.
