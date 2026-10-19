// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Signalverarbeitung pro Kanal (Scroll V, Scroll H, Zoom): Mix -> Vorzeichen ->
// Glaettung -> Deadzone -> Kurve -> Geschwindigkeit. Rein rechnerisch, ohne X11/
// libspnav, damit es unit-testbar ist.
#pragma once
#include "common.hpp"
#include "config.hpp"

namespace spn {

// Summe der Terme (double, noch ohne Vorzeichen/Deadzone).
double evalMix(const Mix &m, const RawAxes &raw);

// Deadzone abziehen: |v| <= dz -> 0, sonst Betrag um dz verkleinert, Vorzeichen bleibt.
double applyDeadzone(double v, int dz);

// Kurve auf einen deadzone-bereinigten Wert anwenden (Vorzeichen bleibt).
// Ohne curve_points: sign(v) * (|v|/CURVE_REF)^exp * CURVE_REF  (exp == 1 -> linear).
// Mit curve_points: |v| linear * interpolierter Faktor.
constexpr double CURVE_REF = 200.0;
double applyCurve(const ChannelCfg &c, double v);

// Zeitbasierter Tiefpass erster Ordnung, unabhaengig von der Tickrate:
// alpha = 1 - exp(-dt / tau). tau_ms <= 0 -> Durchreichen. Bei Reset (z.B.
// Verbindungsabbruch) startet der Filter bei 0.
class Smoother {
public:
    void reset() { y_ = 0.0; }
    double step(double in, double dt_s, double tau_ms);
private:
    double y_ = 0.0;
};

// Eine Kanal-Instanz mit Filterzustand. process() liefert die Geschwindigkeit in
// "Schritten pro Sekunde" (vorzeichenbehaftet), kalibriert wie das bisherige
// Verhalten: (curved * sensitivity / 2500) * 125. sensitivity == 0 -> 0.
class Channel {
public:
    double process(const ChannelCfg &c, const RawAxes &raw, double dt_s);
    // Wie process(), aber mit bereits berechnetem Rohwert (z.B. Zoom, wo der Mix
    // vorher fuer die Dominanzpruefung gebraucht wird).
    double processValue(const ChannelCfg &c, double mixed, double dt_s);
    void reset() { smoother_.reset(); }
private:
    Smoother smoother_;
};

// Schritt-Akkumulator: sammelt Schritte/s * dt und gibt ganze Schritte aus.
class StepAccumulator {
public:
    // Addiert rate*dt; liefert ganze Schritte (vorzeichenbehaftet), Rest bleibt.
    int add(double steps_per_s, double dt_s);
    void reset() { acc_ = 0.0; }
private:
    double acc_ = 0.0;
};

// Zoom-Erkennung (Latch mit Hysterese + Einstiegsverzoegerung).
// twist = |Zoom-Mix nach Vorzeichen|, lateral = staerkste Einzelachse aus
// settings.zoom_lateral, deadzone = zoom.deadzone.
class ZoomDetector {
public:
    // true, solange gezoomt wird.
    bool update(double twist_abs, double lateral_abs, const Settings &s, double dt_s);
    void reset() { latched_ = false; candidate_s_ = 0.0; }
    bool active() const { return latched_; }
private:
    bool latched_ = false;
    double candidate_s_ = 0.0;
};

// Staerkste Einzelachse (Betrag) aus der Liste.
double lateralMax(const std::vector<Axis> &axes, const RawAxes &raw);

}  // namespace spn
