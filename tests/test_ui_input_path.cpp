// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Input path" model: choices, status line of the active path, the About text and its licence notes.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <ctime>
#include <string>

#include "ui/model/input_path_model.hpp"

using namespace spn::ui;

static StatusModel running(const std::string &input) {
    StatusModel m;
    const int64_t now = 1000;
    m.update("profile=global\ndevice=SpaceMouse Pro\nmode=normal\nenabled=1\npaused=0\ndevices_seen=1\n" +
                 (input.empty() ? std::string() : "input=" + input + "\n") + "updated=" + std::to_string(now) + "\n",
             true, now);
    return m;
}

int main() {
    IdentityStrings id;

    // ---- choices ----
    const auto c = inputChoices(id);
    assert(c.size() == 3 && c[0].value == "auto" && c[0].label == "Automatic" && c[1].value == "driver" &&
           c[1].label == "3Dconnexion driver" && c[2].value == "hid" && c[2].label == "Direct (HID)");
    assert(inputChoiceIndex("auto") == 0 && inputChoiceIndex("driver") == 1 && inputChoiceIndex("hid") == 2 &&
           inputChoiceIndex("nonsense") == 0 && inputChoiceIndex("") == 0);
    assert(inputChoiceAvailable(true) && !inputChoiceAvailable(false));
    assert(inputExplanation(id).find("keeps running for your CAD programs") != std::string::npos);

    // ---- the status ----
    StatusModel d = running("driver");
    assert(d.inputLine(id) == "Input: 3Dconnexion driver");
    assert(d.infoLine(id) == "Device: SpaceMouse Pro  |  Input: 3Dconnexion driver");
    assert(inputActiveLine(d, id) == "Active input: 3Dconnexion driver");
    StatusModel h = running("hid");
    assert(h.inputLine(id) == "Input: direct (HID)" && inputActiveLine(h, id) == "Active input: direct (HID)");
    StatusModel none = running("");  // Linux or an older daemon: no line at all
    assert(none.inputLine(id).empty() && none.infoLine(id) == "Device: SpaceMouse Pro");
    assert(inputActiveLine(none, id) == "Active input: not known yet");
    StatusModel odd = running("pigeon");
    assert(odd.inputLine(id).empty());
    StatusModel stopped;
    stopped.update("", false, 1000);
    assert(stopped.inputLine(id).empty() && inputActiveLine(stopped, id) == "Active input: not known yet");
    // untrusted status text cannot smuggle anything into the line: only the two known ids produce text
    assert(running("driver\x01").inputLine(id).empty());

    // ---- About: licence notes ----
    const auto win = aboutLines("0.2.0", "6.8.3", true, id);
    assert(win.size() == 4 && win[0] == "dofscroll 0.2.0");
    assert(win[1].find("GPL-3.0-or-later") != std::string::npos);
    assert(win[2] == "This program uses Qt 6.8.3 under the LGPL (https://www.qt.io).");
    assert(win[3].find("belong to 3Dconnexion (a Logitech company)") != std::string::npos &&
           win[3].find("does not contain any of it") != std::string::npos &&
           win[3].find("Not affiliated with or endorsed by 3Dconnexion.") != std::string::npos);
    const auto lin = aboutLines("0.2.0", "6.8.3", false, id);
    assert(lin.size() == 3);  // no driver note where no driver interface is used
    for (const std::string &l : lin) assert(l.find("3Dconnexion") == std::string::npos);

    std::puts("test_ui_input_path: ok");
    return 0;
}
