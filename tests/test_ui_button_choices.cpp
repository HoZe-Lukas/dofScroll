// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The per-button drop-down of the Profiles page: entry order, inherit, button modes, "missing: <id>".
#undef NDEBUG
#include <cassert>
#include <fstream>
#include <string>

#include "testutil.hpp"
#include "ui/model/button_choice_model.hpp"

using namespace spn;
using namespace spn::ui;

static SettingsModel loadModel(const std::string &text) {
    const std::string dir = tu::makeTempDir("dofscroll-choices");
    const std::string path = dir + "/dofscroll.conf";
    std::ofstream(path, std::ios::binary) << text;
    SettingsModel m;
    std::vector<std::string> warn;
    std::string err;
    assert(m.load(path, &warn, &err));
    m.setDeviceInfo("", 6);
    return m;
}

static ButtonModeSet twoButtonModes() {
    ButtonModeSet s;
    ButtonMode a, b;
    a.id = "latch-nav"; a.label = "Navigation latch"; a.description = "Toggles navigation";
    b.id = "next-mode"; b.label = "Next mode";
    s.modes = {a, b};
    return s;
}

static void testBindingKind() {
    Binding b;
    assert(parseBinding("buttonmode:latch-nav", &b) && b.kind == BindingKind::ButtonMode && b.arg == "latch-nav");
    assert(composeBinding(b) == "buttonmode:latch-nav");
    assert(!parseBinding("buttonmode:", &b) && !parseBinding("buttonmode:Bad Id", &b));
    Binding incomplete{BindingKind::ButtonMode, ""};
    assert(composeBinding(incomplete).empty());  // incomplete
    // a button mode is chosen from the button modes, it is not one of the plain actions
    for (BindingContext c : {BindingContext::Global, BindingContext::Specific})
        for (bool adv : {false, true})
            for (const BindingKindInfo &k : bindingKindsFor(c, adv)) assert(k.kind != BindingKind::ButtonMode);
}

static void testOrder() {
    IdentityStrings id;
    const ButtonModeSet bm = twoButtonModes();
    // specific: Default (inherit), the button modes by name, then all actions
    std::vector<ButtonChoice> c = buttonChoices(BindingContext::Specific, false, bm, "", id);
    assert(c.size() == 3 + bindingKindsFor(BindingContext::Specific, false).size() - 0);
    assert(c[0].type == ButtonChoice::Type::Inherit && c[0].label == "Default (inherit)");
    assert(c[1].type == ButtonChoice::Type::ButtonMode && c[1].label == "Navigation latch" && c[1].id == "latch-nav" &&
           c[1].tooltip == "Toggles navigation" && !c[1].missing);
    assert(c[2].label == "Next mode" && c[2].id == "next-mode");
    assert(c[3].type == ButtonChoice::Type::Action && c[3].group == ButtonChoice::Group::Actions &&
           c[3].kind == BindingKind::None && c[3].label == "Nothing");
    bool sawKey = false;
    for (const ButtonChoice &x : c) sawKey = sawKey || (x.type == ButtonChoice::Type::Action && x.kind == BindingKind::Key);
    assert(sawKey);  // plain actions are first-class in a profile

    // global: "Nothing" first (no inherit), no second "Nothing", plain key/click only behind Advanced
    c = buttonChoices(BindingContext::Global, false, bm, "", id);
    assert(c[0].type == ButtonChoice::Type::Action && c[0].kind == BindingKind::None && c[0].label == "Nothing");
    assert(c[1].type == ButtonChoice::Type::ButtonMode);
    int nothing = 0;
    for (const ButtonChoice &x : c) {
        nothing += x.type == ButtonChoice::Type::Action && x.kind == BindingKind::None;
        assert(x.type != ButtonChoice::Type::Inherit);
        assert(!(x.type == ButtonChoice::Type::Action && x.kind == BindingKind::Key));
    }
    assert(nothing == 1);
    c = buttonChoices(BindingContext::Global, true, bm, "", id);
    sawKey = false;
    for (const ButtonChoice &x : c) sawKey = sawKey || (x.type == ButtonChoice::Type::Action && x.kind == BindingKind::Key);
    assert(sawKey);

    // without button modes the list is Default/Nothing + actions
    c = buttonChoices(BindingContext::Specific, false, ButtonModeSet(), "", id);
    assert(c[0].type == ButtonChoice::Type::Inherit && c[1].type == ButtonChoice::Type::Action);
}

static void testMissing() {
    IdentityStrings id;
    const ButtonModeSet bm = twoButtonModes();
    SettingsModel m = loadModel("[global]\n\n[app:x]\nBUTTON_2 = buttonmode:gone\n");
    const size_t app = 1;
    std::vector<BindingRow> rows = bindingRows(m, app);
    assert(rows.size() >= 2 && rows[1].valid && rows[1].binding.kind == BindingKind::ButtonMode);
    std::vector<ButtonChoice> c = buttonChoices(BindingContext::Specific, false, bm, rows[1].binding.arg, id);
    const int sel = selectedButtonChoice(c, BindingContext::Specific, rows[1]);
    assert(sel >= 0 && c[size_t(sel)].missing && c[size_t(sel)].label == "missing: gone" &&
           c[size_t(sel)].group == ButtonChoice::Group::ButtonModes);
    // shown, not rewritten: the line stays as it is until the user picks something else
    assert(rows[1].text == "buttonmode:gone");
    std::string err;
    assert(applyButtonChoice(m, app, rows[1], c[size_t(sel)], {}, &err));
    assert(bindingRows(m, app)[1].text == "buttonmode:gone");
    // no missing entry when the button mode exists
    c = buttonChoices(BindingContext::Specific, false, bm, "latch-nav", id);
    for (const ButtonChoice &x : c) assert(!x.missing);
}

static void testApply() {
    IdentityStrings id;
    const ButtonModeSet bm = twoButtonModes();
    SettingsModel m = loadModel("[global]\nBUTTON_1 = nav\n\n[device:Pad]\n\n[app:x]\nBUTTON_3 = key:space\n");
    std::string err;
    const std::vector<std::string> modeIds = {"normal", "off"};

    // inherited row in a device profile: selected entry is "Default (inherit)"
    std::vector<BindingRow> rows = bindingRows(m, 1);
    std::vector<ButtonChoice> c = buttonChoices(BindingContext::Specific, false, bm, "", id);
    assert(!rows[0].overridden && selectedButtonChoice(c, BindingContext::Specific, rows[0]) == 0);

    // pick a button mode: writes buttonmode:<id>, the row then shows it and "Default" is no longer selected
    assert(applyButtonChoice(m, 1, rows[0], c[1], modeIds, &err));
    rows = bindingRows(m, 1);
    assert(rows[0].overridden && rows[0].text == "buttonmode:latch-nav" && rows[0].binding.kind == BindingKind::ButtonMode);
    c = buttonChoices(BindingContext::Specific, false, bm, rows[0].binding.arg, id);
    const int sel = selectedButtonChoice(c, BindingContext::Specific, rows[0]);
    assert(sel == 1 && c[size_t(sel)].id == "latch-nav");
    // the same button mode on a second button
    assert(applyButtonChoice(m, 1, bindingRows(m, 1)[1], c[1], modeIds, &err));
    assert(bindingRows(m, 1)[1].text == "buttonmode:latch-nav");

    // "Default (inherit)" removes the line of the device profile and the inherited action shows again
    assert(applyButtonChoice(m, 1, bindingRows(m, 1)[0], c[0], modeIds, &err));
    rows = bindingRows(m, 1);
    assert(!rows[0].overridden && rows[0].text == "nav");

    // an action: keeps a fitting argument
    rows = bindingRows(m, 2);
    assert(rows[2].binding.kind == BindingKind::Key && rows[2].binding.arg == "space");
    c = buttonChoices(BindingContext::Specific, false, bm, "", id);
    size_t toggleNav = 0;
    for (size_t i = 0; i < c.size(); ++i)
        if (c[i].type == ButtonChoice::Type::Action && c[i].kind == BindingKind::ToggleNav) toggleNav = i;
    assert(toggleNav > 0 && applyButtonChoice(m, 2, rows[2], c[toggleNav], modeIds, &err));
    assert(bindingRows(m, 2)[2].text == "toggle:nav");

    // global: "Nothing" is the first entry and unbinds; there is no inherit
    c = buttonChoices(BindingContext::Global, false, bm, "", id);
    rows = bindingRows(m, 0);
    assert(rows[0].binding.kind == BindingKind::Nav && selectedButtonChoice(c, BindingContext::Global, rows[0]) >= 0);
    assert(applyButtonChoice(m, 0, rows[0], c[0], modeIds, &err));
    assert(bindingRows(m, 0)[0].text == "none");
    assert(applyButtonChoice(m, 0, bindingRows(m, 0)[0], c[1], modeIds, &err));
    assert(bindingRows(m, 0)[0].text == "buttonmode:latch-nav");
    assert(selectedButtonChoice(c, BindingContext::Global, bindingRows(m, 0)[0]) == 1);
}

int main() {
    testBindingKind();
    testOrder();
    testMissing();
    testApply();
    std::puts("test_ui_button_choices OK");
    return 0;
}
