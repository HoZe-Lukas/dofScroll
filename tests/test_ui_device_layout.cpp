// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Button layout of the visual joystick mode editor (issue #143): the number of buttons per model, the layout
// kind, the generic grid for unknown models, positions inside the unit box and no overlapping slots.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <cstdio>
#include <set>

#include "ui/model/device_layout_model.hpp"

using namespace spn;
using namespace spn::ui;

static void checkSlots(const DeviceLayout &l) {
    assert(int(l.buttons.size()) == l.count);
    std::set<int> numbers;
    for (size_t i = 0; i < l.buttons.size(); ++i) {
        const ButtonSlot &s = l.buttons[i];
        assert(s.number == int(i) + 1 && numbers.insert(s.number).second);
        assert(s.x >= 0 && s.x <= 1 && s.y >= 0 && s.y <= 1);
        for (size_t j = 0; j < i; ++j) assert(std::fabs(s.x - l.buttons[j].x) > 1e-9 || std::fabs(s.y - l.buttons[j].y) > 1e-9);
    }
}

int main() {
    const struct { const char *name; int reported; int count; LayoutKind kind; bool known; } cases[] = {
        {"3Dconnexion SpaceMouse Compact", 2, 2, LayoutKind::TwoSides, true},
        {"SpaceMouse Wireless", 2, 2, LayoutKind::TwoSides, true},
        {"3Dconnexion SpaceMouse Pro", 15, 15, LayoutKind::Pro, true},
        {"SpaceMouse Pro Wireless", 15, 15, LayoutKind::Pro, true},
        {"SpaceMouse Enterprise", 31, 31, LayoutKind::Keypad, true},
        {"SpaceNavigator", 2, 2, LayoutKind::TwoSides, true},
        {"SpaceMouse Compact", 0, 2, LayoutKind::TwoSides, true},  // the factory count wins over the reported one
        {"Mystery Cap 9000", 9, 9, LayoutKind::Grid, false},
        {"", 4, 4, LayoutKind::Grid, false},
        {"Mystery Cap", 32, 32, LayoutKind::Grid, false},
    };
    for (const auto &c : cases) {
        const DeviceLayout l = deviceLayout(c.name, c.reported);
        assert(l.count == c.count && l.kind == c.kind && l.knownModel == c.known);
        checkSlots(l);
    }
    // unknown model: a grid with enough columns, at most 32 buttons
    const DeviceLayout g = deviceLayout("Mystery Cap", 9);
    assert(g.columns == 3);
    assert(deviceLayout("Mystery Cap", 3).columns == 3);
    assert(deviceLayout("Mystery Cap", 100).count == 32);
    // none
    const DeviceLayout none = deviceLayout("Mystery Cap", 0);
    assert(none.kind == LayoutKind::None && none.buttons.empty());
    assert(deviceLayout("Mystery Cap", -3).count == 0);
    // Compact: one button left of the cap, one right
    const DeviceLayout c = deviceLayout("SpaceMouse Compact", 2);
    assert(c.buttons[0].x < 0.5 && c.buttons[1].x > 0.5);
    // Pro: 1 and 2 left, 3 and 4 right on top, five keys left and six right below; provisional numbers
    const DeviceLayout pro = deviceLayout("SpaceMouse Pro", 15);
    assert(pro.provisional && pro.buttons[0].x < 0.5 && pro.buttons[1].x < 0.5 && pro.buttons[2].x > 0.5 && pro.buttons[3].x > 0.5);
    assert(pro.buttons[0].y == 0 && pro.buttons[3].y == 0);
    int leftKeys = 0, rightKeys = 0;
    for (const ButtonSlot &b : pro.buttons) (b.y > 0 ? (b.x < 0.5 ? leftKeys : rightKeys) : leftKeys) += b.y > 0;
    assert(leftKeys == 5 && rightKeys == 6);
    assert(!deviceLayout("SpaceMouse Compact", 2).provisional && deviceLayout("SpaceMouse Enterprise", 31).provisional);
    // the count to draw when the device is unknown: the reported one, or a short grid
    assert(reportedOrDefaultButtons(6) == 6 && reportedOrDefaultButtons(0) == kUnknownDeviceButtons);
    assert(reportedOrDefaultButtons(32) == kUnknownDeviceButtons && reportedOrDefaultButtons(-1) == kUnknownDeviceButtons);
    std::puts("test_ui_device_layout OK");
    return 0;
}
