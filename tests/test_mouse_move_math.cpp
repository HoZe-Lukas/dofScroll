// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pointer move math: union clamp over several displays, event type from the held buttons.
#include <cassert>
#include <cstdio>

#include "mouse_move_math.hpp"

using namespace spn::mousemove;

int main() {
    // main display 0..1920 x 0..1080, secondary to the right and one to the left (negative origin).
    const Rect d[3] = {{0, 0, 1920, 1080}, {1920, 0, 1280, 1024}, {-1000, 100, 1000, 800}};
    // inside a secondary display: unchanged (not teleported to the main one)
    Point p = clampToUnion({2500, 500}, d, 3);
    assert(p.x == 2500 && p.y == 500);
    p = clampToUnion({-500, 300}, d, 3);
    assert(p.x == -500 && p.y == 300);
    // beyond the right edge of the union
    p = clampToUnion({4000, 500}, d, 3);
    assert(p.x == 3199 && p.y == 500);
    // below the secondary display (1024 high) but x still in it: stays on the nearest display
    p = clampToUnion({2500, 1050}, d, 3);
    assert(p.x == 2500 && p.y == 1023);
    // left of the leftmost display
    p = clampToUnion({-1200, 50}, d, 3);
    assert(p.x == -1000 && p.y == 100);
    // no displays / empty rectangles: unchanged
    p = clampToUnion({5, 6}, d, 0);
    assert(p.x == 5 && p.y == 6);
    const Rect empty{0, 0, 0, 0};
    p = clampToUnion({5, 6}, &empty, 1);
    assert(p.x == 5 && p.y == 6);

    assert(moveEventType(false, false, false) == kEvMouseMoved);
    assert(moveEventType(true, false, false) == kEvLeftMouseDragged);
    assert(moveEventType(false, false, true) == kEvRightMouseDragged);
    assert(moveEventType(false, true, false) == kEvOtherMouseDragged);
    assert(moveEventType(true, true, true) == kEvLeftMouseDragged);
    assert(moveEventType(false, true, true) == kEvRightMouseDragged);
    std::puts("test_mouse_move_math OK");
    return 0;
}
