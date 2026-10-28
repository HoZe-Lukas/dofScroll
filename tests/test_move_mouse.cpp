// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Output::moveMouse: clamping, signs and sums through a fake output (also via the mux).
#undef NDEBUG
#include <cassert>
#include <string>

#include "output.hpp"
#include "output_mux.hpp"

using namespace spn;

namespace {
struct Fake : Output {
    long sx = 0, sy = 0;
    int calls = 0;
    const char *name() const override { return "fake"; }
    bool init(std::string *) override { return true; }
    void wheelV(int) override {}
    void wheelH(int) override {}
    void setKey(Key, bool) override {}
    void tapKey(Key) override {}
    void mouseButton(int, bool) override {}
    void releaseAll() override {}
    void flush() override {}
    void moveMouse(int dx, int dy) override {
        sx += clampMouseDelta(dx);
        sy += clampMouseDelta(dy);
        ++calls;
    }
};
struct Plain : Fake {  // reaches the base default
    void moveMouse(int dx, int dy) override { Output::moveMouse(dx, dy); }
};
}  // namespace

int main() {
    static_assert(kMaxMouseStep == 127, "cap");
    assert(clampMouseDelta(0) == 0 && clampMouseDelta(5) == 5 && clampMouseDelta(-5) == -5);
    assert(clampMouseDelta(127) == 127 && clampMouseDelta(128) == 127 && clampMouseDelta(100000) == 127);
    assert(clampMouseDelta(-127) == -127 && clampMouseDelta(-128) == -127 && clampMouseDelta(-100000) == -127);

    Fake f;
    f.moveMouse(3, -4);
    f.moveMouse(-1, 10);
    f.moveMouse(1000, -1000);  // clamped to 127 / -127
    assert(f.sx == 3 - 1 + 127 && f.sy == -4 + 10 - 127 && f.calls == 3);

    Plain p;
    p.moveMouse(9, 9);  // base default: no-op
    assert(p.sx == 0 && p.sy == 0 && p.calls == 0);

    Fake real;
    OutputMux mux(&real);
    Output *v = mux.addView();
    assert(v);
    v->moveMouse(2, -3);
    assert(real.sx == 2 && real.sy == -3 && real.calls == 1);
    return 0;
}
