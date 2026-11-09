// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Documentation against the parser, in both directions, for the keys of dofscroll.conf and of the profile files:
//  1. Every key named in the first cell of a table row of docs/configuration.md and docs/wiki/Configuration-reference.md
//     (`KEY`, `<CH>_SENS`, `<P>TRIGGER`, `BUTTON_<n>`, `SCROLL_<V|H>_<PUSH|TILT>_SENS`) is a key applyKey knows.
//  2. Every key applyKey knows is named in both pages (the old pre-1.0 names only in docs/configuration.md's "Older key
//     names" sentence or the wiki's, wherever they are documented).
// A new key therefore needs its documentation in the same change, and a renamed or removed key cannot stay documented.
// Run from the repository root (ctest does).
#undef NDEBUG
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "config.hpp"

using namespace spn;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
    std::fprintf(stderr, __VA_ARGS__); std::fprintf(stderr, "\n"); } } while (0)

static std::string slurp(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// "KEY" or a pattern with <CH>, <P>, <n>, <V|H>, <PUSH|TILT>: every concrete key it stands for; empty if the span is no key.
static std::vector<std::string> expand(const std::string &span) {
    if (span.empty() || span[0] < 'A' || span[0] > 'Z') return {};
    std::vector<std::string> cur = {span};
    auto sub = [&](const std::string &ph, const std::vector<std::string> &vals) {
        std::vector<std::string> next;
        for (const std::string &s : cur) {
            const size_t at = s.find(ph);
            if (at == std::string::npos) {
                next.push_back(s);
                continue;
            }
            for (const std::string &v : vals) next.push_back(s.substr(0, at) + v + s.substr(at + ph.size()));
        }
        cur = next;
    };
    const std::vector<std::string> prefixes = {"SCROLL_V_PUSH_", "SCROLL_V_TILT_", "SCROLL_H_PUSH_", "SCROLL_H_TILT_", "ZOOM_", "LIFT_"};
    sub("<CH>", {"SCROLL_V", "SCROLL_H", "ZOOM"});
    sub("<P>", prefixes);
    sub("<prefix>", prefixes);
    sub("<n>", {"1"});
    sub("<V|H>", {"V", "H"});
    sub("<PUSH|TILT>", {"PUSH", "TILT"});
    // A key is upper case letters, digits and '_' once the placeholders are filled in (this drops file names, values, prose).
    std::vector<std::string> out;
    for (const std::string &k : cur) {
        bool ok = true;
        for (char c : k) ok = ok && ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_');
        if (ok) out.push_back(k);
    }
    return out;
}

static bool knownName(const std::string &key) {
    Settings s;
    std::string err;
    applyKey(s, key, "1", &err);
    return err.find("unknown key '" + key + "'") == std::string::npos;
}

static bool mentions(const std::string &page, const std::string &word) {
    for (size_t at = page.find(word); at != std::string::npos; at = page.find(word, at + 1)) {
        auto isId = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; };
        const bool before = at > 0 && isId(page[at - 1]);
        const bool after = at + word.size() < page.size() && isId(page[at + word.size()]);
        if (!before && !after) return true;
    }
    return false;
}

int main() {
    const char *pages[] = {"docs/configuration.md", "docs/wiki/Configuration-reference.md"};
    for (const char *path : pages) {
        const std::string text = slurp(path);
        CHECK(!text.empty(), "%s not found (run from the repository root)", path);
        std::istringstream in(text);
        std::string line;
        size_t keysSeen = 0;
        while (std::getline(in, line)) {
            if (line.compare(0, 3, "| `") != 0) continue;
            const size_t cellEnd = line.find(" |", 3);
            const std::string cell = line.substr(2, cellEnd == std::string::npos ? std::string::npos : cellEnd - 2);
            for (size_t b = cell.find('`'); b != std::string::npos; b = cell.find('`', b + 1)) {
                const size_t e = cell.find('`', b + 1);
                if (e == std::string::npos) break;
                for (const std::string &k : expand(cell.substr(b + 1, e - b - 1))) {
                    ++keysSeen;
                    CHECK(knownName(k), "%s documents '%s' but the parser does not know it", path, k.c_str());
                }
                b = e;
            }
        }
        CHECK(keysSeen > 20, "%s: only %zu keys found in the tables (table format changed?)", path, keysSeen);
    }

    // Every key of the parser (config.cpp applyKeyImpl). The generated families come from partTuningKeys()/keyTuningKeys().
    std::vector<std::string> all = {
        "PRIORITY", "ZOOM_LATERAL", "INVERT_AXES", "ZOOM_MODIFIER", "ZOOM_ENTER_MS", "ZOOM_EXIT_RATIO", "STEP_TRIGGER",
        "MOUSE_SPEED", "BACKEND", "INPUT", "TICK_HZ", "LIVE_FEED", "MODE", "ENABLED", "NAV_BUTTON", "CLICK_BUTTON",
        "NAV_BUTTON_ENABLED", "CLICK_BUTTON_ENABLED", "V_ACTION", "H_ACTION", "TWIST_ACTION", "LIFT_ACTION", "V_PUSH_ACTION",
        "V_TILT_ACTION", "H_PUSH_ACTION", "H_TILT_ACTION", "LIFT_MIX", "LIFT_SENS", "LIFT_DEADZONE", "LIFT_SIGN", "LIFT_CURVE",
        "LIFT_SMOOTH_MS"};
    for (const std::string &k : partTuningKeys()) all.push_back(k);
    for (const std::string &k : keyTuningKeys()) all.push_back(k);
    for (const char *ch : {"SCROLL_V", "SCROLL_H", "ZOOM"})
        for (const char *f : {"MIX", "SENS", "DEADZONE", "SIGN", "CURVE", "SMOOTH_MS"}) all.push_back(std::string(ch) + "_" + f);
    const std::vector<std::string> legacy = {"SCROLL_SENSITIVITY_Y", "SCROLL_SENSITIVITY_X", "ZOOM_SENSITIVITY",
                                              "SCROLL_DEADZONE_Y", "SCROLL_DEADZONE_X", "INVERT_Y", "INVERT_X", "ZOOM_INVERT",
                                              "RESPONSE_CURVE"};
    for (const std::string &k : all) CHECK(knownName(k), "the list of this test names '%s' but the parser does not know it", k.c_str());
    for (const std::string &k : legacy) CHECK(knownName(k), "legacy key '%s' is gone from the parser", k.c_str());

    // The shorthand of the pages: a family is documented as <CH>_X, <P>X or SCROLL_<V|H>_<PUSH|TILT>_X, so a key is documented
    // when its full name, or its family name with the placeholder, or (for a family suffix) the bare suffix appears.
    auto documented = [](const std::string &page, const std::string &key) {
        if (mentions(page, key)) return true;
        for (const char *ch : {"SCROLL_V", "SCROLL_H", "ZOOM"}) {
            const std::string pre = std::string(ch) + "_";
            if (key.compare(0, pre.size(), pre) == 0 && mentions(page, "<CH>_" + key.substr(pre.size()))) return true;
        }
        for (const char *p : {"SCROLL_V_PUSH_", "SCROLL_V_TILT_", "SCROLL_H_PUSH_", "SCROLL_H_TILT_", "ZOOM_", "LIFT_"}) {
            const std::string pre = p;
            if (key.compare(0, pre.size(), pre) != 0) continue;
            const std::string rest = key.substr(pre.size());
            if ((rest == "KEY_RATE_MIN" || rest == "KEY_RATE_MAX" || rest == "TRIGGER") &&
                (mentions(page, "<P>" + rest) || mentions(page, "<prefix>" + rest)))
                return true;
        }
        if (key.compare(0, 7, "SCROLL_") == 0 && key.size() > 14 && key[8] == '_' &&
            (key.compare(9, 5, "PUSH_") == 0 || key.compare(9, 5, "TILT_") == 0)) {
            const std::string rest = key.substr(14);
            if (mentions(page, "SCROLL_<V|H>_<PUSH|TILT>_" + rest) || mentions(page, "<PUSH|TILT>_" + rest) ||
                mentions(page, "<V|H>_<PUSH|TILT>_" + rest))
                return true;
            // the page lists the part keys as SCROLL_<V|H>_<PUSH|TILT>_<SENS|DEADZONE|...>
            const std::string list = "SCROLL_<V|H>_<PUSH|TILT>_<";
            const size_t at = page.find(list);
            if (at != std::string::npos) {
                const size_t end = page.find('>', at + list.size());
                const std::string alts = "|" + page.substr(at + list.size(), end - at - list.size()) + "|";
                if (alts.find("|" + rest + "|") != std::string::npos) return true;
            }
        }
        return false;
    };
    for (const char *path : pages) {
        const std::string text = slurp(path);
        for (const std::string &k : all) CHECK(documented(text, k), "%s does not document '%s'", path, k.c_str());
    }
    // The old names: the wiki lists them in one sentence, configuration.md in "Older key names".
    for (const char *path : pages) {
        const std::string text = slurp(path);
        for (const std::string &k : legacy) {
            // the wiki abbreviates SCROLL_SENSITIVITY_X and _Y to SCROLL_SENSITIVITY_X/Y
            const bool xy = k.size() > 2 && k[k.size() - 2] == '_' && (k.back() == 'X' || k.back() == 'Y');
            const bool ok = mentions(text, k) || (xy && text.find(k.substr(0, k.size() - 1) + "X/Y") != std::string::npos);
            CHECK(ok, "%s does not mention the legacy key '%s'", path, k.c_str());
        }
    }
    if (failures) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts("test_docs_keys OK");
    return 0;
}
