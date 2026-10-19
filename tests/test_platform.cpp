// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Contract test for the platform seams using the scripted fake input source.
#include <cassert>
#include <cstdio>

#include "platform/fake_input.hpp"

using namespace spn;

int main() {
    DeviceInfo d;
    d.id = 7;
    d.name = "Test Device";
    d.reportedButtons = 2;
    FakeInputSource src({d});

    InputEvent e;
    assert(src.wait(&e, 0) == WaitResult::Closed);  // not opened yet
    std::string err;
    assert(src.open(&err));
    assert(src.wait(&e, 0) == WaitResult::Timeout);  // open but empty

    src.pushMotion(7, 1, 2, 3, 4, 5, 6);
    src.pushButton(7, 2, true);
    src.pushButton(7, 2, false);

    assert(src.wait(&e, 0) == WaitResult::Event);
    assert(e.type == InputEventType::Motion && e.device == 7);
    assert((e.axes == std::array<int, 6>{1, 2, 3, 4, 5, 6}));
    assert(src.wait(&e, 0) == WaitResult::Event);
    assert(e.type == InputEventType::Button && e.button == 2 && e.down);
    assert(src.wait(&e, 0) == WaitResult::Event);
    assert(e.type == InputEventType::Button && !e.down);
    assert(src.wait(&e, 0) == WaitResult::Timeout);  // drained, order preserved

    assert(src.devices().size() == 1 && src.devices()[0].name == "Test Device");
    src.close();
    assert(src.wait(&e, 0) == WaitResult::Closed);

    std::puts("test_platform: all tests passed");
    return 0;
}
