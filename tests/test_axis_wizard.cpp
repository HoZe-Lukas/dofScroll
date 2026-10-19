// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Unit-Tests fuer src/axis_wizard.hpp (Assistent "Achsen einlernen" im Tray).
// Grundlage sind echte Spitzenwerte einer SpaceMouse Pro (x y z | rx ry rz):
//   Drehen                82 144  90 |  35 350 111
//   Kippen vor/zurueck    61 125 140 | 350  50  80
//   Kippen seitlich      107  95  65 | 107  62 350
//   Schieben seitlich    350 293  60 | 132  70  75
//   Schieben vor/zurueck  69 286 350 | 145  56 155
//   Heben                170 350 149 |  59 103  81
// Daraus werden synthetische Sample-Folgen gebaut: Rampe in die angesagte
// Richtung bis zur Spitze, zurueck auf 0, dann (schwaecher) in die
// Gegenrichtung und wieder zurueck - wie bei echtem Hin-und-Her.
#include <cassert>
#include <cstdio>
#include <string>

#include "axis_wizard.hpp"
#include "config.hpp"

using namespace spn;
using namespace spn::wiz;

// peaks: Spitzenbetrag je Achse; dir: Vorzeichen jeder Achse in der ERSTEN
// Halbbewegung (angesagte Richtung); back: Faktor fuer die Rueckbewegung.
static std::vector<RawAxes> sweep(const RawAxes &peaks, const RawAxes &dir, double back = 0.8,
                                  int n = 40) {
    std::vector<RawAxes> s;
    auto add = [&](double f) {
        RawAxes v{};
        for (int a = 0; a < AX_COUNT; ++a) v[a] = int(peaks[a] * dir[a] * f);
        s.push_back(v);
    };
    s.push_back(RawAxes{});  // Ruhe
    for (int i = 1; i <= n; ++i) add(double(i) / n);         // hin
    for (int i = n - 1; i >= 0; --i) add(double(i) / n);     // zurueck auf 0
    for (int i = 1; i <= n; ++i) add(-back * i / n);         // Gegenrichtung
    for (int i = n - 1; i >= 0; --i) add(-back * i / n);
    return s;
}

static const RawAxes P_TWIST = {82, 144, 90, 35, 350, 111};
static const RawAxes P_TILT_V = {61, 125, 140, 350, 50, 80};
static const RawAxes P_TILT_H = {107, 95, 65, 107, 62, 350};
static const RawAxes P_PUSH_H = {350, 293, 60, 132, 70, 75};
static const RawAxes P_PUSH_V = {69, 286, 350, 145, 56, 155};
static const RawAxes P_LIFT = {170, 350, 149, 59, 103, 81};

// Uebersprech-Vorzeichen bewusst gemischt, damit nur die Hauptachse zaehlt.
static const RawAxes MIXED = {1, -1, 1, -1, 1, -1};

static RawAxes withMain(int axis, int sign) {
    RawAxes d = MIXED;
    d[axis] = sign;
    return d;
}

static void testPeaks() {
    auto p = peakAbs(sweep(P_PUSH_H, MIXED));
    for (int a = 0; a < AX_COUNT; ++a) assert(p[a] == P_PUSH_H[a]);
    assert(strongestAxis(p) == AX_X);
    assert(strongestAxis(p, AX_X) == AX_Y);
    assert(strongestAxis(RawAxes{}) == -1);
}

static void testSingleSteps() {
    struct Case { RawAxes peaks; int axis; };
    const Case cases[] = {{P_PUSH_V, AX_Z}, {P_TILT_V, AX_RX}, {P_PUSH_H, AX_X},
                          {P_TILT_H, AX_RZ}, {P_TWIST, AX_RY}, {P_LIFT, AX_Y}};
    for (const auto &c : cases)
        for (int sign : {1, -1}) {
            StepResult r = analyzeStep(sweep(c.peaks, withMain(c.axis, sign)));
            assert(r.valid && !r.too_weak);
            assert(r.axis == c.axis);
            assert(r.sign == sign);
            assert(r.peak >= 340 && r.peak <= 350);  // geglaettet (Median ueber 3 Samples)
        }
    // Schieben seitlich: y (293) spricht stark mit, x muss trotzdem gewinnen.
    StepResult r = analyzeStep(sweep(P_PUSH_H, withMain(AX_X, 1)));
    assert(r.runner_up == AX_Y && r.ratio > 0.8 && r.ratio < 0.85);
}

static void testSignFromFirstHalf() {
    // Erste Halbbewegung schwach (+60), Rueckbewegung kraeftig (-350):
    // Vorzeichen kommt aus der ersten Bewegung ueber 40, nicht aus der Spitze.
    std::vector<RawAxes> s;
    for (int v : {0, 20, 45, 60, 30, 0, -100, -250, -350, -200, 0}) {
        RawAxes a{};
        a[AX_RY] = v;
        a[AX_X] = v / 4;
        s.push_back(a);
    }
    StepResult r = analyzeStep(s);
    assert(r.valid && r.axis == AX_RY && r.sign == 1);
    // Rauschen unter der Schwelle darf das Vorzeichen nicht bestimmen.
    std::vector<RawAxes> n = {RawAxes{0, 0, -30, 0, 0, 0}, RawAxes{0, 0, 120, 0, 0, 0},
                              RawAxes{0, 0, 300, 0, 0, 0}};
    assert(analyzeStep(n).sign == 1);
}

static void testTooWeak() {
    RawAxes weak = {20, 30, 70, 25, 10, 30};  // Spitze 70 < 80
    StepResult r = analyzeStep(sweep(weak, withMain(AX_Z, 1)));
    assert(!r.valid && r.too_weak && r.axis == AX_Z && r.peak >= 67 && r.peak <= 70);
    assert(r.message.find("schwach") != std::string::npos);
    StepResult e = analyzeStep({});
    assert(!e.valid && e.too_weak && e.axis == -1);
    StepResult z = analyzeStep({RawAxes{}, RawAxes{}});
    assert(!z.valid && z.too_weak);
}

static void testBuildMix() {
    assert(buildMix({{AX_Z, 1}, {AX_RX, 1}}) == "z + rx");
    assert(buildMix({{AX_X, 1}, {AX_RZ, -1}}) == "x - rz");
    assert(buildMix({{AX_RY, -1}}) == "-ry");
    assert(buildMix({{AX_Z, 1}, {-1, 0}}) == "z");
    assert(buildMix({{AX_RX, 1}, {AX_RX, -1}}) == "rx");  // doppelte Achse nur einmal
    assert(buildMix({}).empty());
    // Ergebnis muss vom echten Parser akzeptiert werden und zurueckrunden.
    for (const char *t : {"z + rx", "x - rz", "-ry", "-z - rx"}) {
        Mix m;
        std::string err;
        assert(parseMix(t, &m, &err));
        assert(mixToString(m) == t);
    }
}

// Komplettlauf mit den echten Messwerten und libspnav-typischen Vorzeichen
// (wie die Defaults "z + rx", "x - rz", "ry"): zu dir = +z, zu dir kippen = +rx,
// rechts = +x, rechts kippen = -rz, Uhrzeigersinn = +ry.
static std::array<StepResult, ROLE_COUNT> fullRun(int sz, int srx, int sx, int srz, int sry) {
    std::array<StepResult, ROLE_COUNT> res;
    res[ROLE_PUSH_V] = analyzeStep(sweep(P_PUSH_V, withMain(AX_Z, sz)));
    res[ROLE_TILT_V] = analyzeStep(sweep(P_TILT_V, withMain(AX_RX, srx)));
    res[ROLE_PUSH_H] = analyzeStep(sweep(P_PUSH_H, withMain(AX_X, sx)));
    res[ROLE_TILT_H] = analyzeStep(sweep(P_TILT_H, withMain(AX_RZ, srz)));
    res[ROLE_TWIST] = analyzeStep(sweep(P_TWIST, withMain(AX_RY, sry)));
    return res;
}

static void testEvaluate() {
    std::array<bool, ROLE_COUNT> none{};
    WizardResult w = evaluate(fullRun(1, 1, 1, -1, 1), none);
    assert(w.ok && w.errors.empty() && w.warnings.empty());
    assert(w.mix_v == "z + rx");
    assert(w.mix_h == "x - rz");
    assert(w.mix_zoom == "ry");

    // Anderes Geraet / andere Einbaulage: alle Vorzeichen gedreht.
    w = evaluate(fullRun(-1, -1, -1, 1, -1), none);
    assert(w.ok && w.mix_v == "-z - rx" && w.mix_h == "-x + rz" && w.mix_zoom == "-ry");

    // Kipp-Schritte uebersprungen -> nur je ein Term.
    std::array<bool, ROLE_COUNT> skip{};
    skip[ROLE_TILT_V] = skip[ROLE_TILT_H] = true;
    w = evaluate(fullRun(1, 1, 1, -1, 1), skip);
    assert(w.ok && w.mix_v == "z" && w.mix_h == "x" && w.mix_zoom == "ry");

    // Pflichtschritt uebersprungen -> nicht uebernehmbar.
    std::array<bool, ROLE_COUNT> skipReq{};
    skipReq[ROLE_TWIST] = true;
    w = evaluate(fullRun(1, 1, 1, -1, 1), skipReq);
    assert(!w.ok && !w.errors.empty());

    // Pflichtschritt zu schwach -> Fehler.
    auto res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_PUSH_H] = analyzeStep(sweep(RawAxes{60, 40, 10, 20, 10, 15}, withMain(AX_X, 1)));
    w = evaluate(res, none);
    assert(!w.ok && w.errors.size() == 1);
    assert(w.errors[0].find("schwach") != std::string::npos);

    // Widerspruch: beim "Schieben vor/zurueck" eigentlich gekippt (rx gewinnt)
    // -> dieselbe Achse in zwei Rollen -> Warnung, rx nur einmal im Mix.
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_PUSH_V] = analyzeStep(sweep(P_TILT_V, withMain(AX_RX, 1)));
    w = evaluate(res, none);
    assert(w.ok && w.warnings.size() == 1);
    assert(w.warnings[0].find("rx") != std::string::npos);
    assert(w.mix_v == "rx");

    // Achse ueber Kanaele hinweg doppelt (Drehen als rz erkannt) -> Fehler, kein Uebernehmen.
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_TWIST] = analyzeStep(sweep(P_TILT_H, withMain(AX_RZ, 1)));
    w = evaluate(res, none);
    assert(!w.ok && w.errors.size() == 1);
    assert(w.errors[0].find("rz") != std::string::npos);

    // Schieben vor/zurueck und seitlich beide x -> Fehler (nicht scroll_v=x, scroll_h=x).
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_PUSH_V] = analyzeStep(sweep(P_PUSH_H, withMain(AX_X, 1)));
    w = evaluate(res, none);
    assert(!w.ok && !w.errors.empty());
    assert(w.errors[0].find("x") != std::string::npos);

    // Mehrdeutiger Schritt -> Pflichtschritt ungueltig.
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_TWIST] = analyzeStep(sweep(RawAxes{0, 0, 0, 0, 300, 300}, withMain(AX_RY, 1)));
    w = evaluate(res, none);
    assert(!w.ok && w.errors[0].find("eindeutig") != std::string::npos);

    // y (Heben/Druecken) als staerkste Achse -> Hinweis.
    res = fullRun(1, 1, 1, -1, 1);
    res[ROLE_PUSH_V] = analyzeStep(sweep(P_LIFT, withMain(AX_Y, 1)));
    w = evaluate(res, none);
    bool hasY = false;
    for (const auto &m : w.warnings) hasY = hasY || m.find("(y)") != std::string::npos;
    assert(hasY && w.mix_v == "y + rx");

    // Alle erzeugten Mixe muss parseMix akzeptieren.
    w = evaluate(fullRun(1, 1, 1, -1, 1), none);
    for (const std::string *t : {&w.mix_v, &w.mix_h, &w.mix_zoom}) {
        Mix m;
        std::string err;
        assert(parseMix(*t, &m, &err));
    }
}

// Restbewegung vom vorherigen Schritt: ein einzelnes Sample in Gegenrichtung ueber
// der Schwelle darf das Vorzeichen nicht kippen.
static void testSignRobust() {
    std::vector<RawAxes> s;
    for (int v : {-90, 0, 0, 50, 120, 250, 350, 200, 0, -100, 0}) {
        RawAxes a{};
        a[AX_RX] = v;
        s.push_back(a);
    }
    StepResult r = analyzeStep(s);
    assert(r.valid && r.axis == AX_RX && r.sign == 1);
    // Zwei getrennte Zuckungen (-80, -70) mit Pause dazwischen: ebenfalls kein Lauf.
    std::vector<RawAxes> t;
    for (int v : {-80, 0, -70, 0, 0, 60, 150, 300, 350, 200, 0}) {
        RawAxes a{};
        a[AX_RX] = v;
        t.push_back(a);
    }
    assert(analyzeStep(t).sign == 1);
    // Ohne anhaltende Auslenkung ueber der Schwelle: Vorzeichen um die Spitze.
    std::vector<RawAxes> f;
    for (int v : {0, -30, -35, -38, -30, 0})  // alles <= 40
        f.push_back(RawAxes{0, 0, 0, 0, v, 0});
    assert(firstExcursionSign(f, AX_RY, 40, 2) == -1);
}

// Ein einzelner Spike auf einer Uebersprechachse gewinnt nicht.
static void testSpikeIgnored() {
    auto s = sweep(P_PUSH_V, withMain(AX_Z, 1));
    RawAxes spike{};
    spike[AX_RX] = 340;
    s.insert(s.begin() + 10, spike);
    StepResult r = analyzeStep(s);
    assert(r.valid && r.axis == AX_Z && r.sign == 1);
    assert(r.peaks[AX_RX] < 200);
    // Nur Spikes, nirgends gehalten -> keine gueltige Erkennung.
    std::vector<RawAxes> only;
    for (int i = 0; i < 10; ++i) only.push_back(RawAxes{});
    only[4][AX_X] = 300;
    only[8][AX_RZ] = 300;
    StepResult o = analyzeStep(only);
    assert(!o.valid && o.axis == -1);
}

// Fast gleich starke Achsen (auch exakter Gleichstand) sind nicht eindeutig.
static void testAmbiguous() {
    RawAxes tie = {0, 0, 0, 0, 300, 300};
    StepResult r = analyzeStep(sweep(tie, withMain(AX_RY, 1)));
    assert(!r.valid && r.ambiguous && !r.too_weak);
    RawAxes near = {0, 0, 0, 0, 300, 290};  // Verhaeltnis 0.967
    r = analyzeStep(sweep(near, withMain(AX_RY, 1)));
    assert(!r.valid && r.ambiguous);
    RawAxes ok = {0, 0, 0, 0, 300, 250};  // 0.83: gueltig
    r = analyzeStep(sweep(ok, withMain(AX_RY, 1)));
    assert(r.valid && !r.ambiguous && r.axis == AX_RY);
}

int main() {
    testSignRobust();
    testSpikeIgnored();
    testAmbiguous();
    testPeaks();
    testSingleSteps();
    testSignFromFirstHalf();
    testTooWeak();
    testBuildMix();
    testEvaluate();
    printf("test_axis_wizard: alle Tests bestanden\n");
    return 0;
}
