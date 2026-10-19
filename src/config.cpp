// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Implementation of the configuration system; see config.hpp for the
// file format and the contract.
#include "config.hpp"
#include "modes.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <pwd.h>
#include <sys/random.h>
#include <time.h>

#include <sstream>

namespace spn {

namespace {

// ---------------------------------------------------------------- Helpers

std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
    return s.substr(a, b - a);
}

std::string lower(std::string s) {
    for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upper(std::string s) {
    for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool setErr(std::string *err, const std::string &msg) {
    if (err) *err = msg;
    return false;
}

// Shortest representation that parses back to exactly the same double.
std::string fmtNum(double v) {
    char buf[64];
    for (int prec = 6; prec <= 17; prec++) {
        std::snprintf(buf, sizeof buf, "%.*g", prec, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    return buf;
}

bool parseDouble(const std::string &text, double *out) {
    std::string t = trim(text);
    if (t.empty()) return false;
    char *end = nullptr;
    errno = 0;
    double v = std::strtod(t.c_str(), &end);
    if (end == t.c_str() || *end != '\0' || !std::isfinite(v)) return false;
    *out = v;
    return true;
}

// Integer; "5" and "5.0" are ok, "5.5" is not.
bool parseInt(const std::string &text, long *out) {
    double v;
    if (!parseDouble(text, &v)) return false;
    if (std::fabs(v) > 1e9 || v != std::floor(v)) return false;
    *out = static_cast<long>(v);
    return true;
}

bool parseBool(const std::string &text, bool *out) {
    std::string t = lower(trim(text));
    if (t == "1" || t == "true" || t == "yes" || t == "on") { *out = true; return true; }
    if (t == "0" || t == "false" || t == "no" || t == "off") { *out = false; return true; }
    return false;
}

bool axisFromName(const std::string &name, Axis *out) {
    std::string n = lower(name);
    for (int a = 0; a < AX_COUNT; a++) {
        if (n == axisName(static_cast<Axis>(a))) { *out = static_cast<Axis>(a); return true; }
    }
    return false;
}

// Splits at whitespace/comma. More than `max` tokens -> *over = true and abort
// (no unbounded growth).
std::vector<std::string> splitList(const std::string &text, size_t max, bool *over) {
    std::vector<std::string> out;
    std::string cur;
    *over = false;
    auto flush = [&] {
        if (cur.empty()) return true;
        if (out.size() >= max) { *over = true; return false; }
        out.push_back(cur);
        cur.clear();
        return true;
    };
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            if (!flush()) return out;
        } else {
            cur += c;
        }
    }
    flush();
    return out;
}

bool parseLateral(const std::string &text, std::vector<Axis> *out, std::string *err) {
    std::vector<Axis> res;
    bool over;
    std::vector<std::string> toks = splitList(text, kMaxLateralAxes * 4, &over);
    if (over) return setErr(err, "too many axes (max. " + std::to_string(kMaxLateralAxes) + ")");
    for (const std::string &tok : toks) {
        Axis a;
        if (!axisFromName(tok, &a)) return setErr(err, "unknown axis '" + tok + "'");
        if (std::find(res.begin(), res.end(), a) == res.end()) res.push_back(a);
    }
    if (res.size() > kMaxLateralAxes)
        return setErr(err, "too many axes (max. " + std::to_string(kMaxLateralAxes) + ")");
    *out = res;
    return true;
}

std::string lateralToString(const std::vector<Axis> &v) {
    std::string s;
    for (Axis a : v) {
        if (!s.empty()) s += ' ';
        s += axisName(a);
    }
    return s;
}

// CURVE: a number (exponent) OR a point list "in:factor in:factor".
bool parseCurve(const std::string &text, ChannelCfg *c, std::string *err) {
    bool over;
    std::vector<std::string> toks = splitList(text, kMaxCurvePoints, &over);
    if (over) return setErr(err, "CURVE: too many points (max. " + std::to_string(kMaxCurvePoints) + ")");
    if (toks.empty()) return setErr(err, "CURVE: value missing");
    bool any_colon = false;
    for (const std::string &t : toks) any_colon |= (t.find(':') != std::string::npos);
    if (!any_colon) {
        double v;
        if (toks.size() != 1 || !parseDouble(toks[0], &v))
            return setErr(err, "CURVE: number or point list 'in:factor ...' expected");
        c->curve_exp = std::min(10.0, std::max(0.1, v));
        c->curve_points.clear();
        return true;
    }
    std::vector<std::pair<double, double>> pts;
    for (const std::string &t : toks) {
        size_t p = t.find(':');
        double in, f;
        if (p == std::string::npos || !parseDouble(t.substr(0, p), &in) ||
            !parseDouble(t.substr(p + 1), &f))
            return setErr(err, "CURVE: invalid point '" + t + "'");
        if (in < 0 || f < 0) return setErr(err, "CURVE: Punkt '" + t + "' must not be negative");
        // Upper bounds: otherwise step-rate/click flood or overflow (see docs/security.md S2).
        if (in > 100000 || f > 100) return setErr(err, "CURVE: Punkt '" + t + "' outside the allowed range (factor <= 100)");
        if (!pts.empty() && in <= pts.back().first)
            return setErr(err, "CURVE: inputs must be ascending ('" + t + "')");
        pts.emplace_back(in, f);
    }
    c->curve_points = pts;
    return true;
}

std::string curveToString(const ChannelCfg &c) {
    if (c.curve_points.empty()) return fmtNum(c.curve_exp);
    std::string s;
    for (const auto &p : c.curve_points) {
        if (!s.empty()) s += ' ';
        s += fmtNum(p.first) + ":" + fmtNum(p.second);
    }
    return s;
}

// Sets a channel field by suffix (MIX, SENS, ...). false + err on error,
// *known = false if the suffix does not exist.
bool applyChannel(ChannelCfg &c, const std::string &suffix, const std::string &value,
                  std::string *err, bool *known) {
    *known = true;
    long n;
    double d;
    if (suffix == "MIX") return parseMix(value, &c.mix, err);
    if (suffix == "SENS") {
        if (!parseInt(value, &n)) return setErr(err, "integer expected");
        c.sensitivity = static_cast<int>(std::clamp(n, 0L, 255L));
        return true;
    }
    if (suffix == "DEADZONE") {
        if (!parseInt(value, &n)) return setErr(err, "integer expected");
        c.deadzone = static_cast<int>(std::max(n, 0L));
        return true;
    }
    if (suffix == "SIGN") {
        if (!parseInt(value, &n) || (n != 1 && n != -1)) return setErr(err, "1 or -1 expected");
        c.sign = static_cast<int>(n);
        return true;
    }
    if (suffix == "CURVE") return parseCurve(value, &c, err);
    if (suffix == "SMOOTH_MS") {
        if (!parseDouble(value, &d)) return setErr(err, "number expected");
        c.smooth_ms = std::clamp(d, 0.0, 500.0);
        return true;
    }
    *known = false;
    return false;
}

// The actual logic, works directly on s (the caller ensures atomicity).
bool applyKeyImpl(Settings &s, const std::string &keyIn, const std::string &value,
                  std::string *err) {
    const std::string key = upper(trim(keyIn));
    long n;
    double d;
    bool b;

    // --- global keys
    if (key == "ZOOM_LATERAL") return parseLateral(value, &s.zoom_lateral, err);
    if (key == "ZOOM_ENTER_MS") {
        if (!parseInt(value, &n)) return setErr(err, "integer expected");
        s.zoom_enter_ms = static_cast<int>(std::max(n, 0L));
        return true;
    }
    if (key == "ZOOM_EXIT_RATIO") {
        if (!parseDouble(value, &d)) return setErr(err, "number expected");
        s.zoom_exit_ratio = std::min(kMaxZoomExitRatio, std::max(1.0, d));
        return true;
    }
    if (key == "BACKEND") {
        std::string v = lower(trim(value));
        if (v != "auto" && v != "x11" && v != "uinput")
            return setErr(err, "auto, x11 or uinput expected");
        s.backend = v;
        return true;
    }
    if (key == "TICK_HZ") {
        if (!parseInt(value, &n)) return setErr(err, "integer expected");
        s.tick_hz = static_cast<int>(std::clamp(n, 30L, 1000L));
        return true;
    }
    if (key == "MODE") {
        std::string v = lower(trim(value));
        if (!validModeId(v)) return setErr(err, "mode id expected ([a-z0-9_-], max. 32 characters)");
        s.mode = v;
        return true;
    }
    if (key == "ENABLED") {
        if (!parseBool(value, &b)) return setErr(err, "0/1 expected");
        s.enabled = b;
        return true;
    }
    if (key.compare(0, 7, "BUTTON_") == 0) {
        const std::string num = key.substr(7);
        long bn = 0;
        if (num.empty() || num.size() > 3 || num.find_first_not_of("0123456789") != std::string::npos ||
            (bn = std::strtol(num.c_str(), nullptr, 10)) < 1 || bn > kMaxButtons)
            return setErr(err, "BUTTON_<n> needs n in 1.." + std::to_string(kMaxButtons));
        ButtonActionType bt;
        Key bk = Key::Space;
        if (!parseButtonAction(value, &bt, &bk, err)) return false;
        std::string canon = lower(trim(value));
        if (bt == ButtonActionType::Key) canon = std::string("key:") + keyName(bk);
        s.buttons[static_cast<int>(bn)] = canon;
        return true;
    }
    if (key == "NAV_BUTTON" || key == "CLICK_BUTTON") {
        if (!parseInt(value, &n) || n < 0) return setErr(err, "button number >= 0 expected");
        (key == "NAV_BUTTON" ? s.nav_button : s.click_button) = static_cast<int>(n);
        return true;
    }
    if (key == "NAV_BUTTON_ENABLED" || key == "CLICK_BUTTON_ENABLED") {
        if (!parseBool(value, &b)) return setErr(err, "0/1 expected");
        (key == "NAV_BUTTON_ENABLED" ? s.nav_button_enabled : s.click_button_enabled) = b;
        return true;
    }

    // --- legacy keys
    if (key == "SCROLL_SENSITIVITY_Y") return applyKeyImpl(s, "SCROLL_V_SENS", value, err);
    if (key == "SCROLL_SENSITIVITY_X") return applyKeyImpl(s, "SCROLL_H_SENS", value, err);
    if (key == "ZOOM_SENSITIVITY") return applyKeyImpl(s, "ZOOM_SENS", value, err);
    if (key == "SCROLL_DEADZONE_Y") return applyKeyImpl(s, "SCROLL_V_DEADZONE", value, err);
    if (key == "SCROLL_DEADZONE_X") return applyKeyImpl(s, "SCROLL_H_DEADZONE", value, err);
    if (key == "INVERT_Y") return applyKeyImpl(s, "SCROLL_V_SIGN", value, err);
    if (key == "INVERT_X") return applyKeyImpl(s, "SCROLL_H_SIGN", value, err);
    if (key == "ZOOM_INVERT") {
        if (!parseBool(value, &b)) return setErr(err, "0/1 expected");
        s.zoom.sign = b ? -1 : 1;
        return true;
    }
    if (key == "RESPONSE_CURVE") {
        if (!parseDouble(value, &d)) return setErr(err, "number expected");
        for (ChannelCfg *c : {&s.scroll_v, &s.scroll_h, &s.zoom}) {
            c->curve_exp = std::min(10.0, std::max(0.1, d));
            c->curve_points.clear();  // otherwise the exponent would have no effect
        }
        return true;
    }

    // --- channel keys (PREFIX_SUFFIX)
    struct Pref { const char *name; ChannelCfg Settings::*cfg; };
    static const Pref prefs[] = {{"SCROLL_V_", &Settings::scroll_v},
                                 {"SCROLL_H_", &Settings::scroll_h},
                                 {"ZOOM_", &Settings::zoom}};
    for (const Pref &p : prefs) {
        size_t len = std::strlen(p.name);
        if (key.compare(0, len, p.name) != 0) continue;
        bool known = false;
        bool ok = applyChannel(s.*(p.cfg), key.substr(len), value, err, &known);
        if (known) {
            if (!ok && err && err->empty()) *err = "invalid value";
            return ok;
        }
    }
    return setErr(err, "unknown key '" + keyIn + "'");
}

bool containsCI(const std::string &haystack, const std::string &needle) {
    if (needle.empty()) return false;
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

}  // namespace

// ---------------------------------------------------------------- Mix

bool parseMix(const std::string &text, Mix *out, std::string *err) {
    Mix m;
    const size_t n = text.size();
    size_t i = 0;
    auto skipws = [&] { while (i < n && std::isspace(static_cast<unsigned char>(text[i]))) i++; };
    skipws();
    if (i >= n) { if (out) *out = m; return true; }  // empty = channel yields 0
    bool first = true;
    while (true) {
        skipws();
        double sign = 1.0;
        if (i < n && (text[i] == '+' || text[i] == '-')) {
            sign = text[i] == '-' ? -1.0 : 1.0;
            i++;
            skipws();
        } else if (!first) {
            return setErr(err, "'+' or '-' expected between terms");
        }
        double weight = 1.0;
        if (i < n && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) {
            char *end = nullptr;
            weight = std::strtod(text.c_str() + i, &end);
            if (end == text.c_str() + i || !std::isfinite(weight) || weight > 100.0)
                return setErr(err, "invalid weight (0..100)");
            i = static_cast<size_t>(end - text.c_str());
            skipws();
            if (i >= n || text[i] != '*') return setErr(err, "'*' expected after the weight");
            i++;
            skipws();
        }
        size_t a = i;
        while (i < n && std::isalpha(static_cast<unsigned char>(text[i]))) i++;
        if (a == i) return setErr(err, "axis name expected");
        std::string name = text.substr(a, i - a);
        Axis ax;
        if (!axisFromName(name, &ax)) return setErr(err, "unknown axis '" + name + "'");
        if (m.terms.size() >= kMaxMixTerms)
            return setErr(err, "too many terms (max. " + std::to_string(kMaxMixTerms) + ")");
        m.terms.push_back({ax, sign * weight});
        skipws();
        if (i >= n) break;
        first = false;
    }
    if (out) *out = m;
    return true;
}

std::string mixToString(const Mix &m) {
    std::string s;
    for (size_t k = 0; k < m.terms.size(); k++) {
        const Term &t = m.terms[k];
        bool neg = std::signbit(t.weight);
        double a = std::fabs(t.weight);
        if (k == 0) s += neg ? "-" : "";
        else s += neg ? " - " : " + ";
        if (a != 1.0) s += fmtNum(a) + "*";
        s += axisName(t.axis);
    }
    return s;
}

// ---------------------------------------------------------------- Settings

Settings::Settings() {
    scroll_v.mix.terms = {{AX_Z, 1.0}, {AX_RX, 1.0}};
    scroll_v.sensitivity = 5;
    scroll_v.deadzone = 30;
    scroll_v.sign = 1;

    scroll_h.mix.terms = {{AX_X, 1.0}, {AX_RZ, -1.0}};
    scroll_h.sensitivity = 3;
    scroll_h.deadzone = 20;
    scroll_h.sign = 1;

    zoom.mix.terms = {{AX_RY, 1.0}};
    zoom.sensitivity = 5;
    zoom.deadzone = 40;
    zoom.sign = 1;

    // cppcheck-suppress useInitializationList
    zoom_lateral = {AX_X, AX_Z, AX_RX, AX_RZ};
}

bool applyKey(Settings &s, const std::string &key, const std::string &value, std::string *err) {
    Settings tmp = s;
    std::string e;
    if (!applyKeyImpl(tmp, key, value, &e)) {
        if (err) *err = e.empty() ? "invalid value" : e;
        return false;
    }
    s = std::move(tmp);
    return true;
}

std::vector<KV> allKeys(const Settings &s) {
    std::vector<KV> kv;
    struct Pref { const char *name; const ChannelCfg *c; };
    const Pref prefs[] = {{"SCROLL_V_", &s.scroll_v}, {"SCROLL_H_", &s.scroll_h}, {"ZOOM_", &s.zoom}};
    for (const Pref &p : prefs) {
        std::string pre = p.name;
        kv.emplace_back(pre + "MIX", mixToString(p.c->mix));
        kv.emplace_back(pre + "SENS", std::to_string(p.c->sensitivity));
        kv.emplace_back(pre + "DEADZONE", std::to_string(p.c->deadzone));
        kv.emplace_back(pre + "SIGN", std::to_string(p.c->sign));
        kv.emplace_back(pre + "CURVE", curveToString(*p.c));
        kv.emplace_back(pre + "SMOOTH_MS", fmtNum(p.c->smooth_ms));
    }
    kv.emplace_back("ZOOM_LATERAL", lateralToString(s.zoom_lateral));
    kv.emplace_back("ZOOM_ENTER_MS", std::to_string(s.zoom_enter_ms));
    kv.emplace_back("ZOOM_EXIT_RATIO", fmtNum(s.zoom_exit_ratio));
    kv.emplace_back("BACKEND", s.backend);
    kv.emplace_back("TICK_HZ", std::to_string(s.tick_hz));
    kv.emplace_back("ENABLED", s.enabled ? "1" : "0");
    kv.emplace_back("MODE", s.mode);
    kv.emplace_back("NAV_BUTTON", std::to_string(s.nav_button));
    kv.emplace_back("NAV_BUTTON_ENABLED", s.nav_button_enabled ? "1" : "0");
    kv.emplace_back("CLICK_BUTTON", std::to_string(s.click_button));
    kv.emplace_back("CLICK_BUTTON_ENABLED", s.click_button_enabled ? "1" : "0");
    for (const auto &b : s.buttons) kv.emplace_back("BUTTON_" + std::to_string(b.first), b.second);
    return kv;
}

std::vector<KV> diffKeys(const Settings &base, const Settings &s) {
    // Matched by key name: the BUTTON_<n> entries vary in number.
    std::unordered_map<std::string, std::string> a;
    for (const KV &p : allKeys(base)) a[p.first] = p.second;
    std::vector<KV> out;
    for (const KV &p : allKeys(s)) {
        auto it = a.find(p.first);
        if (it == a.end() || it->second != p.second) out.push_back(p);
    }
    // A binding present in base but absent in s can only be expressed as an explicit "none".
    for (const auto &b : base.buttons)
        if (!s.buttons.count(b.first)) out.emplace_back("BUTTON_" + std::to_string(b.first), "none");
    return out;
}

// ---------------------------------------------------------------- ProfileSet

ProfileSet::ProfileSet() { sections.push_back(Section()); }

static void applyKvList(Settings &s, const std::vector<KV> &kv) {
    for (const KV &p : kv) applyKey(s, p.first, p.second, nullptr);  // error: skip
}

Settings ProfileSet::sectionSettings(size_t i) const {
    Settings s;
    if (sections.empty()) return s;
    applyKvList(s, sections[0].kv);
    if (i > 0 && i < sections.size()) applyKvList(s, sections[i].kv);
    return s;
}

Settings ProfileSet::resolve(const std::string &device_name, const std::string &wm_class) const {
    return resolve(device_name, wm_class, nullptr);
}

Settings ProfileSet::resolve(const std::string &device_name, const std::string &wm_class,
                             const std::vector<KV> *mode_overrides) const {
    Settings s;
    if (sections.empty()) return s;
    applyKvList(s, sections[0].kv);
    for (size_t i = 1; i < sections.size(); i++)
        if (sections[i].kind == Section::DEVICE && containsCI(device_name, sections[i].pattern))
            applyKvList(s, sections[i].kv);
    if (mode_overrides) applyKvList(s, *mode_overrides);
    for (size_t i = 1; i < sections.size(); i++)
        if (sections[i].kind == Section::APP && containsCI(wm_class, sections[i].pattern))
            applyKvList(s, sections[i].kv);
    return s;
}

void ProfileSet::setSectionFromSettings(size_t i, const Settings &s) {
    if (i >= sections.size()) return;
    if (i == 0) {
        sections[0].kv = allKeys(s);
    } else {
        const Settings base = sectionSettings(0);
        if (sections[i].kind != Section::APP) {
            sections[i].kv = diffKeys(base, s);
            return;
        }
        // APP: resolution is global -> devices -> (mode) -> apps. A value that a
        // device section (alone or all accumulated) sets to something other than
        // what s wants must be written explicitly, even if s equals the global
        // value - otherwise the device value wins.
        const std::vector<KV> want = allKeys(s);
        auto toMap = [](const std::vector<KV> &v) {
            std::unordered_map<std::string, std::string> m;
            for (const KV &p : v) m[p.first] = p.second;
            return m;
        };
        const auto glob = toMap(allKeys(base));
        std::vector<std::unordered_map<std::string, std::string>> devs;
        Settings all = base;
        for (size_t j = 1; j < sections.size(); j++) {
            if (sections[j].kind != Section::DEVICE) continue;
            Settings d = base;
            applyKvList(d, sections[j].kv);
            applyKvList(all, sections[j].kv);
            devs.push_back(toMap(allKeys(d)));
        }
        devs.push_back(toMap(allKeys(all)));
        std::vector<KV> out;
        for (const KV &w : want) {
            auto g = glob.find(w.first);
            bool need = g == glob.end() || g->second != w.second;
            for (size_t d = 0; !need && d < devs.size(); d++) {
                auto it = devs[d].find(w.first);
                if (it != devs[d].end() && it->second != w.second) need = true;
            }
            if (need) out.push_back(w);
        }
        sections[i].kv = out;
    }
}

bool ProfileSet::normalizePattern(const std::string &pattern, std::string *out) {
    std::string t = trim(pattern);
    if (t.empty() || t.find_first_of("]\r\n") != std::string::npos) return false;
    if (out) *out = t;
    return true;
}

static std::string indexKey(Section::Kind kind, const std::string &pattern) {
    return std::string(1, kind == Section::DEVICE ? 'd' : 'a') + lower(pattern);
}

void ProfileSet::rebuildIndex() const {
    idx_.clear();
    for (size_t i = 1; i < sections.size(); i++)
        idx_.emplace(indexKey(sections[i].kind, sections[i].pattern), i);  // first wins
    idx_n_ = sections.size();
}

size_t ProfileSet::find(Section::Kind kind, const std::string &pattern) const {
    if (kind == Section::GLOBAL) return 0;
    std::string pat;
    if (!normalizePattern(pattern, &pat)) return npos;
    if (idx_n_ != sections.size()) rebuildIndex();
    const std::string key = indexKey(kind, pat);
    auto it = idx_.find(key);
    if (it != idx_.end()) {
        const size_t i = it->second;
        if (i > 0 && i < sections.size() && sections[i].kind == kind &&
            lower(sections[i].pattern) == key.substr(1))
            return i;
        rebuildIndex();  // stale (sections modified from outside)
        it = idx_.find(key);
        if (it != idx_.end()) return it->second;
    }
    return npos;
}

size_t ProfileSet::findOrAdd(Section::Kind kind, const std::string &pattern) {
    if (kind == Section::GLOBAL) return 0;
    std::string pat;
    if (!normalizePattern(pattern, &pat)) return npos;
    size_t i = find(kind, pat);
    if (i != npos) return i;
    Section sec;
    sec.kind = kind;
    sec.pattern = pat;
    sections.push_back(sec);
    idx_.emplace(indexKey(kind, pat), sections.size() - 1);
    idx_n_ = sections.size();
    return sections.size() - 1;
}

void ProfileSet::remove(size_t i) {
    if (i == 0 || i >= sections.size()) return;
    sections.erase(sections.begin() + static_cast<long>(i));
    idx_n_ = static_cast<size_t>(-1);  // indices shifted -> rebuild lazily
}

// ---------------------------------------------------------------- File


namespace {

bool isAbs(const char *p) { return p && p[0] == '/'; }

// Reads from fd to EOF into *out, at most `cap` bytes. *toobig = true if there is more.
bool readBounded(int fd, size_t cap, std::string *out, bool *toobig, int *err) {
    *toobig = false;
    char buf[16384];
    while (true) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0) {
            if (errno == EINTR) continue;
            *err = errno;
            return false;
        }
        if (n == 0) return true;
        if (out->size() + static_cast<size_t>(n) > cap) { *toobig = true; return true; }
        out->append(buf, static_cast<size_t>(n));
    }
}

// Creates all directory components with mode 0700 (existing ones stay untouched).
bool mkdirs0700(const std::string &dir, std::string *emsg) {
    size_t i = 0;
    while (i <= dir.size()) {
        size_t j = dir.find('/', i);
        if (j == std::string::npos) j = dir.size();
        std::string cur = dir.substr(0, j);
        if (!cur.empty() && cur != "." && cur != ".." && j > i) {
            if (::mkdir(cur.c_str(), 0700) != 0 && errno != EEXIST) {
                *emsg = cur + ": " + std::strerror(errno);
                return false;
            }
        }
        i = j + 1;
    }
    return true;
}

void randomSuffix(char out[17]) {
    unsigned char r[8];
    ssize_t n = ::getrandom(r, sizeof r, 0);
    if (n != static_cast<ssize_t>(sizeof r)) {
        int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        n = fd >= 0 ? ::read(fd, r, sizeof r) : -1;
        if (fd >= 0) ::close(fd);
        if (n != static_cast<ssize_t>(sizeof r)) {  // last resort: time + PID
            struct timespec ts;
            ::clock_gettime(CLOCK_REALTIME, &ts);
            unsigned long long v = static_cast<unsigned long long>(ts.tv_nsec) * 6364136223846793005ULL ^
                                   static_cast<unsigned long long>(::getpid());
            std::memcpy(r, &v, sizeof r);
        }
    }
    for (int k = 0; k < 8; k++) std::snprintf(out + 2 * k, 3, "%02x", r[k]);
}

// Checks a raw pair of a section before writing.
bool validPair(const KV &p, std::string *why) {
    const std::string &k = p.first, &v = p.second;
    if (k.empty() || k[0] == '[' || k[0] == '#' || k[0] == ';') { *why = "invalid key '" + k + "'"; return false; }
    if (k.find_first_of("\r\n=") != std::string::npos || v.find_first_of("\r\n") != std::string::npos) {
        *why = "key/value with line break or '=' in key: '" + k + "'";
        return false;
    }
    Settings probe;
    std::string e;
    if (!applyKey(probe, k, v, &e)) { *why = k + ": " + e; return false; }
    return true;
}

}  // namespace

namespace {

// Base config directory (without trailing slash).
std::string configBaseDir() {
    const char *xdg = std::getenv("XDG_CONFIG_HOME");
    if (isAbs(xdg)) return xdg;  // relative -> ignore (XDG spec)
    const char *home = std::getenv("HOME");
    if (home && *home) return std::string(home) + "/.config";
    struct passwd *pw = ::getpwuid(::getuid());
    if (pw && isAbs(pw->pw_dir)) return std::string(pw->pw_dir) + "/.config";
    return "./.config";
}

bool lexists(const std::string &p) {
    struct stat st;
    return ::lstat(p.c_str(), &st) == 0;
}

}  // namespace

// Oldest location (before the move into its own folder); the project was then named "spnav-scroll".
std::string legacyConfigPath() { return configBaseDir() + "/spnav-scroll.conf"; }

// Previous location (own folder, project name "spnav-scroll").
std::string previousConfigPath() { return configBaseDir() + "/spnav-scroll/spnav-scroll.conf"; }

static std::string newConfigDir() { return configBaseDir() + "/dofscroll"; }

std::string defaultConfigPath() {
    const std::string np = newConfigDir() + "/dofscroll.conf";
    if (lexists(np)) return np;
    const std::string pp = previousConfigPath();
    if (lexists(pp)) return pp;  // fallback until migration
    const std::string lp = legacyConfigPath();
    if (lexists(lp)) return lp;  // fallback until migration
    return np;
}

bool migrateLegacyConfig(std::string *log) {
    auto fail = [&](const std::string &msg) {
        if (log) *log = msg;
        return false;
    };
    const std::string dir = newConfigDir();
    const std::string np = dir + "/dofscroll.conf";
    // Source: the previous location first, otherwise the oldest.
    const std::string lp = lexists(previousConfigPath()) ? previousConfigPath() : legacyConfigPath();
    if (lexists(np)) return true;
    struct stat lst;
    if (::lstat(lp.c_str(), &lst) != 0) return fail("no old configuration to migrate (" + lp + ")");
    const bool isLink = S_ISLNK(lst.st_mode);
    if (!isLink && !S_ISREG(lst.st_mode))
        return fail(lp + ": neither regular file nor symlink - not migrated");

    // Preparation before creating the directory (so nothing is left behind on errors).
    std::string linkTarget, content;
    mode_t mode = 0600;
    if (isLink) {
        std::string l(4096, '\0');
        ssize_t n = ::readlink(lp.c_str(), &l[0], l.size());
        if (n <= 0 || static_cast<size_t>(n) >= l.size())
            return fail(lp + ": symlink not readable");
        l.resize(static_cast<size_t>(n));
        if (l[0] == '/') {
            linkTarget = l;
        } else {
            size_t slash = lp.rfind('/');
            linkTarget = (slash == std::string::npos ? std::string() : lp.substr(0, slash + 1)) + l;
            if (linkTarget[0] != '/') {  // relative base directory -> make absolute
                char *cwd = ::getcwd(nullptr, 0);
                if (cwd) { linkTarget = std::string(cwd) + "/" + linkTarget; std::free(cwd); }
            }
            char *rp = ::realpath(linkTarget.c_str(), nullptr);
            if (rp) { linkTarget = rp; std::free(rp); }
        }
        struct stat tst;
        if (::stat(linkTarget.c_str(), &tst) == 0 && !S_ISREG(tst.st_mode))
            return fail(lp + ": symlink target is not a regular file - not migrated");
    } else {
        int fd = ::open(lp.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) return fail(lp + ": " + std::strerror(errno));
        struct stat st;
        if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
            ::close(fd);
            return fail(lp + ": not a regular file - not migrated");
        }
        if (st.st_size > 0 && static_cast<size_t>(st.st_size) > kMaxConfigBytes) {
            ::close(fd);
            return fail(lp + ": file too large (max. " + std::to_string(kMaxConfigBytes) + " bytes) - not migrated");
        }
        bool toobig = false;
        int rerr = 0;
        bool rok = readBounded(fd, kMaxConfigBytes, &content, &toobig, &rerr);
        ::close(fd);
        if (!rok) return fail(lp + ": " + std::strerror(rerr));
        if (toobig) return fail(lp + ": file too large (max. " + std::to_string(kMaxConfigBytes) + " bytes) - not migrated");
        mode = st.st_mode & 0777;
    }

    std::string emsg;
    if (!mkdirs0700(dir, &emsg)) return fail(emsg);
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) return fail(dir + ": " + std::strerror(errno));
    struct DirGuard { int fd; ~DirGuard() { ::close(fd); } } guard{dfd};
    const char *base = "dofscroll.conf";
    const std::string okMsg = "configuration migrated to " + np + "; the old file " + lp +
                              " is left unchanged and can be removed";

    if (isLink) {
        if (::symlinkat(linkTarget.c_str(), dfd, base) != 0) {
            if (errno == EEXIST) return true;  // migrated in parallel
            return fail(np + ": " + std::strerror(errno));
        }
        (void)::fsync(dfd);
        if (log) *log = okMsg;
        return true;
    }

    std::string tmp;
    int fd = -1;
    for (int attempt = 0; attempt < 8 && fd < 0; attempt++) {
        char suf[17];
        randomSuffix(suf);
        tmp = std::string(base) + ".tmp." + suf;
        fd = ::openat(dfd, tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST) return fail(dir + "/" + tmp + ": " + std::strerror(errno));
    }
    if (fd < 0) return fail(dir + "/" + base + ".tmp.*: " + std::strerror(EEXIST));
    std::string m;
    size_t off = 0;
    while (off < content.size()) {
        ssize_t n = ::write(fd, content.data() + off, content.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { m = tmp + ": write failed: " + std::strerror(n < 0 ? errno : EIO); break; }
        off += static_cast<size_t>(n);
    }
    if (m.empty() && ::fchmod(fd, mode) != 0) m = tmp + ": chmod: " + std::strerror(errno);
    if (m.empty() && ::fsync(fd) != 0) m = tmp + ": fsync: " + std::strerror(errno);
    if (::close(fd) != 0 && m.empty()) m = tmp + ": close: " + std::strerror(errno);
    bool exists = false;
    if (m.empty() && ::linkat(dfd, tmp.c_str(), dfd, base, 0) != 0) {
        if (errno == EEXIST) exists = true;  // someone was faster: do not overwrite
        else m = np + ": " + std::strerror(errno);
    }
    (void)::unlinkat(dfd, tmp.c_str(), 0);
    if (!m.empty()) return fail(m);
    if (exists) return true;
    (void)::fsync(dfd);
    if (log) *log = okMsg;
    return true;
}

bool loadProfiles(const std::string &path, ProfileSet *out, std::vector<std::string> *warnings) {
    ProfileSet ps;
    constexpr size_t kMaxWarnings = 200;
    size_t nwarn = 0;
    auto warn = [&](size_t line, const std::string &msg) {
        if (!warnings) return;
        if (nwarn == kMaxWarnings) {
            warnings->push_back(path + ": further warnings suppressed");
            nwarn++;
        }
        if (nwarn > kMaxWarnings) return;
        nwarn++;
        warnings->push_back(path + ":" + std::to_string(line) + ": " + msg);
    };
    // Open once, then work only through the fd (no stat/open race).
    int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        int e = errno;
        if (e == ENOENT) { if (out) *out = ps; return true; }  // missing file = defaults
        if (warnings) warnings->push_back(path + ": " + std::strerror(e));
        return false;
    }
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        if (warnings) warnings->push_back(path + ": not a regular file");
        return false;
    }
    const std::string tooBigMsg = path + ": file too large (max. " + std::to_string(kMaxConfigBytes) + " bytes)";
    if (st.st_size > 0 && static_cast<size_t>(st.st_size) > kMaxConfigBytes) {
        ::close(fd);
        if (warnings) warnings->push_back(tooBigMsg);
        return false;
    }
    std::string data;
    bool toobig = false;
    int rerr = 0;
    bool rok = readBounded(fd, kMaxConfigBytes, &data, &toobig, &rerr);
    ::close(fd);
    if (!rok) {
        if (warnings) warnings->push_back(path + ": " + std::strerror(rerr));
        return false;
    }
    if (toobig) {
        if (warnings) warnings->push_back(tooBigMsg);
        return false;
    }

    long cur = 0;  // current section, -1 = unknown/surplus section (ignore keys)
    size_t lineno = 0;
    size_t pos = 0;
    while (pos < data.size()) {
        size_t nl = data.find('\n', pos);
        if (nl == std::string::npos) nl = data.size();
        const size_t len = nl - pos;
        std::string raw;
        const bool tooLong = len > kMaxLineBytes;
        if (!tooLong) raw.assign(data, pos, len);
        const size_t lstart = pos;
        pos = nl + 1;
        lineno++;
        if (tooLong) {
            warn(lineno, "line too long (max. " + std::to_string(kMaxLineBytes) + " bytes) - ignored");
            // If it was a section header, the keys after it must not fall into the old section.
            size_t q = lstart;
            while (q < nl && std::isspace(static_cast<unsigned char>(data[q]))) q++;
            if (q < nl && data[q] == '[') cur = -1;
            continue;
        }
        if (lineno == 1 && raw.compare(0, 3, "\xEF\xBB\xBF") == 0) raw.erase(0, 3);  // UTF-8-BOM
        std::string line = trim(raw);  // also removes CR
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close == std::string::npos) {
                warn(lineno, "section header without ']' - section ignored");
                cur = -1;
                continue;
            }
            std::string name = trim(line.substr(1, close - 1));
            std::string lname = lower(name);
            if (lname == "global") {
                cur = 0;
            } else if (lname.compare(0, 7, "device:") == 0 || lname.compare(0, 4, "app:") == 0) {
                bool dev = lname[0] == 'd';
                std::string pat = trim(name.substr(name.find(':') + 1));
                const Section::Kind kind = dev ? Section::DEVICE : Section::APP;
                if (pat.empty()) {
                    warn(lineno, "section without pattern - ignored");
                    cur = -1;
                } else if (ps.find(kind, pat) == ProfileSet::npos && ps.sections.size() >= kMaxSections) {
                    warn(lineno, "too many sections (max. " + std::to_string(kMaxSections) + ") - section ignored");
                    cur = -1;
                } else {
                    size_t idx = ps.findOrAdd(kind, pat);
                    cur = idx == ProfileSet::npos ? -1 : static_cast<long>(idx);
                }
            } else {
                warn(lineno, "unknown section '[" + name + "]' - ignored");
                cur = -1;
            }
            continue;
        }

        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            warn(lineno, "line without '=' ignored: " + line);
            continue;
        }
        if (cur < 0) continue;  // section was invalid, warning already issued
        std::string key = upper(trim(line.substr(0, eq)));
        std::string value = line.substr(eq + 1);
        // Trailing comment: ' #' (or '#' right at the start of the value)
        std::string tv = trim(value);
        if (!tv.empty() && tv[0] == '#') {
            value.clear();
        } else {
            for (size_t p = 1; p < value.size(); p++) {
                if (value[p] == '#' && std::isspace(static_cast<unsigned char>(value[p - 1]))) {
                    value.resize(p);
                    break;
                }
            }
        }
        value = trim(value);
        if (key.empty()) { warn(lineno, "line without key ignored"); continue; }
        Settings probe;
        std::string err;
        if (!applyKey(probe, key, value, &err)) {
            warn(lineno, key + ": " + err + " - line ignored");
            continue;
        }
        std::vector<KV> &kv = ps.sections[static_cast<size_t>(cur)].kv;
        // Merge duplicates: last value wins, position of the last occurrence.
        auto dup = std::find_if(kv.begin(), kv.end(), [&](const KV &e) { return e.first == key; });
        if (dup != kv.end()) {
            kv.erase(dup);
        } else if (kv.size() >= kMaxKeysPerSection) {
            warn(lineno, "too many keys in the section (max. " + std::to_string(kMaxKeysPerSection) +
                             ") - line ignored");
            continue;
        }
        kv.emplace_back(key, value);
    }
    if (out) *out = ps;
    return true;
}

bool saveProfiles(const std::string &path, const ProfileSet &ps, std::string *err) {
    std::ostringstream os;
    os << "# dofscroll configuration - written by the tray program.\n"
       << "# Note: comments in this file are NOT preserved when saving.\n"
       << "# Sections: [global], [device:NAME], [app:NAME] - overridden keys only.\n\n";
    os << "[global]\n";
    for (const KV &p : allKeys(ps.sectionSettings(0)))
        os << p.first << " = " << p.second << "\n";
    for (size_t i = 1; i < ps.sections.size(); i++) {
        const Section &sec = ps.sections[i];
        std::string np;
        if (!ProfileSet::normalizePattern(sec.pattern, &np) || np != sec.pattern)
            return setErr(err, "invalid section pattern '" + sec.pattern +
                                   "' (empty, ']' or line break / leading or trailing whitespace)");
        os << "\n[" << (sec.kind == Section::DEVICE ? "device:" : "app:") << sec.pattern << "]\n";
        for (const KV &p : sec.kv) {
            std::string why;
            if (!validPair(p, &why)) return setErr(err, "section '" + sec.pattern + "': " + why);
            os << p.first << " = " << p.second << "\n";
        }
    }

    // Resolve symlink: temp next to the real target, so the link is preserved.
    std::string real = path;
    bool resolved = false;
    for (int depth = 0; depth <= 16; depth++) {
        struct stat lst;
        if (::lstat(real.c_str(), &lst) != 0 || !S_ISLNK(lst.st_mode)) { resolved = true; break; }
        if (depth == 16) break;
        std::string l(4096, '\0');
        ssize_t n = ::readlink(real.c_str(), &l[0], l.size());
        if (n < 0) return setErr(err, real + ": " + std::strerror(errno));
        l.resize(static_cast<size_t>(n));
        if (l.empty()) return setErr(err, real + ": empty symlink");
        if (l[0] == '/') {
            real = l;
        } else {
            size_t slash = real.rfind('/');
            real = (slash == std::string::npos ? std::string() : real.substr(0, slash + 1)) + l;
        }
    }
    if (!resolved) return setErr(err, path + ": " + std::strerror(ELOOP) + " (more than 16 symlinks)");

    std::string dir, base;
    {
        size_t slash = real.rfind('/');
        if (slash == std::string::npos) { dir = "."; base = real; }
        else { dir = slash == 0 ? "/" : real.substr(0, slash); base = real.substr(slash + 1); }
    }
    if (base.empty() || base == "." || base == "..") return setErr(err, real + ": invalid file name");
    if (dir != "." && dir != "/") {
        std::string emsg;
        if (!mkdirs0700(dir, &emsg)) return setErr(err, emsg);
    }
    // Open the directory once; temp file, rename and fsync all go through this handle.
    int dfd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dfd < 0) return setErr(err, dir + ": " + std::strerror(errno));
    struct DirGuard { int fd; ~DirGuard() { ::close(fd); } } guard{dfd};

    mode_t mode = 0600;
    struct stat est;
    if (::fstatat(dfd, base.c_str(), &est, AT_SYMLINK_NOFOLLOW) == 0) {
        if (!S_ISREG(est.st_mode)) return setErr(err, real + ": not a regular file");
        mode = est.st_mode & 0777;  // never carry over setuid/setgid/sticky
    }

    std::string tmp;
    int fd = -1;
    for (int attempt = 0; attempt < 8 && fd < 0; attempt++) {
        char suf[17];
        randomSuffix(suf);
        tmp = base + ".tmp." + suf;
        fd = ::openat(dfd, tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
        if (fd < 0 && errno != EEXIST) return setErr(err, dir + "/" + tmp + ": " + std::strerror(errno));
    }
    if (fd < 0) return setErr(err, dir + "/" + base + ".tmp.*: " + std::strerror(EEXIST));

    std::string m;
    const std::string data = os.str();
    size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { m = tmp + ": write failed: " + std::strerror(n < 0 ? errno : EIO); break; }
        off += static_cast<size_t>(n);
    }
    if (m.empty() && ::fchmod(fd, mode) != 0) m = tmp + ": chmod: " + std::strerror(errno);
    if (m.empty() && ::fsync(fd) != 0) m = tmp + ": fsync: " + std::strerror(errno);
    if (::close(fd) != 0 && m.empty()) m = tmp + ": close: " + std::strerror(errno);
    if (m.empty() && ::renameat(dfd, tmp.c_str(), dfd, base.c_str()) != 0) m = real + ": " + std::strerror(errno);
    if (!m.empty()) {
        (void)::unlinkat(dfd, tmp.c_str(), 0);
        return setErr(err, m);
    }
    (void)::fsync(dfd);  // persist the directory entry (best effort)
    return true;
}

}  // namespace spn
