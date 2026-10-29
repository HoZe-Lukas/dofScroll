// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Own functions of this profile" (own_functions_model.hpp): which action texts of a profile are own functions, the same text on
// several motions and buttons as one function that is used by all of them, deleting it (the users inherit again, said in words),
// and the lines of a joystick mode file that is a copy.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <string>

#include "config.hpp"
#include "testutil.hpp"
#include "ui/model/new_function_model.hpp"
#include "ui/model/own_functions_model.hpp"
#include "ui_fixture.hpp"

using namespace spn;
using namespace spn::ui;

namespace {
struct OfEnglish : StringProvider {
    std::string tr(const char *s) const override { return s; }
};
bool ofHas(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }
std::string ofSection(const SettingsModel &m, size_t i) {
    std::string t;
    for (const KV &kv : m.profiles().sections[i].kv) t += kv.first + "=" + kv.second + ";";
    return t;
}
}  // namespace

int main() {
    useFreshDefaultOf("linux");
    OfEnglish s;
    ModeSet modes = loadModes("/nonexistent-dir-for-test", nullptr);
    ProfileSet ps;
    const size_t app = ps.findOrAdd(Section::APP, "browser");
    SettingsModel m;
    m.setProfiles(ps, "");
    m.setModes(modes, "");
    std::vector<FunctionProblem> pr;

    // nothing of its own yet
    assert(ownFunctions(m, app, s).empty());

    // the plain choices are not own functions: scroll, zoom, a click on a button, nothing
    assert(m.setValue(app, "TWIST_ACTION", "zoom", nullptr) && m.setButton(app, 2, "click", nullptr) && m.setButton(app, 4, "none", nullptr));
    assert(ownFunctions(m, app, s).empty());

    // tab switching on two motions and a key on a button: the same text is one function used by both motions
    FunctionDraft tabs = newDraft(FunctionTarget::motion("h_tilt"), FunctionKind::Key);
    tabs.neg = "ctrl+pageup";
    tabs.pos = "ctrl+pagedown";
    tabs.trigger = "25";
    assert(applyFunction(m, app, FunctionTarget::motion("h_tilt"), tabs, &pr));
    assert(applyFunction(m, app, FunctionTarget::motion("v_tilt"), tabs, &pr));
    FunctionDraft key = newDraft(FunctionTarget::buttonNumber(3), FunctionKind::Key);
    key.neg = "ctrl+0";
    assert(applyFunction(m, app, FunctionTarget::buttonNumber(3), key, &pr));
    std::vector<OwnFunction> list = ownFunctions(m, app, s);
    assert(list.size() == 2);
    assert(list[0].text == "step:ctrl+pageup,ctrl+pagedown" && list[0].uses.size() == 2 && list[0].onMotion() && !list[0].onButton());
    assert(list[0].uses[0].key == "H_TILT_ACTION" && list[0].uses[1].key == "V_TILT_ACTION");
    assert(ofHas(usesWords(list[0].uses), " and "));
    assert(list[1].text == "key:ctrl+0" && list[1].uses.size() == 1 && list[1].onButton() && list[1].uses[0].button == 3 && list[1].uses[0].name == "Button 3");

    // the lines of a joystick mode that is a copy: the action keys, the buttons, the tuning the profile holds
    const std::string lines = joystickModeLines(list[0], [&](const std::string &key) {
        std::string held;
        motion_edit_detail::ownText(m, app, key, &held);
        return held;
    }, m, app);
    assert(ofHas(lines, "H_TILT_ACTION = step:ctrl+pageup,ctrl+pagedown\n") && ofHas(lines, "V_TILT_ACTION = step:ctrl+pageup,ctrl+pagedown\n"));
    assert(ofHas(lines, "TRIGGER = 25\n"));
    assert(joystickModeLines(list[1], [](const std::string &) { return std::string(); }, m, app) == "BUTTON_3 = key:ctrl+0\n");

    // save as joystick mode / button mode: a new file with a copy, the profile keeps its own function
    {
        const std::string dir = tu::makeTempDir("dofscroll-ownfn");
        ModeCatalog mc(dir + "/modes");
        ButtonModeCatalog bc(dir + "/buttons");
        const std::string modeLines = joystickModeLines(list[0], [&](const std::string &key) {
            std::string held;
            motion_edit_detail::ownText(m, app, key, &held);
            return held;
        }, m, app);
        const SaveAsResult jm = saveAsJoystickMode(&mc, "Tab switching", modeLines);
        assert(jm.ok && jm.id == "tab-switching" && jm.warnings.empty());
        const ModeDef *def = mc.modeSet().find("tab-switching");
        assert(def && def->hSplit.tiltSet && def->hSplit.tilt.type == ChannelActionType::Step && def->vSplit.tiltSet);
        assert(ownFunctions(m, app, s).size() == 2);  // the profile keeps its own function
        assert(!saveAsJoystickMode(&mc, "Tab switching", modeLines).ok);  // never overwrites
        const SaveAsResult bm = saveAsButtonMode(&bc, "Select none", list[1].text);
        assert(bm.ok && bm.id == "select-none" && bc.find("select-none") && bc.find("select-none")->action == "key:ctrl+0");
        const SaveAsResult bad = saveAsButtonMode(&bc, "Broken", "step:left,right");  // a motion function is no button action
        assert(!bad.ok && !bad.error.empty() && !bc.find("broken"));
        tu::removeAll(dir);
    }

    // a channel key (V_ACTION) is used by both parts
    assert(m.setValue(app, "H_TILT_ACTION", "default", nullptr));
    assert(m.setValue(app, "V_ACTION", "keys:left,right", nullptr));
    list = ownFunctions(m, app, s);
    bool channel = false;
    for (const OwnFunction &f : list)
        if (f.text == "keys:left,right") channel = f.uses.size() == 2 && f.uses[0].motionId == "v_push" && f.uses[1].motionId == "v_tilt";
    assert(channel);

    // delete: the users inherit again (the keys go, never "none"), the tuning goes with it, and the words say so
    for (const OwnFunction &f : list)
        if (f.text == "key:ctrl+0") {
            const OwnDeleteResult r = deleteOwnFunction(&m, app, f, s);
            assert(r.ok && ofHas(r.words, "Button 3 inherit again") && ofHas(r.words, "Button 3:"));
            assert(!ofHas(ofSection(m, app), "BUTTON_3"));
        }
    for (const OwnFunction &f : list)
        if (f.text == "keys:left,right") {
            const OwnDeleteResult r = deleteOwnFunction(&m, app, f, s);
            assert(r.ok && ofHas(r.words, "Vertical push and Vertical tilt inherit again"));
            assert(!ofHas(ofSection(m, app), "V_ACTION"));
        }
    assert(!ofHas(ofSection(m, app), "BUTTON_3=none") && !ofHas(ofSection(m, app), "V_ACTION=none"));  // nothing pinned off
    assert(!deleteOwnFunction(nullptr, app, list[0], s).ok);

    std::puts("test_ui_own_functions: ok");
    return 0;
}
