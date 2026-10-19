// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Modi als Plugins: deklarative Dateien in <Konfigurationsordner>/dofscroll/modes/*.conf
// (Dateiname ohne ".conf" = Modus-Kennung, [a-z0-9_-]{1,32}). Kein Neukompilieren noetig.
// Ein Modus legt fest: (1) was die drei Kanaele (vertikal, horizontal, Drehen) ausloesen,
// (2) welche Taste (Button-Index) was tut, (3) Einstellungs-Ueberschreibungen (dieselben
// Keys wie in einem Profil, z.B. SCROLL_H_SENS). Aktiv ist genau EIN Modus: der per
// Config-Key MODE im aufgeloesten Profil gewaehlte, sonst "normal". MODE darf in jeder
// Sektion stehen ([global], [device:*], [app:*]; spaeter gewinnt, App vor Geraet vor Global).
//
// Dateiformat (wie die Haupt-Config: "KEY = Wert", '#'/';' Kommentare, keine Sektionen):
//   NAME = Timeline (Scrubben/Einzelbilder)        # Anzeigename (Tray), optional
//   DESCRIPTION = Videoschnitt, z.B. DaVinci ...    # optional
//   V_ACTION = none                                 # Kanal vertikal  (SCROLL_V)
//   H_ACTION = keys:left,right                      # Kanal horizontal (SCROLL_H)
//   TWIST_ACTION = keys:left,right                  # Kanal Drehen    (ZOOM)
//   BUTTON_0 = key:space                            # Taste 0 haelt Leertaste
//   BUTTON_1 = click
//   SCROLL_H_SENS = 4                               # beliebiger Settings-Key (applyKey)
//
// Kanal-Aktionen:  default | none | wheel_v | wheel_h | zoom | keys:<neg>,<pos>
//   default = normales Verhalten des Kanals (V: wheel_v, H: wheel_h, Drehen: zoom mit
//   Dominanz-Erkennung). zoom = Strg halten + Mausrad. keys:<neg>,<pos> = Tastendruecke
//   (neg bei negativer, pos bei positiver Auslenkung; Tempo analog zur Auslenkung),
//   Tastennamen aus keys.hpp.
// Tasten-Aktionen: none | click | rightclick | middleclick | nav | key:<name>
//   click/rightclick/middleclick = Maustaste halten solange die Taste gehalten wird;
//   key = Taste halten; nav = solange gehalten senden V/H-Kanaele Pfeiltasten statt Scroll.
// Ein Modus, dessen drei Kanal-Aktionen alle "default" sind, laeuft im normalen Pfad (inkl.
// Dominanz-basiertem Zoom, NAV_BUTTON/CLICK_BUTTON aus den Settings).
//
// Eingebaut (ueberschreibbar durch gleichnamige Datei): "normal" (alles default, keine
// Tasten), "off" (alles none: Modus zum Abschalten), "timeline" (V none, H und TWIST
// keys:left,right).
#pragma once
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "keys.hpp"

namespace spn {

enum class ChannelActionType { Default, None, WheelV, WheelH, Zoom, Keys };
struct ChannelAction {
    ChannelActionType type = ChannelActionType::Default;
    Key neg = Key::Up, pos = Key::Down;  // nur bei Keys
    bool operator==(const ChannelAction &o) const {
        return type == o.type && (type != ChannelActionType::Keys || (neg == o.neg && pos == o.pos));
    }
};

enum class ButtonActionType { None, Click, RightClick, MiddleClick, Nav, Key };
struct ButtonBinding {
    int button = 0;                       // Button-Index (spnav_event_button.bnum), 0..31
    ButtonActionType type = ButtonActionType::None;
    Key key = Key::Space;                 // nur bei Key
};

struct ModeDef {
    std::string id;           // Kennung (Dateiname ohne .conf bzw. eingebaut)
    std::string label;        // Anzeige; Fallback: id
    std::string description;  // optional
    std::vector<std::pair<std::string, std::string>> overrides;  // Settings-Keys (validiert)
    ChannelAction v, h, twist;
    std::vector<ButtonBinding> buttons;
    bool builtin = false;

    bool allDefaultChannels() const {
        return v.type == ChannelActionType::Default && h.type == ChannelActionType::Default &&
               twist.type == ChannelActionType::Default;
    }
};

struct ModeSet {
    std::vector<ModeDef> modes;  // modes[0] ist immer "normal"
    // nullptr, wenn unbekannt.
    const ModeDef *find(const std::string &id) const;
    const ModeDef &normal() const { return modes[0]; }
};

// Syntaktisch gueltige Kennung ([a-z0-9_-]{1,32}).
bool validModeId(const std::string &id);

bool parseChannelAction(const std::string &text, ChannelAction *out, std::string *err);
bool parseButtonAction(const std::string &text, ButtonActionType *type, Key *key, std::string *err);

// <Konfigurationsordner>/dofscroll/modes (Konfigurationsordner wie legacyConfigPath()).
std::string modesDir();

// Eingebaute Modi + alle gueltigen *.conf in dir. Fehlendes Verzeichnis ist kein Fehler.
// Harte Grenzen: <= 64 Dateien, je <= 64 KiB, <= 128 Ueberschreibungen je Datei; nur
// regulaere Dateien. Fehlerhafte Zeilen/Dateien werden mit Warnung uebersprungen
// (warnings optional). Gleichnamige Datei ersetzt den eingebauten Modus.
ModeSet loadModes(const std::string &dir, std::vector<std::string> *warnings);

// Billige Identitaet des Verzeichnisinhalts (Namen + mtime + Groesse) fuer Auto-Reload.
// Leer, wenn das Verzeichnis fehlt.
std::string modesStamp(const std::string &dir);

}  // namespace spn
