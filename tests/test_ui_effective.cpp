// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Effective values with origin (global < device < mode < app) for the profile editor.
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <string>

#include "testutil.hpp"
#include "ui/model/effective_model.hpp"

using namespace spn;
using namespace spn::ui;
using Source = SettingsModel::Source;

static const EffectiveRow *find(const std::vector<EffectiveRow> &rows, const std::string &key) {
    for (const EffectiveRow &r : rows)
        if (r.key == key) return &r;
    return nullptr;
}

int main() {
    const std::string dir = tu::makeTempDir("dofscroll-effective");
    std::ofstream(dir + "/ov.conf", std::ios::binary)
        << "NAME = Ov\nV_ACTION = wheel_v\nH_ACTION = wheel_h\nTWIST_ACTION = zoom\nSCROLL_V_SENS = 7\n";
    std::ofstream(dir + "/cfg.conf", std::ios::binary)
        << "[global]\nSCROLL_V_DEADZONE = 20\n\n"
           "[device:Pad]\nSCROLL_V_SENS = 3\nSCROLL_V_DEADZONE = 40\n\n"
           "[app:edit]\nMODE = ov\nSCROLL_V_SENS = 9\n\n"
           "[app:view]\nMODE = ov\n\n"
           "[app:plain]\n";
    SettingsModel m;
    std::vector<std::string> warn;
    std::string err;
    assert(m.load(dir + "/cfg.conf", &warn, &err));
    m.setModes(loadModes(dir, nullptr), dir);
    IdentityStrings id;
    Source s;

    // device profile: own key beats [global]; global key shows as global
    const size_t dev = 1, edit = 2, view = 3, plain = 4;
    assert(m.sourceOf(dev, "SCROLL_V_SENS", &s) && s == Source::Device);
    assert(m.sourceOf(dev, "SCROLL_V_DEADZONE", &s) && s == Source::Device);
    assert(m.sourceOf(plain, "SCROLL_V_DEADZONE", &s) && s == Source::Global);
    assert(m.sourceOf(plain, "SCROLL_V_SMOOTH_MS", &s) && s == Source::BuiltIn);
    assert(m.sourceOf(0, "SCROLL_V_DEADZONE", &s) && s == Source::Global);
    assert(m.sourceOf(0, "SCROLL_V_SMOOTH_MS", &s) && s == Source::BuiltIn);
    // app key beats the mode; the mode beats the global value
    assert(m.sourceOf(edit, "SCROLL_V_SENS", &s) && s == Source::App);
    assert(m.sourceOf(view, "SCROLL_V_SENS", &s) && s == Source::Mode);
    assert(!m.sourceOf(view, "NO_SUCH_KEY", &s));
    assert(!m.sourceOf(99, "SCROLL_V_SENS", &s));
    assert(m.modeOverrideValue(view, "SCROLL_V_SENS", "x") == "7");
    assert(m.modeOverrideValue(view, "SCROLL_V_DEADZONE", "x") == "x");
    assert(m.modeOverrideValue(0, "SCROLL_V_SENS", "x") == "x");

    // rows: text for the origin, value of the mode, reset only for the profile's own keys
    std::vector<EffectiveRow> rows = effectiveRows(m, view, id);
    const EffectiveRow *r = find(rows, "SCROLL_V_SENS");
    assert(r && r->value == "7" && r->sourceText == "from joystick mode \"ov\"" && !r->canReset);
    r = find(rows, "SCROLL_V_DEADZONE");
    assert(r && r->sourceText == "from the global settings" && !r->canReset);
    r = find(rows, "SCROLL_V_SMOOTH_MS");
    assert(r && r->sourceText == "built-in default");
    rows = effectiveRows(m, edit, id);
    r = find(rows, "SCROLL_V_SENS");
    assert(r && r->value == "9" && r->sourceText == "from this program profile" && r->canReset);
    rows = effectiveRows(m, dev, id);
    r = find(rows, "SCROLL_V_SENS");
    assert(r && r->value == "3" && r->sourceText == "from this device profile" && r->canReset);
    for (const EffectiveRow &x : rows) assert(!x.sourceText.empty());
    assert(effectiveRows(m, 99, id).empty());

    // reset: the override goes away and the origin falls back
    assert(m.clearOverride(edit, "SCROLL_V_SENS"));
    assert(m.sourceOf(edit, "SCROLL_V_SENS", &s) && s == Source::Mode);
    assert(m.clearOverride(dev, "SCROLL_V_SENS"));
    assert(m.sourceOf(dev, "SCROLL_V_SENS", &s) && s == Source::BuiltIn);

    tu::removeAll(dir);
    std::puts("test_ui_effective OK");
    return 0;
}
