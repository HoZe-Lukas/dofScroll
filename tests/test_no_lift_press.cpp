// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Lift/press of the cap (axis y) is deliberately unused (issue #94, docs/modes.md): grabbing or
// releasing the cap triggers it by accident. This test scans what ships with the project (the
// built-in mode files, the seeded default configuration, the built-in defaults and the defaults
// documented in docs/configuration.md) for `y` in any *_MIX key or in ZOOM_LATERAL and fails with
// the file and the key. A mode that really wants it says so with "lift/press" in its DESCRIPTION.
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "config.hpp"
#include "ui/model/axis_roles_model.hpp"
#include "ui/model/fresh_state.hpp"

using namespace spn;

static int failures = 0;

static void fail(const std::string &where, const std::string &key, const std::string &what) {
    std::printf("FAIL: %s: %s %s - lift/press (axis y) must stay out of channel mixes and ZOOM_LATERAL "
                "(see docs/modes.md, \"Lift/press is unused\")\n",
                where.c_str(), key.c_str(), what.c_str());
    ++failures;
}

static std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

static std::string upperAscii(std::string s) {
    for (char &c : s)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return s;
}

// LIFT_MIX is the lift / press motion's own mix (stage 2 of #224): it is the one mix that names the lift axis.
static bool mixKey(const std::string &k) { return k != "LIFT_MIX" && k.size() > 4 && k.compare(k.size() - 4, 4, "_MIX") == 0; }

// True if the value of a *_MIX / ZOOM_LATERAL key uses y.
static bool usesY(const std::string &key, const std::string &value, std::string *parseErr) {
    Settings s;
    std::string err;
    if (!applyKey(s, key, value, &err)) {
        *parseErr = err;
        return false;
    }
    if (key == "ZOOM_LATERAL") return std::find(s.zoom_lateral.begin(), s.zoom_lateral.end(), AX_Y) != s.zoom_lateral.end();
    Mix m;
    if (!parseMix(value, &m, &err)) {
        *parseErr = err;
        return false;
    }
    for (const Term &t : m.terms)
        if (t.axis == AX_Y && t.weight != 0) return true;
    return false;
}

// Scans "KEY = value" lines of a mode or config file.
static int scanFile(const std::filesystem::path &p, bool yIsLift = true) {
    std::ifstream in(p);
    assert(in.good());
    std::vector<std::pair<std::string, std::string>> lines;
    std::string line, description;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';' || line[0] == '[') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = upperAscii(trim(line.substr(0, eq))), v = trim(line.substr(eq + 1));
        if (k == "DESCRIPTION") description = v;
        lines.emplace_back(k, v);
    }
    std::string lower = description;
    for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool optedIn = lower.find("lift/press") != std::string::npos;
    int scanned = 0;
    for (const auto &kv : lines) {
        if (!mixKey(kv.first) && kv.first != "ZOOM_LATERAL") continue;
        ++scanned;
        std::string perr;
        const bool y = usesY(kv.first, kv.second, &perr);
        if (!perr.empty()) fail(p.generic_string(), kv.first, "has a value that does not parse: " + perr);
        else if (y && yIsLift && !optedIn) fail(p.generic_string(), kv.first, "uses y (value: " + kv.second + ")");
    }
    return scanned;
}

int main() {
    namespace fs = std::filesystem;

    // 1. the built-in modes and the seeded default configuration
    int files = 0;
    for (const fs::directory_entry &e : fs::directory_iterator("examples/modes"))
        if (e.path().extension() == ".conf") {
            scanFile(e.path());
            ++files;
        }
    assert(files > 0);
    for (const char *os : {"linux", "windows", "macos"})  // the default mixes and ZOOM_LATERAL are in each
    {
        using namespace spn::ui;
        // The driver names the axes differently per system: the lift/press axis of each default file is what its
        // own mixes leave out (axis_roles_model.hpp), y on Linux, z where y is push forward/back.
        const std::string f = std::string("examples/default/") + os + "/dofscroll.conf";
        const AxisRoles r = deriveAxisRoles(settingsOfFile(f));
        assert(r.matched && r.has(Motion::Lift));
        assert(scanFile(f, r.axis(Motion::Lift) == AX_Y) >= 4);
        // LIFT_MIX of each default file names exactly the axis the mixes leave over (Linux: the built-in default y)
        const Settings ds = settingsOfFile(f);
        assert(ds.lift.mix.terms.size() == 1 && ds.lift.mix.terms[0].axis == r.axis(Motion::Lift));
        assert(ds.lift.deadzone == 80);
    }

    // 2. the built-in defaults of the program
    for (const KV &p : allKeys(Settings()))
        if (mixKey(p.first) || p.first == "ZOOM_LATERAL") {
            std::string perr;
            if (usesY(p.first, p.second, &perr)) fail("built-in defaults (Settings)", p.first, "uses y (value: " + p.second + ")");
        }

    // 3. the defaults documented in docs/configuration.md: the default column of the table rows
    {
        std::ifstream in("docs/configuration.md");
        assert(in.good());
        std::string line;
        int rows = 0;
        while (std::getline(in, line)) {
            if (line.rfind("| `<CH>_MIX`", 0) != 0 && line.rfind("| `ZOOM_LATERAL`", 0) != 0) continue;
            ++rows;
            std::vector<std::string> cells;
            size_t a = 1;
            while (a < line.size()) {
                size_t b = line.find('|', a);
                if (b == std::string::npos) b = line.size();
                cells.push_back(line.substr(a, b - a));
                a = b + 1;
            }
            assert(cells.size() >= 3);
            // whole words of the default cell: `y` alone is the axis
            std::string word;
            for (size_t i = 0; i <= cells[1].size(); ++i) {
                const char c = i < cells[1].size() ? cells[1][i] : ' ';
                if (std::isalnum(static_cast<unsigned char>(c))) word += c;
                else {
                    if (word == "y") fail("docs/configuration.md", trim(cells[0]), "documents the default `" + trim(cells[1]) + "`, which uses y");
                    word.clear();
                }
            }
        }
        assert(rows == 2);  // <CH>_MIX and ZOOM_LATERAL are documented
    }

    if (failures) {
        std::printf("test_no_lift_press: %d problem(s)\n", failures);
        return 1;
    }
    std::puts("test_no_lift_press: ok");
    return 0;
}
