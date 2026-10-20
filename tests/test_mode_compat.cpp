// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Compatibility guard for the split push/tilt feature: modes that set none of the split keys must
// produce bit-identical output and channel rates as before the feature existed. A fixed pseudo-random
// motion is run through several unsplit modes; a hash of every output event and every reported rate is
// compared with the value recorded on the code base before the feature (this file uses only the
// pre-split API, so it compiles on both). Run with --print to list the hashes.
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "mode_runner.hpp"

using namespace spn;

namespace {

struct Hasher : Output {
    uint64_t h = 1469598103934665603ull;
    void add(const std::string &s) {
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
        h ^= 0xff; h *= 1099511628211ull;
    }
    const char *name() const override { return "hash"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { add("wv" + std::to_string(n)); }
    void wheelH(int n) override { add("wh" + std::to_string(n)); }
    void setKey(Key k, bool d) override { add(std::string(d ? "+" : "-") + keyName(k)); }
    void tapKey(Key k) override { add(std::string("tap:") + keyName(k)); }
    void mouseButton(int b, bool d) override { add(std::string(d ? "+m" : "-m") + std::to_string(b)); }
    void releaseAll() override {}
    void flush() override {}
};

ChannelAction act(ChannelActionType t, Key neg = Key::Up, Key pos = Key::Down) {
    ChannelAction a;
    a.type = t;
    a.neg = neg;
    a.pos = pos;
    return a;
}

uint64_t scenario(const ModeDef &m) {
    Hasher out;
    ModeRunner r(&out);
    r.setMode(m);
    Settings s;
    s.nav_button_enabled = true;
    s.nav_button = 2;
    s.click_button_enabled = true;
    s.click_button = 0;
    uint32_t rng = 12345;
    auto next = [&] { rng = rng * 1664525u + 1013904223u; return static_cast<int>((rng >> 16) & 0x1ff) - 256; };
    RawAxes raw{};
    uint32_t buttons = 0;
    for (int tick = 0; tick < 1500; ++tick) {
        if (tick % 40 == 0) {  // a new target pose every 160 ms, some axes at rest
            for (int a = 0; a < AX_COUNT; ++a) raw[a] = (next() & 3) == 0 ? 0 : next();
            if (tick % 400 == 200) raw = RawAxes{};
        }
        if (tick % 250 == 100) buttons ^= 1u << 2;  // button 3 = nav button of the settings
        if (tick % 300 == 50) buttons ^= 1u << 0;
        r.tick(s, raw, buttons, true, 1.0 / 250.0);
        const ModeRunner::Info &i = r.info();
        for (double d : {i.rate_h, i.rate_v, i.twist, i.lateral, i.rate_zoom}) {
            uint64_t bits;
            std::memcpy(&bits, &d, sizeof bits);
            out.add(std::to_string(bits));
        }
        out.add(i.zooming ? "z" : "-");
    }
    return out.h;
}

ModeDef mode(const char *id) {
    ModeDef m;
    m.id = id;
    m.label = id;
    return m;
}

}  // namespace

int main(int argc, char **argv) {
    struct Case { const char *name; ModeDef m; uint64_t golden; };
    std::vector<Case> cases;
    cases.push_back({"normal", mode("normal"), 0});
    {
        ModeDef m = mode("keys");
        m.v = act(ChannelActionType::Keys, Key::Up, Key::Down);
        m.h = act(ChannelActionType::Keys, Key::Left, Key::Right);
        cases.push_back({"keys", m, 0});
    }
    {
        ModeDef m = mode("step");
        m.v = act(ChannelActionType::Step, Key::PageUp, Key::PageDown);
        m.h = act(ChannelActionType::Step, Key::Left, Key::Right);
        m.twist = act(ChannelActionType::Step, Key::Minus, Key::Plus);
        cases.push_back({"step", m, 0});
    }
    {
        ModeDef m = mode("zoomv");
        m.v = act(ChannelActionType::Zoom);
        m.h = act(ChannelActionType::WheelH);
        m.twist = act(ChannelActionType::Off);
        cases.push_back({"zoomv", m, 0});
    }
    {
        ModeDef m = mode("timeline");
        m.v = act(ChannelActionType::Off);
        m.h = act(ChannelActionType::Keys, Key::Left, Key::Right);
        m.twist = act(ChannelActionType::Keys, Key::Left, Key::Right);
        ButtonBinding b;
        b.button = 3;
        b.type = ButtonActionType::Nav;
        m.buttons.push_back(b);
        cases.push_back({"timeline", m, 0});
    }
    {
        ModeDef m = mode("wheelswap");
        m.v = act(ChannelActionType::WheelH);
        m.h = act(ChannelActionType::WheelV);
        cases.push_back({"wheelswap", m, 0});
    }
    // Recorded on origin/main before the split feature (same order as the cases above).
    static const uint64_t golden[] = {
        0x7382afd420e336e1ull, 0x495f7e1306707c62ull, 0x5d2f07dcee89f82full,
        0x9540388dab336c39ull, 0xfd22f6bd938bd623ull, 0x8205303a3aa7e1b4ull,
    };
    int fails = 0;
    for (size_t i = 0; i < cases.size(); ++i) {
        const uint64_t h = scenario(cases[i].m);
        if (argc > 1 && std::strcmp(argv[1], "--print") == 0) {
            std::printf("0x%016llxull, // %s\n", static_cast<unsigned long long>(h), cases[i].name);
        } else if (h != golden[i]) {
            std::fprintf(stderr, "FAIL: mode %s output changed (hash %016llx, expected %016llx)\n", cases[i].name,
                         static_cast<unsigned long long>(h), static_cast<unsigned long long>(golden[i]));
            ++fails;
        }
    }
    if (fails) return 1;
    std::puts("test_mode_compat: OK");
    return 0;
}
