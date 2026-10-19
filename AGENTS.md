# AGENTS.md

Kontext für KI-Coding-Agents (Claude Code, Codex, Cursor, ...), die an diesem Repo
arbeiten. Für die vollständige Architektur/Betriebsanleitung siehe `readme.md` - dieses
Dokument ist nur die Kurzorientierung.

## Was das hier ist

Ein kleiner, in zwei getrennten Prozessen laufender C++-Treiber-Bridge für eine
3Dconnexion-SpaceMouse unter Linux (X11, RHEL 9 getestet): der Daemon (`src/dofscroll.cpp`) liest die
Maus über `spacenavd`/`libspnav` und erzeugt daraus X11-Scroll-/Zoom-/Pfeiltasten-/
Maustasten-Events (250 Hz Tick-Rate (`TICK_HZ`), Reader-Thread + Haupt-Thread entkoppelt). Die zweite
Komponente, `src/dofscroll_tray.cpp`, ist ein GTK3+AppIndicator-Tray-Icon mit
Einstellungsfenster, das dieselbe Config-Datei schreibt und dem Daemon per SIGHUP ein
Live-Reload anstößt. Build per `make` (siehe unten), CI folgt als GitHub-Actions-Workflow.

## Wo was liegt

| Pfad | Zweck |
|---|---|
| `src/` | Quellen: `dofscroll.cpp` (Daemon), `dofscroll_tray.cpp` (Tray), `config`/`dsp` (geteilt bzw. unit-getestet), `output*` (X11/uinput), `activewin`; Header sind der Vertrag |
| `tests/` | Unit-Tests (`make test`) |
| `examples/` | Beispiel-Configs: `dofscroll-pro.conf`, `-compact.conf`, `-multi-device.conf` (CI-validiert) |
| `scripts/` | Pre-commit-Hook, Hardening-Check |
| `Makefile` | Build (CI-Workflow folgt) |
| `docs/windows-mac.md`, `packaging/` | Treiber-Recherche Windows/Mac; udev-Regel für uinput |
| `dofscroll`, `dofscroll-tray` | Vorgebaute Binaries, committed im Repo-Root (siehe "Warnung" unten) |
| `icons/dofscroll-tray.svg` | Custom-Tray-Icon |
| `systemd/dofscroll.service`, `systemd/dofscroll-tray.service` | User-systemd-Units, werden per Symlink (nicht Kopie) nach `~/.config/systemd/user/` eingebunden |
| `readme.md` | Vollständige technische Doku (Architektur, Config-Keys, Install/Umzug-Anleitung) - **einzige Quelle der Wahrheit für Betrieb/Install**, dieses AGENTS.md dupliziert sie nicht |

`~/.config/dofscroll/dofscroll.conf` (Legacy: `~/.config/spnav-scroll/spnav-scroll.conf` und `~/.config/spnav-scroll.conf`, vom Tray
beim Start migriert) ist NICHT Teil des Repos (rein benutzerspezifisch) - siehe
`readme.md` Abschnitt 4 für das Format.

## Build (verifiziert)

```sh
make check-deps   # Abhängigkeiten prüfen
make -j4          # beide Binaries im Repo-Root
make test         # Unit-Tests
make check        # komplettes Quality Gate (werror, Tests, ASAN, cppcheck, Hardening)
```

Benötigte Pakete (RHEL 9 + EPEL): `gcc-c++ spacenavd libspnav-devel libX11-devel
libXtst-devel gtk3-devel libappindicator-gtk3-devel` - exaktes `dnf install` in
`readme.md` Abschnitt 5.

**Warnung vor einem Rebuild:** die beiden Binaries im Repo-Root sind reine lokale
Build-Artefakte (in `.gitignore` gelistet, `git ls-files` bestätigt: nicht getrackt - nur
die `.cpp`-Quellen sind es) - ein frischer Clone hat sie nicht, erst der obige Build
erzeugt sie. Ein laufender Dienst hält seine Binärdatei offen (`Text file busy`) - vor
einem Rebuild auf einem System mit aktiven Diensten erst stoppen: `systemctl --user stop
dofscroll.service dofscroll-tray.service`, danach neu bauen, danach `systemctl
--user start ...`.

## Verifikation einer Änderung

Unit-/Szenario-Tests (`make test`) decken Config, DSP und Achsen-Assistent ab; das Verhalten hängt darüber hinaus an echter SpaceMouse-Hardware,
einer laufenden X11-Session und `spacenavd`. Das Realistische, was ein Agent ohne
Hardware-Zugriff tun kann:
1. `make check` (mindestens `make check-fast`) ausführen und auf Compile-Fehler/Warnings prüfen.
2. `readme.md` auf Widerspruch zur geänderten Logik prüfen (das Dokument selbst sagt
   explizit: Code ist die Quelle der Wahrheit für Verhalten, README nur für Architektur/
   Verträge - bei einer Verhaltensänderung also README mit aktualisieren, nicht nur Code).
3. Reale Verifikation (Scrollen/Zoomen/Nav-Modus/Klick-Button testen, `journalctl --user
   -u dofscroll.service -f`) kann nur der Mensch an echter Hardware machen - das klar
   benennen statt zu simulieren, dass es getestet wurde.

## Sprache

Repo-Dokumentation (README, Code-Kommentare) ist durchgehend Deutsch - bei Docs-Änderungen
dabei bleiben, nicht auf Englisch wechseln.
