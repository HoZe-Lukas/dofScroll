// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Konfigurationssystem: Settings (alle Werte), Profile (Sektionen) und
// Aufloesung nach Geraet + aktivem Programm. Wird von Daemon UND Tray benutzt
// (eine Quelle der Wahrheit fuer das Dateiformat, keine zweite Parser-Kopie).
//
// Dateiformat (~/.config/dofscroll/dofscroll.conf), INI-artig, '#' und ';' = Kommentar:
//
//   # Ohne Sektion = [global]. Alte flache Dateien bleiben gueltig.
//   SCROLL_V_MIX = z + rx          # Ausdruck: Summe aus [+-][Gewicht*]Achse
//   SCROLL_V_SENS = 5              # 0..255
//   SCROLL_V_DEADZONE = 30
//   SCROLL_V_SIGN = 1              # 1 oder -1 (Richtung)
//   SCROLL_V_CURVE = 1.5           # Exponent  ODER  Punkte "0:0 50:0.1 200:1 350:3"
//   SCROLL_V_SMOOTH_MS = 20        # Glaettung (Zeitkonstante), 0 = aus
//   (analog SCROLL_H_*, ZOOM_*)
//   ZOOM_LATERAL = x z rx rz       # Achsen, gegen die der Zoom-Twist dominieren muss
//   ZOOM_ENTER_MS = 40
//   ZOOM_EXIT_RATIO = 1.5
//   BACKEND = auto                 # auto | x11 | uinput
//   TICK_HZ = 250
//   MODE = normal                  # normal | timeline (Scrubben/Einzelbilder, z.B. [app:resolve])
//   ENABLED = 1                    # 0 = Daemon sendet nichts (z.B. [app:blender])
//   NAV_BUTTON / NAV_BUTTON_ENABLED / CLICK_BUTTON / CLICK_BUTTON_ENABLED
//
//   [device:SpaceMouse Pro]        # greift, wenn der Geraetename (spnav_dev_name)
//   SCROLL_V_SENS = 8              # diesen Text enthaelt (Gross/Klein egal)
//
//   [app:firefox]                  # greift, wenn WM_CLASS diesen Text enthaelt
//   ZOOM_SENS = 12                 # (Gross/Klein egal); Geraet vor Programm
//
// Sektionen enthalten nur die Keys, die sie ueberschreiben. Reihenfolge der
// Aufloesung: [global] -> passende [device:*] -> passende [app:*] (jeweils in
// Dateireihenfolge, spaetere gewinnen).
//
// Legacy-Keys (weiter lesbar, Tray schreibt nur noch die neuen):
//   SCROLL_SENSITIVITY_Y/X -> SCROLL_V_SENS / SCROLL_H_SENS
//   ZOOM_SENSITIVITY       -> ZOOM_SENS
//   SCROLL_DEADZONE_Y/X    -> SCROLL_V_DEADZONE / SCROLL_H_DEADZONE
//   ZOOM_DEADZONE          -> ZOOM_DEADZONE
//   INVERT_Y / INVERT_X    -> SCROLL_V_SIGN / SCROLL_H_SIGN (Wert 1 / -1, direkt Vorzeichen)
//   ZOOM_INVERT            -> ZOOM_SIGN (ZOOM_INVERT=1 bedeutet SIGN=-1, 0 bedeutet 1)
//   RESPONSE_CURVE         -> setzt den Exponenten aller drei Kanaele
#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "common.hpp"

namespace spn {

// Harte Obergrenzen gegen Ressourcen-Erschoepfung durch manipulierte Dateien
// (docs/security.md). Gueltige Konfigurationen liegen weit darunter.
constexpr size_t kMaxConfigBytes = 1u << 20;   // Dateigroesse (1 MiB)
constexpr size_t kMaxLineBytes = 8192;         // Zeilenlaenge
constexpr size_t kMaxSections = 256;           // Sektionen inkl. [global]
constexpr size_t kMaxKeysPerSection = 512;     // eindeutige Keys je Sektion
constexpr size_t kMaxMixTerms = 12;            // Terme je MIX-Ausdruck
constexpr size_t kMaxCurvePoints = 64;         // Punkte je CURVE
constexpr size_t kMaxLateralAxes = 6;          // Achsen in ZOOM_LATERAL
constexpr double kMaxZoomExitRatio = 100.0;

struct Term {
    Axis axis;
    double weight;  // inkl. Vorzeichen
};

// Ausgabewert = Summe weight*axis. Leer = Kanal liefert immer 0.
struct Mix {
    std::vector<Term> terms;
};

// Parst "z + rx", "x - rz", "0.5*rx + z", "-ry". Ungueltig -> false, *err gesetzt.
bool parseMix(const std::string &text, Mix *out, std::string *err);
std::string mixToString(const Mix &m);  // Roundtrip-faehig: parseMix(mixToString(m)) == m

struct ChannelCfg {
    Mix mix;
    int sensitivity = 5;          // 0..255, 0 = Kanal aus
    int deadzone = 30;            // Rohsensor-Einheiten, >= 0
    int sign = 1;                 // +1 oder -1
    double curve_exp = 1.0;       // >= 0.1; wird genutzt, wenn curve_points leer ist
    // Optionale frei definierte Kurve: (Eingabe nach Deadzone in Rohsensor-
    // Einheiten, Faktor auf die lineare Geschwindigkeit), aufsteigend nach
    // Eingabe, lineare Interpolation, ausserhalb geklemmt. Ueberschreibt curve_exp.
    std::vector<std::pair<double, double>> curve_points;
    double smooth_ms = 0.0;       // Zeitkonstante des Tiefpass, 0 = aus, <= 500
};

struct Settings {
    ChannelCfg scroll_v;
    ChannelCfg scroll_h;
    ChannelCfg zoom;
    std::vector<Axis> zoom_lateral;   // Default: x z rx rz
    int zoom_enter_ms = 40;
    double zoom_exit_ratio = 1.5;
    std::string backend = "auto";     // auto | x11 | uinput
    int tick_hz = 250;                // 30..1000
    int nav_button = 0;
    bool nav_button_enabled = false;
    int click_button = 1;
    bool click_button_enabled = false;
    // Steuerung insgesamt aktiv? false (typisch in einem [app:*]-Profil, z.B. Blender mit
    // eigener 3D-Navigation): der Daemon erzeugt dann gar keine Ausgabe.
    bool enabled = true;
    // "normal" (Scroll/Zoom/Nav) oder "timeline" (Video-Schnitt, z.B. DaVinci Resolve):
    // seitliches Schieben/Kippen (SCROLL_H_*) = Scrubben, Drehen (ZOOM_*) = Einzelbilder,
    // beides als Links/Rechts-Pfeiltasten (Schritte pro Sekunde je nach Auslenkung).
    std::string mode = "normal";  // Kennung aus modes.hpp (Register)

    Settings();  // setzt die Defaults (siehe config.cpp): scroll_v "z + rx" sens 5
                 // dz 30 sign 1; scroll_h "x - rz" sens 3 dz 20 sign 1;
                 // zoom "ry" sens 5 dz 40 sign 1
};

using KV = std::pair<std::string, std::string>;

// Setzt genau einen Key (neue und Legacy-Namen, Key case-insensitiv).
// false + *err bei unbekanntem Key oder ungueltigem Wert (Settings bleibt dann unveraendert).
bool applyKey(Settings &s, const std::string &key, const std::string &value, std::string *err);

// Alle Keys in kanonischer Reihenfolge (neue Namen), fuer die [global]-Sektion.
std::vector<KV> allKeys(const Settings &s);
// Nur Keys, in denen s von base abweicht (fuer [device:*]/[app:*]-Sektionen).
std::vector<KV> diffKeys(const Settings &base, const Settings &s);

struct Section {
    enum Kind { GLOBAL, DEVICE, APP } kind = GLOBAL;
    std::string pattern;              // bei DEVICE/APP; leer bei GLOBAL
    std::vector<KV> kv;               // Roh-Keys wie in der Datei (Reihenfolge erhalten)
};

struct ProfileSet {
    std::vector<Section> sections;    // sections[0] ist immer GLOBAL

    ProfileSet();                     // nur leere GLOBAL-Sektion
    // Aufloesung fuer Geraet + Programm (beide duerfen leer sein).
    Settings resolve(const std::string &device_name, const std::string &wm_class) const;
    // Settings nur der Sektion i (global: Defaults + global kv; sonst: base + kv der Sektion).
    Settings sectionSettings(size_t i) const;
    // Schreibt s in Sektion i zurueck (global: allKeys; DEVICE: diffKeys gegen global;
    // APP: Abweichungen von global PLUS jeder Key, den ein [device:*]-Abschnitt auf
    // einen anderen Wert setzt als s - damit ein Programmprofil auch den globalen Wert
    // gegen einen Geraete-Override durchsetzen kann). Signatur unveraendert.
    void setSectionFromSettings(size_t i, const Settings &s);
    static constexpr size_t npos = static_cast<size_t>(-1);
    // Muster gueltig? (nach trim nicht leer, kein ']' und kein Zeilenumbruch -
    // sonst ueberlebt es Speichern+Laden nicht). *out = getrimmtes Muster.
    static bool normalizePattern(const std::string &pattern, std::string *out);
    // Findet/legt Sektion an, liefert Index. Ungueltiges Muster (siehe
    // normalizePattern) -> npos, Aufrufer MUSS das pruefen. Muster wird getrimmt.
    size_t findOrAdd(Section::Kind kind, const std::string &pattern);
    // Wie findOrAdd, legt aber nichts an: npos wenn nicht vorhanden/ungueltig.
    size_t find(Section::Kind kind, const std::string &pattern) const;
    void remove(size_t i);            // GLOBAL (i == 0) nicht entfernbar

private:
    // Suchindex (Art + kleingeschriebenes Muster -> Index), damit findOrAdd O(1)
    // statt O(n) ist. Wird bei Groessenaenderung von `sections` bzw. Treffer-
    // Mismatch lazy neu aufgebaut; Aenderungen ausser push_back/erase von aussen
    // bitte vermeiden (oder sections per findOrAdd/remove pflegen).
    mutable std::unordered_map<std::string, size_t> idx_;
    mutable size_t idx_n_ = static_cast<size_t>(-1);
    void rebuildIndex() const;
};

// Aeltester Ort (Projektname damals "spnav-scroll"): $XDG_CONFIG_HOME (nur wenn absolut,
// XDG-Spec) oder $HOME bzw. passwd-Heimat + /.config, jeweils + /spnav-scroll.conf
std::string legacyConfigPath();
// Vorheriger Ort: <configdir>/spnav-scroll/spnav-scroll.conf
std::string previousConfigPath();
// Neuer Ort: <configdir>/dofscroll/dofscroll.conf. Fallback: existiert die neue Datei nicht
// (lstat), wird der vorherige, sonst der aelteste Pfad geliefert, falls vorhanden (bis zur
// Migration). Hoechstens 3 lstat-Aufrufe, billig genug fuer 1x/s.
std::string defaultConfigPath();
// Idempotente Best-Effort-Migration alt -> neu. true, wenn die neue Datei danach existiert
// (schon da oder migriert); false, wenn nichts zu migrieren war oder es scheiterte (*log =
// einzeilige Begruendung, darf nullptr sein; bei Erfolg Hinweis, dass die alte Datei
// entfernt werden kann). Symlinks werden als Symlink (absolutes Ziel) uebernommen,
// regulaere Dateien kopiert (Modus erhalten, ueberschreibt nie eine vorhandene neue
// Datei, max. kMaxConfigBytes). Die alte Datei bleibt immer unangetastet.
bool migrateLegacyConfig(std::string *log);
// Fehlende Datei -> leeres ProfileSet (Defaults), true. Nicht-regulaere Datei
// (z.B. Verzeichnis, FIFO) oder Datei > kMaxConfigBytes -> false. Zeilen > kMaxLineBytes,
// Sektionen > kMaxSections und Keys > kMaxKeysPerSection werden mit Warnung uebersprungen;
// doppelte Keys einer Sektion werden zusammengefasst (letzter Wert, Position des letzten). Ein UTF-8-BOM am Dateianfang wird ignoriert. Ungueltige Zeilen/Werte
// werden uebersprungen und in *warnings (falls != nullptr) beschrieben.
bool loadProfiles(const std::string &path, ProfileSet *out, std::vector<std::string> *warnings);
// Schreibt atomar (temp neben dem echten Ziel + fsync + rename). Symlinks werden
// aufgeloest und bleiben erhalten, Dateimodus bleibt erhalten (neu: 0600).
// Ungueltige Sektionsmuster, Key/Wert mit Zeilenumbruch, leerer Key oder Key mit
// fuehrendem '[', '#', ';' bzw. mit '=', von applyKey abgelehnte Paare, Symlink-
// Schleifen (> 16 Hops, ELOOP) -> false, es wird nichts geschrieben. Modus ohne
// setuid/setgid/sticky. Temp-Datei mit Zufallsnamen im selben Verzeichnis-Handle. Kommentare der Originaldatei gehen verloren
// (bekannte Einschraenkung, steht im Dateikopf).
bool saveProfiles(const std::string &path, const ProfileSet &ps, std::string *err);

}  // namespace spn
