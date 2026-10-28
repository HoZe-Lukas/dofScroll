// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Output::moveMouse through OutputMux beyond tests/test_move_mouse.cpp: the clamp at every edge of int (the backends
// clamp, the mux passes the raw values on), no reference counting or state for pointer movement (every view's
// movement reaches the real output, also after other views left), releasing never moves the pointer, and the base
// default stays a no-op through a view.
#undef NDEBUG
#include <cassert>
#include <climits>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "output.hpp"
#include "output_mux.hpp"

using namespace spn;

namespace {
// Records the raw arguments and sums the clamped ones, as a backend does.
struct Rec : Output {
    std::vector<std::pair<int, int>> raw;
    long sx = 0, sy = 0;
    int releases = 0;
    const char *name() const override { return "rec"; }
    bool init(std::string *) override { return true; }
    void wheelV(int) override {}
    void wheelH(int) override {}
    void setKey(Key, bool) override {}
    void tapKey(Key) override {}
    void mouseButton(int, bool) override {}
    void releaseAll() override { ++releases; }
    void flush() override {}
    void moveMouse(int dx, int dy) override {
        raw.emplace_back(dx, dy);
        sx += clampMouseDelta(dx);
        sy += clampMouseDelta(dy);
    }
};
struct NoPointer : Rec {  // a backend without pointer support: the base default
    void moveMouse(int dx, int dy) override { Output::moveMouse(dx, dy); }
};
}  // namespace

int main() {
    // edges of the clamp
    assert(clampMouseDelta(INT_MAX) == 127 && clampMouseDelta(INT_MIN) == -127 && clampMouseDelta(INT_MIN + 1) == -127);
    assert(clampMouseDelta(126) == 126 && clampMouseDelta(-126) == -126 && clampMouseDelta(1) == 1 && clampMouseDelta(-1) == -1);
    static_assert(clampMouseDelta(kMaxMouseStep + 1) == kMaxMouseStep && clampMouseDelta(-kMaxMouseStep - 1) == -kMaxMouseStep, "constexpr");

    Rec real;
    OutputMux mux(&real);
    Output *a = mux.addView(), *b = mux.addView();
    assert(a && b && a != b);
    // the mux passes the raw values (the backend clamps), the sum of the clamped values is the pointer travel
    a->moveMouse(INT_MAX, INT_MIN);
    b->moveMouse(-300, 300);
    a->moveMouse(0, 0);
    assert(real.raw.size() == 3 && real.raw[0] == std::make_pair(INT_MAX, INT_MIN) && real.raw[1] == std::make_pair(-300, 300));
    assert(real.sx == 127 - 127 + 0 && real.sy == -127 + 127);
    // each axis on its own: dx only and dy only
    real.sx = real.sy = 0;
    a->moveMouse(500, 0);
    a->moveMouse(0, -500);
    assert(real.sx == 127 && real.sy == -127);

    // pointer movement has no per-view state: nothing to release, and a removed view's neighbour keeps moving it
    const size_t n = real.raw.size();
    mux.releaseView(a);
    mux.releaseEverything();
    assert(real.raw.size() == n && real.releases == 1);
    mux.removeView(a);
    b->moveMouse(5, 6);
    assert(real.raw.size() == n + 1 && real.raw.back() == std::make_pair(5, 6));
    // a freed slot is handed out again and works the same
    Output *c = mux.addView();
    assert(c);
    c->moveMouse(-1, -2);
    assert(real.raw.back() == std::make_pair(-1, -2));

    // the base default through a view: ignored, no crash, nothing summed
    NoPointer plain;
    OutputMux m2(&plain);
    Output *v = m2.addView();
    v->moveMouse(10, 10);
    assert(plain.raw.empty() && plain.sx == 0 && plain.sy == 0);
    std::puts("test_move_mouse_mux: OK");
    return 0;
}
