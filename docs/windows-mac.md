# SpaceMouse unter Windows und macOS: offizieller 3Dconnexion-Treiber

Recherchestand: 2026-10-12. Die offiziellen 3Dconnexion-Seiten (3dconnexion.com/...)
lieferten beim Abruf HTTP 403; Aussagen dazu stammen daher aus Suchergebnis-Zusammenfassungen
und Forenbeitraegen, nicht aus selbst gelesenen Volltexten. Kennzeichnung:

- **[belegt]** = durch verlinkte Quelle gestuetzt (soweit die Quelle erreichbar/zusammengefasst war)
- **[Forum]** = nur Forum-/Community-Aussage, teils aelter (2007-2016), Treiberversion kann abweichen
- **[Vermutung]** = eigene Schlussfolgerung, nicht belegt
- **[ungeklaert]** = nichts Belastbares gefunden

## Kurzfazit

1. Der offizielle Treiber (3DxWare / 3Dconnexion Software) hat **Anwendungsprofile**; pro App
   lassen sich Achsen aktivieren/deaktivieren, umkehren, Dominanz waehlen, Empfindlichkeit
   und Tastenbelegung einstellen. [belegt/Forum, siehe Quellen 1, 2, 3]
2. Fuer **normale Apps ohne 3D-Support (Browser, PDF-Viewer) scrollt der Treiber nicht
   zuverlaessig**: Unter macOS bestaetigte ein Moderator, dass Scrollen in Safari/Acrobat
   Reader mit 3DxWare 10.8.4 nicht unterstuetzt wird. [Forum, Quelle 5] Unter Windows gibt es
   Berichte, dass Word/Excel scrollen bzw. die Kappendrehung zoomt, aber keine offizielle
   Zusage eines generischen Scroll-Modus. [Forum, Quelle 6] Ein dokumentierter generischer
   "Scroll"- oder "Pan/Zoom"-Modus fuer beliebige Apps wurde **nicht gefunden** [ungeklaert].
3. Profile lassen sich **offiziell exportieren/importieren** (Windows und macOS) und sind
   damit grundsaetzlich versionierbar, aber nur sinnvoll mit gleicher Treiberversion.
4. Wer exakt das Verhalten dieses Repos (analoge Scrollgeschwindigkeit, Drehen = Zoom,
   Horizontalscroll) braucht, muesste ein eigenes Tool ueber das 3Dconnexion-SDK bauen
   (Aufwand mittel bis hoch) oder ein Drittwerkzeug nutzen.

## Windows

### Einstellungen pro Anwendung

- Das Control Panel hat pro Anwendung einen "App Configuration"-Bereich: Pan, Zoom, Tilt,
  Spin, Roll einzeln aktivieren/deaktivieren, alle Achsen umkehren, dominante Achse waehlen.
  [Forum/Drittdoku, Quelle 2 (Autodesk-Hilfe zum Control Panel, Version 2015)]
- Empfindlichkeit gesamt und pro Achse sowie Zoomrichtung pro Anwendung. [Forum, Quelle 3]
- Das SpaceMouse-Pro-Handbuch nennt "Advanced Settings" mit Pan/Zoom-Schalter und
  Bewegungsverhalten fuer viele Anwendungen. [belegt, Quelle 1]
- Feinere Achsenzuordnung (z.B. Zoomrichtung ohne Rotationsvertauschung) laeuft ueber die
  pro-App-XML-Konfigurationsdatei (Eintraege wie `AXIS_MAP_HRZ_USER`/`AXIS_MAP_VRT_USER`);
  ein Forenbeitrag sagt, das sei nicht in der GUI exponiert. [Forum, Quellen 3, 4]
- Tastenbelegung pro Anwendung: Tasten der SpaceMouse (Pro/Compact) koennen im Treiber
  belegt werden; die genaue Oberflaeche der aktuellen "3Dconnexion Software" habe ich nicht
  verifiziert. [ungeklaert fuer aktuelle Version]
- Deadzone: als eigener Regler **nicht belegt** gefunden. [ungeklaert] Die Empfindlichkeit
  wirkt in der Praxis aehnlich, ist aber nicht dasselbe. [Vermutung]

### Scrollen in normalen Programmen

- Office: Forenbericht, Scrollen in Office-2013-Apps funktionierte erst nach "Restore Defaults";
  Kappendrehung zoomt in Excel/Word. [Forum, Quelle 6]
- Acrobat unter Windows: Pan/Zoom funktionierte nach Anpassung der Sicherheitseinstellungen;
  Tipp, `AdobeReader.xml` im 3DxWare-Ordner zu loeschen. [Forum, Quelle 6 (Adobe Community)]
- Browser (Chrome/Firefox): Feature-Wunsch im Forum, kein Beleg fuer native Unterstuetzung.
  [Forum, Quelle 6]
- Fazit: Verhalten haengt von App-spezifischen Profilen bzw. Mappings des Treibers ab. Ob der
  Treiber fuer unbekannte Apps auf Scroll-Events zurueckfaellt, ist **nicht belegt**.

### Konfigurationsdateien, Export/Import

- Nutzerspezifische Dateien: `%APPDATA%\3Dconnexion\3DxWare\Cfg` (XML). [belegt, Quelle 7]
- Aeltere Treiber: Profile als `.scg`; Zuordnung App-zu-Profil zusaetzlich in der Registry
  (`HKCU\Software\3Dconnexion\3DxWare\Applications`); reines Kopieren der Dateien reichte
  nicht, ein Nutzer berichtet, kopierte Profile wurden ignoriert. [Forum, Quelle 8]
- Offiziell: Treiberpanel, Menue "More", "Export Settings" / "Import Settings"; nur mit
  gleicher Treiberversion verwenden. Speicherort ist ueber den "3Dconnexion Administrators
  Guide" (Abschnitt "Configuration hierarchy and sharing") aenderbar. [belegt, Quelle 7]
- Versionierung: Export-Datei bzw. `Cfg`-XMLs koennten in ein Git-Repo; Wiederherstellung am
  zuverlaessigsten ueber Import. [Vermutung]

## macOS

### Einstellungen

- Die Konfiguration erfolgt im 3Dconnexion-Bereich der Systemeinstellungen (frueher
  Preference Pane); Tasten-/Achsenoptionen analog zu Windows, Details der aktuellen
  macOS-Version nicht verifiziert. [ungeklaert]
- Scrollen in Safari und Acrobat Reader wird mit 3DxWare 10.8.4 laut Moderator nicht
  unterstuetzt; ohne installierten Treiber verhaelt sich die Maus als einfaches 2D-Geraet,
  mit Treiber nicht mehr. [Forum, Quelle 5] Das Beispiel ist nicht aktuell (Version, Datum
  unklar); aktuelle Release Notes pruefen. [ungeklaert]

### Konfigurationsdateien, Export/Import

- Nutzereinstellungen: `~/Library/Preferences/com.3dconnexion.mappings.plist`; Moderator:
  Bearbeiten "not recommended and not supported". [Forum, Quelle 9]
- Weitere plists (`com.3dconnexion.3DMouseHome.plist`, `VirtualNumPad.plist`) werden in einem
  McNeel-Thread genannt, Rolle unklar. [Forum, Quelle 10]
- Einstellungen sind pro Nutzer gespeichert und ueberleben ein Deinstallieren des Treibers.
  [Forum, Quelle 9]
- Offiziell: Pulldown-Menue im Treiberbereich, "Export Configuration...". [Forum, Quelle 9]
- Pfad `~/Library/Application Support/3Dconnexion` wurde nicht bestaetigt. [ungeklaert]

## Falls der Treiber nicht scrollt: Alternativen

| Option | Beschreibung | Aufwand/Nutzen (Einschaetzung = Vermutung) |
|---|---|---|
| 3Dconnexion SDK (Windows: SiApp/TDx-API; macOS: `3DconnexionClient.framework`) | Eigenes Programm liest SpaceMouse-Bewegung und erzeugt Scroll-/Zoom-Events (`SendInput` bzw. CGEvent) | Mittel bis hoch; entspricht funktional diesem Repo, Portierung der Logik (125-Hz-Tick, Kennlinie) auf zwei neue Plattformen. Nutzen: exakt gewuenschtes Verhalten. |
| Treiber-Profile pro App anlegen | Mappings/Tastenbelegung (z.B. Taste = Bild hoch/runter) | Gering; kein analoges Scrollen, aber ohne Code. |
| Drittwerkzeuge | Konkrete Tools nicht recherchiert | [ungeklaert] |

### SDK-Zugang und Lizenz

- Registrierung als Entwickler bei 3Dconnexion noetig (Developer Program, laut ParaView-Doku
  `3dconnexion.com/us/software-developer-program/`); ein Kommentar sagt, kein formaler Vertrag
  noetig, andere berichten Registrierungsprobleme. [Forum/Community, Quellen 11, 12]
- Lizenztext (Versionen 2007/2013/2020 in Drittprodukten): widerruflich, tantiemenfrei,
  nicht uebertragbar, keine Patentrechte; Anwendungen muessen 3Dconnexion im About-/Hilfetext
  nennen. Den aktuellen Wortlaut vor Nutzung im SDK-Download pruefen. [belegt, Quelle 13]
- Aeltere C/C++-SDK-Versionen liegen laut Forum im Archivbereich; Linux-SDK sei nicht
  geplant gewesen. [Forum, Quelle 12]
- Ob eine reine Eigenbau-Verteilung (Privatgebrauch) lizenzrechtlich unproblematisch ist,
  habe ich nicht geprueft. [ungeklaert]

## Empfehlung fuer den Nutzer

1. Offiziellen Treiber installieren und ausprobieren: Browser, Office, PDF-Viewer direkt
   testen. Pro Anwendung im Treiberpanel Achsen, Umkehrung, Empfindlichkeit und Tasten
   einstellen. Ergebnis mit dem Linux-Verhalten (Auslenkung = Geschwindigkeit) vergleichen.
2. Wenn die Kappendrehung zoomt und Scrollen reicht: Einstellungen per "Export Settings"
   sichern, die Export-Datei (nicht das rohe `Cfg`-Verzeichnis) ins Repo legen, Treiberversion
   dazuschreiben.
3. Wenn Apps nicht scrollen (v.a. macOS-Browser laut Forum): vor Eigenbau pruefen, ob aktuelle
   Release Notes das beheben; sonst SDK-Eigenbau evaluieren (Developer-Registrierung zuerst).
4. Vor Entscheidungen die Luecken schliessen: Deadzone, generischer Scroll-Modus, aktuelle
   Menues der "3Dconnexion Software" - am besten direkt am Geraet pruefen.

## Quellen

1. SpaceMouse Pro Manual: https://3dconnexion.com/manuals/spacemouse-pro/en/Manual_3Dconnexion-SpaceMouse-Pro_EN.pdf
2. Autodesk Moldflow zum 3Dconnexion Control Panel: https://help.autodesk.com/cloudhelp/2015/ENU/MoldflowComm/files/GUID-68EED47E-2541-47A2-835F-F08CCDF7BE5D.htm
3. Forum, Zoomrichtung/Achsen-XML: https://forum.3dconnexion.com/viewtopic.php?p=7144
4. Forum, Zoomrichtung ohne Rotation: https://forum.3dconnexion.com/viewtopic.php?p=124907
5. Forum, Safari/Adobe Reader (macOS): https://forum.3dconnexion.com/viewtopic.php?p=122247
6. Forum, Axes Configuration; Adobe-Community: https://forum.3dconnexion.com/viewtopic.php?p=120675 , https://community.adobe.com/t5/acrobat-discussions/3d-navigation/m-p/11505360/highlight/true
7. 3Dconnexion FAQ Config-Dateien und Speicherort: https://3dconnexion.com/uk/?p=5394 , https://3dconnexion.com/us/?p=56012 (beim Abruf 403, nur Suchzusammenfassung)
8. Forum, Backup/Restore Profile: https://forum.3dconnexion.com/viewtopic.php?p=7227
9. Forum, macOS Cfg-Ort und Backup: https://forum.3dconnexion.com/viewtopic.php?p=46892 , https://forum.3dconnexion.com/viewtopic.php?p=78976
10. McNeel Discourse: https://discourse.mcneel.com/t/3dconnexion-rhino-issue/57143
11. ParaView SpaceMouseInteractor: https://gitlab.kitware.com/paraview/paraview/-/tree/master/Plugins/SpaceMouseInteractor
12. Forum "Where's the SDK?"; PrusaSlicer-Issue: https://forum.3dconnexion.com/viewtopic.php?t=984 , https://github.com/prusa3d/PrusaSlicer/issues/3447
13. SDK-Lizenztext in Drittdoku: https://learn.foundry.com/modo/16.0/content/help/pages/appendices/appendix_c_external_software/third_party_libraries.html
