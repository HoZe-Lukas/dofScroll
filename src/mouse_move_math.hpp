// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure logic of the macOS pointer move (toolkit-free, unit-tested on every OS): clamping a point to the
// union of several display rectangles and the event type for a move while a button is held.
#pragma once
#include <cstddef>

namespace spn {
namespace mousemove {

struct Rect {
    double x = 0, y = 0, w = 0, h = 0;
};
struct Point {
    double x = 0, y = 0;
};

// CGEventType values (checked against the Apple constants in mac_output.cpp).
constexpr int kEvMouseMoved = 5;
constexpr int kEvLeftMouseDragged = 6;
constexpr int kEvRightMouseDragged = 7;
constexpr int kEvOtherMouseDragged = 27;

// Nearest point (last pixel inclusive) of one rectangle; a rectangle without area returns p unchanged.
inline Point clampToRect(Point p, const Rect &r) {
    if (r.w < 1 || r.h < 1) return p;
    if (p.x < r.x) p.x = r.x;
    if (p.y < r.y) p.y = r.y;
    if (p.x > r.x + r.w - 1) p.x = r.x + r.w - 1;
    if (p.y > r.y + r.h - 1) p.y = r.y + r.h - 1;
    return p;
}

// Clamps p to the union of the rectangles: p itself if it lies in one of them, otherwise the nearest point of
// any rectangle. n == 0 (no display list) leaves p unchanged.
inline Point clampToUnion(Point p, const Rect *rects, size_t n) {
    bool have = false;
    Point best = p;
    double bestD = 0;
    for (size_t i = 0; i < n; ++i) {
        if (rects[i].w < 1 || rects[i].h < 1) continue;
        const Point c = clampToRect(p, rects[i]);
        const double dx = c.x - p.x, dy = c.y - p.y, d = dx * dx + dy * dy;
        if (!have || d < bestD) {
            have = true;
            best = c;
            bestD = d;
        }
        if (d == 0) break;
    }
    return have ? best : p;
}

// Event type of a pointer move: dragged for the held button (left wins over right over middle), else moved.
// Buttons are 1 = left, 2 = middle, 3 = right (as in Output::mouseButton).
inline int moveEventType(bool left, bool middle, bool right) {
    if (left) return kEvLeftMouseDragged;
    if (right) return kEvRightMouseDragged;
    if (middle) return kEvOtherMouseDragged;
    return kEvMouseMoved;
}

}  // namespace mousemove
}  // namespace spn
