// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Auswertungslogik des Assistenten "Achsen einlernen" (Tray, Reiter "Achsen").
//
// Header-only und bewusst ohne GTK/libspnav, damit sie in tests/test_axis_wizard.cpp
// ohne GUI und ohne Hardware geprueft werden kann.
//
// Prinzip: Pro Schritt bewegt der Nutzer die Kappe ~3 s lang in eine angesagte
// Richtung (zuerst in die genannte Richtung, dann zurueck). Gemessen wird der
// Spitzenwert (Betrag) je Rohachse. Weil die SpaceMouse stark uebersprechen kann
// (Beispiel SpaceMouse Pro, "Schieben seitlich": x 350, y 293 ...), wird immer die
// ABSOLUT staerkste Achse gewaehlt - nach Glaettung (Median ueber 3 Samples, ein
// einzelner Spike zaehlt nicht); fast gleich starke Achsen (TIE_RATIO) gelten als
// nicht eindeutig. Das Vorzeichen kommt aus der ersten anhaltenden Halbbewegung
// (mind. MIN_RUN Samples ueber SIGN_THRESHOLD) - sie zeigt in die angesagte
// Richtung, die damit positiv wird.
#pragma once
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "common.hpp"

namespace spn {
namespace wiz {

// Reihenfolge = Reihenfolge der Schritte im Assistenten.
enum Role { ROLE_PUSH_V = 0, ROLE_TILT_V, ROLE_PUSH_H, ROLE_TILT_H, ROLE_TWIST, ROLE_COUNT };

constexpr int SIGN_THRESHOLD = 40;      // erste Halbbewegung: ab diesem Betrag zaehlt das Vorzeichen
constexpr int MIN_PEAK = 80;            // darunter: "zu schwach"
constexpr double AMBIGUOUS_RATIO = 0.85;  // zweitstaerkste/staerkste darueber -> Hinweis "knapp"
constexpr double TIE_RATIO = 0.95;      // darueber: praktisch Gleichstand -> nicht eindeutig, ungueltig

inline const char *roleTitle(int r) {
    static const char *t[ROLE_COUNT] = {"Schieben vor/zurueck", "Kippen vor/zurueck",
                                        "Schieben seitlich", "Kippen seitlich",
                                        "Drehen"};
    return (r >= 0 && r < ROLE_COUNT) ? t[r] : "?";
}

// Kanal einer Rolle: 0 = Scroll vertikal, 1 = Scroll horizontal, 2 = Zoom.
inline int roleChannel(int r) {
    return r == ROLE_PUSH_V || r == ROLE_TILT_V ? 0 : (r == ROLE_PUSH_H || r == ROLE_TILT_H ? 1 : 2);
}

inline bool roleOptional(int r) { return r == ROLE_TILT_V || r == ROLE_TILT_H; }

struct StepResult {
    bool valid = false;        // Achse erkannt und stark genug
    bool too_weak = false;     // Spitze < min_peak
    bool ambiguous = false;    // zwei Achsen (fast) gleich stark -> ungueltig
    int axis = -1;             // spn::Axis der staerksten Achse, -1 = keine Daten
    int sign = 0;              // +1/-1: Vorzeichen der angesagten Richtung
    int peak = 0;              // geglaetteter Spitzenbetrag der gewaehlten Achse
    std::array<int, AX_COUNT> peaks{};  // geglaetteter Spitzenbetrag je Achse
    int runner_up = -1;        // zweitstaerkste Achse
    double ratio = 0.0;        // peaks[runner_up] / peak
    std::string message;       // Klartext fuer die Anzeige
};

// Spitzenbetrag je Achse ueber alle Samples.
inline std::array<int, AX_COUNT> peakAbs(const std::vector<RawAxes> &samples) {
    std::array<int, AX_COUNT> p{};
    for (const auto &s : samples)
        for (int a = 0; a < AX_COUNT; ++a) {
            int v = std::abs(s[a]);
            if (v > p[a]) p[a] = v;
        }
    return p;
}

// Index der betragsmaessig staerksten Achse (bei Gleichstand die erste), -1 wenn alle 0.
inline int strongestAxis(const std::array<int, AX_COUNT> &peaks, int exclude = -1) {
    int best = -1;
    for (int a = 0; a < AX_COUNT; ++a) {
        if (a == exclude) continue;
        if (best < 0 || peaks[a] > peaks[best]) best = a;
    }
    if (best >= 0 && peaks[best] == 0 && exclude < 0) return -1;
    return best;
}

// Geglaetteter Spitzenbetrag je Achse: Maximum des Medians ueber jeweils
// SMOOTH_WIN aufeinanderfolgende Betraege. Ein einzelner Ausreisser (Spike,
// Uebersprechen) zaehlt dadurch nicht; die Spitze muss mindestens 2 von 3 Samples
// lang gehalten werden. Bei weniger als SMOOTH_WIN Samples gilt ein Fenster ueber alle
// vorhandenen (Median = unterer Wert). center[a] = Index der Fenstermitte der Spitze.
constexpr int SMOOTH_WIN = 3;

inline std::array<int, AX_COUNT> smoothedPeaks(const std::vector<RawAxes> &samples,
                                               std::array<int, AX_COUNT> *center = nullptr) {
    std::array<int, AX_COUNT> p{};
    std::array<int, AX_COUNT> c{};
    const int n = int(samples.size());
    const int w = n < SMOOTH_WIN ? n : SMOOTH_WIN;
    for (int a = 0; a < AX_COUNT && w > 0; ++a)
        for (int i = 0; i + w <= n; ++i) {
            std::array<int, SMOOTH_WIN> win{};
            for (int k = 0; k < w; ++k) win[k] = std::abs(samples[i + k][a]);
            std::sort(win.begin(), win.begin() + w);
            const int m = win[(w - 1) / 2];
            if (m > p[a]) {
                p[a] = m;
                c[a] = i + w / 2;
            }
        }
    if (center) *center = c;
    return p;
}

// Mindestlaenge einer zusammenhaengenden Auslenkung (gleiches Vorzeichen, Betrag >
// Schwelle), die als "erste Halbbewegung" zaehlt. Kuerzere Zuckungen (Restbewegung
// des vorherigen Schritts, Spike) bestimmen das Vorzeichen nicht.
constexpr int MIN_RUN = 2;

// Vorzeichen der ersten anhaltenden Auslenkung von 'axis' (>= MIN_RUN aufeinander-
// folgende Samples ueber 'threshold' mit gleichem Vorzeichen). Gibt es keine:
// Vorzeichen der Summe um die Spitze (peak_center, +-1 Sample), 0 ohne Ausschlag.
inline int firstExcursionSign(const std::vector<RawAxes> &samples, int axis, int threshold,
                              int peak_center = -1) {
    if (axis < 0 || axis >= AX_COUNT) return 0;
    const int n = int(samples.size());
    int run = 0, run_sign = 0, run_sum = 0;
    for (int i = 0; i < n; ++i) {
        int v = samples[i][axis];
        int sg = std::abs(v) > threshold ? (v > 0 ? 1 : -1) : 0;
        if (sg != 0 && sg == run_sign) {
            ++run;
            run_sum += v;
        } else {
            run = sg != 0 ? 1 : 0;
            run_sign = sg;
            run_sum = sg != 0 ? v : 0;
        }
        if (run >= MIN_RUN) return run_sum > 0 ? 1 : -1;
    }
    long sum = 0;
    if (peak_center < 0) {
        int peak_v = 0;
        for (const auto &s : samples)
            if (std::abs(s[axis]) > std::abs(peak_v)) peak_v = s[axis];
        sum = peak_v;
    } else {
        for (int i = peak_center - 1; i <= peak_center + 1; ++i)
            if (i >= 0 && i < n) sum += samples[i][axis];
    }
    return sum > 0 ? 1 : (sum < 0 ? -1 : 0);
}

inline StepResult analyzeStep(const std::vector<RawAxes> &samples,
                              int sign_threshold = SIGN_THRESHOLD, int min_peak = MIN_PEAK) {
    StepResult r;
    std::array<int, AX_COUNT> center{};
    r.peaks = smoothedPeaks(samples, &center);
    r.axis = strongestAxis(r.peaks);
    if (r.axis < 0) {
        r.too_weak = true;
        // Rohspitzen vorhanden, aber nirgends gehalten -> Hinweis auf Zuckungen.
        const auto raw = peakAbs(samples);
        r.message = strongestAxis(raw) >= 0
                        ? "Keine anhaltende Bewegung erkannt (nur kurze Ausschlaege) - bitte "
                          "gleichmaessig und etwas laenger bewegen."
                        : "Keine Bewegung erkannt - bitte nach \"Start\" die Kappe bewegen.";
        return r;
    }
    r.peak = r.peaks[r.axis];
    r.runner_up = strongestAxis(r.peaks, r.axis);
    r.ratio = (r.runner_up >= 0 && r.peak > 0) ? double(r.peaks[r.runner_up]) / r.peak : 0.0;
    r.sign = firstExcursionSign(samples, r.axis, sign_threshold, center[r.axis]);
    const std::string an = axisName(Axis(r.axis));
    if (r.peak < min_peak) {
        r.too_weak = true;
        r.message = "Zu schwach (Spitze " + std::to_string(r.peak) + " auf " + an +
                    ", noetig " + std::to_string(min_peak) + ") - bitte kraeftiger bewegen.";
        return r;
    }
    if (r.ratio >= TIE_RATIO) {
        r.ambiguous = true;
        r.message = "Nicht eindeutig: " + an + " " + std::to_string(r.peak) + " und " +
                    axisName(Axis(r.runner_up)) + " " + std::to_string(r.peaks[r.runner_up]) +
                    " sind fast gleich stark - bitte wiederholen und sauberer in nur eine "
                    "Richtung bewegen.";
        return r;
    }
    r.valid = r.sign != 0;
    r.message = "Erkannt: " + std::string(r.sign < 0 ? "-" : "+") + an + " (Spitze " +
                std::to_string(r.peak) + ")";
    if (r.runner_up >= 0)
        r.message += ", zweitstaerkste Achse: " + std::string(axisName(Axis(r.runner_up))) + " " +
                     std::to_string(r.peaks[r.runner_up]);
    r.message += ".";
    if (r.ratio > AMBIGUOUS_RATIO)
        r.message += " Knapp - ggf. wiederholen und sauberer in nur eine Richtung bewegen.";
    return r;
}

// Baut einen fuer spn::parseMix gueltigen Ausdruck aus (Achse, Vorzeichen)-Termen,
// Gewicht immer 1. Doppelte Achsen werden nur einmal uebernommen (erster gewinnt).
// Beispiel: {{AX_Z,+1},{AX_RX,+1}} -> "z + rx"; {{AX_RY,-1}} -> "-ry".
inline std::string buildMix(const std::vector<std::pair<int, int>> &terms) {
    std::string out;
    std::vector<int> used;
    for (const auto &t : terms) {
        if (t.first < 0 || t.first >= AX_COUNT || t.second == 0) continue;
        bool dup = false;
        for (int u : used) dup = dup || u == t.first;
        if (dup) continue;
        used.push_back(t.first);
        const char *n = axisName(Axis(t.first));
        if (out.empty()) out = std::string(t.second < 0 ? "-" : "") + n;
        else out += std::string(t.second < 0 ? " - " : " + ") + n;
    }
    return out;
}

struct WizardResult {
    bool ok = false;                   // alle Pflichtschritte gueltig -> uebernehmbar
    std::string mix_v, mix_h, mix_zoom;
    std::vector<std::string> errors;   // verhindern "Uebernehmen"
    std::vector<std::string> warnings; // nur Hinweise
};

// Fuehrt die Schrittergebnisse zusammen. skipped[r] = Schritt bewusst
// uebersprungen (nur fuer optionale Schritte erlaubt).
inline WizardResult evaluate(const std::array<StepResult, ROLE_COUNT> &res,
                             const std::array<bool, ROLE_COUNT> &skipped) {
    WizardResult w;
    std::array<bool, ROLE_COUNT> used{};
    for (int r = 0; r < ROLE_COUNT; ++r) {
        if (skipped[r] && roleOptional(r)) continue;
        if (skipped[r]) {
            w.errors.push_back(std::string("Schritt \"") + roleTitle(r) +
                               "\" ist Pflicht und darf nicht uebersprungen werden.");
            continue;
        }
        if (!res[r].valid) {
            w.errors.push_back(std::string("Schritt \"") + roleTitle(r) +
                               "\" hat kein gueltiges Ergebnis" +
                               (res[r].too_weak ? " (zu schwach)."
                                                 : res[r].ambiguous ? " (nicht eindeutig)." : "."));
            continue;
        }
        used[r] = true;
    }
    // Widerspruechliche Rollen: dieselbe Achse in zwei Schritten. Ueber Kanaele
    // hinweg (Scroll V/H/Zoom) ist das ein Fehler - sonst wuerde z.B. scroll_v=x und
    // scroll_h=x geschrieben. Innerhalb eines Kanals (Schieben+Kippen) faellt der
    // Duplikat-Term im Mix weg (buildMix) - das wird als Warnung gemeldet.
    for (int a = 0; a < ROLE_COUNT; ++a)
        for (int b = a + 1; b < ROLE_COUNT; ++b)
            if (used[a] && used[b] && res[a].axis == res[b].axis) {
                const std::string base = std::string("Achse ") + axisName(Axis(res[a].axis)) +
                                         " wurde bei \"" + roleTitle(a) + "\" und bei \"" +
                                         roleTitle(b) + "\" erkannt - widerspruechlich";
                if (roleChannel(a) != roleChannel(b))
                    w.errors.push_back(base + " (zwei verschiedene Kanaele wuerden dieselbe "
                                              "Achse verwenden), bitte die Schritte wiederholen.");
                else
                    w.warnings.push_back(base + "; sie wird im Mix nur einmal verwendet, bitte "
                                                "die Schritte wiederholen.");
            }
    for (int r = 0; r < ROLE_COUNT; ++r)
        if (used[r] && res[r].axis == AX_Y)
            w.warnings.push_back(std::string("Bei \"") + roleTitle(r) +
                                 "\" war Heben/Druecken (y) am staerksten - vermutlich "
                                 "zu stark nach unten gedrueckt.");

    auto term = [&](int r) {
        return used[r] ? std::make_pair(res[r].axis, res[r].sign) : std::make_pair(-1, 0);
    };
    w.mix_v = buildMix({term(ROLE_PUSH_V), term(ROLE_TILT_V)});
    w.mix_h = buildMix({term(ROLE_PUSH_H), term(ROLE_TILT_H)});
    w.mix_zoom = buildMix({term(ROLE_TWIST)});
    w.ok = w.errors.empty() && !w.mix_v.empty() && !w.mix_h.empty() && !w.mix_zoom.empty();
    return w;
}

}  // namespace wiz
}  // namespace spn
