# uinput-Zugriff fuer dofscroll

Unter Wayland kann kein XTest genutzt werden. Das uinput-Backend legt stattdessen ein
virtuelles Eingabegeraet ueber `/dev/uinput` an. Dafuer braucht der Nutzer Schreibrechte
auf dieses Device, die standardmaessig nur root hat.

## Installation

```sh
sudo cp packaging/70-dofscroll-uinput.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

Die Regel setzt `MODE="0600"` und `TAG+="uaccess"`: systemd-logind vergibt per ACL den
Zugriff nur an den Nutzer der gerade aktiven lokalen Sitzung am Seat. Danach einmal ab- und
wieder anmelden (bzw. `udevadm trigger` abwarten). Reine SSH-Sitzungen und
Hintergrundprozesse anderer Nutzer erhalten keinen Zugriff.

Falls `/dev/uinput` fehlt: `sudo modprobe uinput` (dauerhaft per
`/etc/modules-load.d/uinput.conf` mit dem Inhalt `uinput`).

Pruefen: `ls -l /dev/uinput` zeigt `crw------- root root` plus ACL (`+`); `getfacl
/dev/uinput` listet den angemeldeten Nutzer mit `rw-`.

## Warnung: nicht in die Gruppe `input` aufnehmen

Frueher wurde empfohlen, den Nutzer mit `usermod -aG input` aufzunehmen. Das ist
**nicht mehr noetig und nicht empfohlen**: Die Gruppe `input` hat Lesezugriff auf alle
`/dev/input/event*` (typisch `root:input 0660`). Jeder Prozess des Nutzers kann damit alle
Tastaturen systemweit mitlesen (Keylogging, auch Passwoerter in `sudo`, Sperrbildschirm
oder Konsole), unabhaengig von der X11-/Wayland-Isolation. Wer die Gruppe bereits hat,
sollte sie entfernen (`sudo gpasswd -d "$USER" input`), sofern nichts anderes sie braucht.
Falls zwingend eine Gruppe noetig ist (headless/SSH): eigene Gruppe nur fuer `/dev/uinput`
anlegen, nicht `input`.

## Hinweis

Ist `/dev/uinput` auf einem Rechner `0666`, ist das eine Fehlkonfiguration (jeder lokale
Account koennte Tastendruecke in fremde Sitzungen einspeisen). Ursache in anderen
udev-Regeln unter `/etc/udev/rules.d` bzw. `/usr/lib/udev/rules.d` suchen und beheben.
Unter X11 genuegt das Backend `x11` (Standard bei `BACKEND=auto`); dann wird `/dev/uinput`
gar nicht gebraucht.

## Hinweis: andere Regeln koennen diese Regel ueberstimmen

udev wendet Regeldateien in lexikalischer Reihenfolge an, spaetere gewinnen. Eine eigene
Regel wie `/etc/udev/rules.d/99-uinput.rules` mit `KERNEL=="uinput", MODE="0666"` setzt
`/dev/uinput` fuer **alle** lokalen Nutzer und Dienste beschreibbar und hebt die Regel
`70-dofscroll-uinput.rules` auf. Pruefen: `ls -l /dev/uinput` (soll nicht `crw-rw-rw-` sein)
und `grep -rl uinput /etc/udev/rules.d /usr/lib/udev/rules.d`. Unter X11 wird uinput gar
nicht gebraucht (BACKEND=auto nimmt XTest); die Regel wird nur fuer Wayland benoetigt.
