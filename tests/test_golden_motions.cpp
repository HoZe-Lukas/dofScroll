// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Golden master of the motion handling (stage 1 of docs/plan-motion-functions.md, issue #224).
//
// Records the exact output events of the real ModeRunner for deterministic input sequences: all built-in
// joystick modes and the profile actions (V_ACTION, H_ACTION, TWIST_ACTION and the four push/tilt parts) with
// every function that exists today (default, none, wheel_v, wheel_h, zoom, keys:, step:). Later stages must
// not change any of these lines unless the change is intended and documented; then the table is regenerated:
//     GOLDEN_PRINT=1 ./build/test_golden_motions
// Every token is "<events>:<hash>" of the ordered event list of one input sequence (see kInputs).
// The hash is FNV-1a over the event texts, so the table compares the complete order, not only counts.
// Readable spot checks on top of the table are in testReadable().
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "config.hpp"
#include "mode_runner.hpp"
#include "modes.hpp"

using namespace spn;

namespace {

struct Rec : Output {
    std::vector<std::string> ev;
    const char *name() const override { return "golden"; }
    bool init(std::string *) override { return true; }
    void wheelV(int n) override { ev.push_back("wv" + std::to_string(n)); }
    void wheelH(int n) override { ev.push_back("wh" + std::to_string(n)); }
    void setKey(Key k, bool d) override { ev.push_back(std::string(d ? "+" : "-") + keyName(k)); }
    void tapKey(Key k) override { ev.push_back(std::string("tap:") + keyName(k)); }
    void mouseButton(int b, bool d) override { ev.push_back(std::string(d ? "+m" : "-m") + std::to_string(b)); }
    void releaseAll() override { ev.push_back("releaseAll"); }
    void flush() override {}
    int count(const std::string &e) const {
        int c = 0;
        for (const auto &x : ev) c += x == e;
        return c;
    }
};

const double DT = 1.0 / 250.0;

struct Step {  // deflect the axes for secs seconds
    int x, y, z, rx, ry, rz;
    double secs;
};
struct Input {
    const char *name;
    std::vector<Step> steps;
};

// Every motion in both directions, a ramp and mixed motions (zoom latch with a lateral part, push against tilt).
const std::vector<Input> kInputs = {
    {"z+", {{0, 0, 300, 0, 0, 0, 0.6}}},   {"z-", {{0, 0, -300, 0, 0, 0, 0.6}}},
    {"rx+", {{0, 0, 0, 300, 0, 0, 0.6}}},  {"rx-", {{0, 0, 0, -300, 0, 0, 0.6}}},
    {"x+", {{300, 0, 0, 0, 0, 0, 0.6}}},   {"x-", {{-300, 0, 0, 0, 0, 0, 0.6}}},
    {"rz+", {{0, 0, 0, 0, 0, 300, 0.6}}},  {"rz-", {{0, 0, 0, 0, 0, -300, 0.6}}},
    {"ry+", {{0, 0, 0, 0, 300, 0, 0.6}}},  {"ry-", {{0, 0, 0, 0, -300, 0, 0.6}}},
    {"y+", {{0, 300, 0, 0, 0, 0, 0.6}}},   {"y-", {{0, -300, 0, 0, 0, 0, 0.6}}},
    {"z-ramp", {{0, 0, 40, 0, 0, 0, 0.2}, {0, 0, 120, 0, 0, 0, 0.2}, {0, 0, 350, 0, 0, 0, 0.2},
                {0, 0, 0, 0, 0, 0, 0.3}}},
    {"x+rz-hard", {{500, 0, 0, 0, 0, 500, 0.5}, {0, 0, 0, 0, 0, 0, 0.3}}},
    {"z+rx-opposed", {{0, 0, 300, -300, 0, 0, 0.5}}},
    {"ry+x", {{200, 0, 0, 0, 300, 0, 0.6}}},
    {"z+ry", {{0, 0, 300, 0, 120, 0, 0.6}}},
};

struct Config {
    std::string mode;                                         // built-in mode id
    std::vector<std::pair<const char *, const char *>> keys;  // profile keys (layer 0)
};

uint64_t fnv(uint64_t h, const std::string &s) {
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ULL;
    return (h ^ 0xff) * 1099511628211ULL;
}

Settings settingsOf(const Config &c) {
    Settings s;
    for (const auto &kv : c.keys) {
        std::string err;
        if (!applyKeyLayer(s, kv.first, kv.second, 0, &err)) {
            std::fprintf(stderr, "bad key %s=%s: %s\n", kv.first, kv.second, err.c_str());
            std::abort();
        }
    }
    return s;
}

void play(ModeRunner &r, const Settings &s, const Input &in) {
    for (const Step &st : in.steps) {
        RawAxes raw{};
        raw[AX_X] = st.x;
        raw[AX_Y] = st.y;
        raw[AX_Z] = st.z;
        raw[AX_RX] = st.rx;
        raw[AX_RY] = st.ry;
        raw[AX_RZ] = st.rz;
        for (int i = 0; i < static_cast<int>(st.secs / DT + 0.5); ++i) r.tick(s, raw, 0, true, DT);
    }
}

const ModeDef &modeOf(const ModeSet &ms, const std::string &id) {
    const ModeDef *m = ms.find(id);
    if (!m) {
        std::fprintf(stderr, "unknown mode %s\n", id.c_str());
        std::abort();
    }
    return *m;
}

std::string runInput(const ModeSet &ms, const Config &c, const Input &in) {
    const Settings s = settingsOf(c);
    Rec o;
    ModeRunner r(&o);
    r.setMode(modeOf(ms, c.mode));
    play(r, s, in);
    r.releaseAll();
    uint64_t h = 1469598103934665603ULL;
    for (const auto &e : o.ev) h = fnv(h, e);
    char buf[48];
    std::snprintf(buf, sizeof buf, "%zu:%08x", o.ev.size(), static_cast<unsigned>(h ^ (h >> 32)));
    return buf;
}

std::vector<Config> configs(const ModeSet &ms) {
    std::vector<Config> v;
    for (const ModeDef &m : ms.modes) v.push_back({m.id, {}});
    const char *funcs[] = {"default",
                           "none",
                           "wheel_v",
                           "wheel_h",
                           "keys:up,down",
                           "keys:ctrl+pageup,ctrl+pagedown",
                           "step:left,right",
                           "step:pageup,pagedown"};
    for (const char *k : {"V_ACTION", "H_ACTION", "TWIST_ACTION"})
        for (const char *f : funcs) v.push_back({"normal", {{k, f}}});
    for (const char *k : {"V_PUSH_ACTION", "V_TILT_ACTION", "H_PUSH_ACTION", "H_TILT_ACTION"})
        for (const char *f : {"none", "wheel_v", "wheel_h", "keys:up,down", "step:left,right"})
            v.push_back({"normal", {{k, f}}});
    v.push_back({"normal", {{"TWIST_ACTION", "zoom"}}});
    v.push_back({"normal", {{"V_TILT_ACTION", "none"}, {"V_PUSH_ACTION", "keys:up,down"}}});
    v.push_back({"normal", {{"H_PUSH_ACTION", "step:left,right"}, {"H_TILT_ACTION", "keys:pageup,pagedown"}}});
    v.push_back({"normal", {{"V_ACTION", "step:up,down"}, {"STEP_TRIGGER", "8"}}});
    v.push_back({"normal", {{"V_ACTION", "step:up,down"}, {"STEP_TRIGGER", "60"}}});
    v.push_back({"normal", {{"H_ACTION", "wheel_v"}, {"V_ACTION", "wheel_h"}, {"TWIST_ACTION", "keys:plus,minus"}}});
    v.push_back({"normal", {{"V_ACTION", "keys:up,down"}, {"H_ACTION", "keys:left,right"}, {"TWIST_ACTION", "none"}}});
    v.push_back({"tabs", {{"V_ACTION", "wheel_h"}}});
    v.push_back({"browse", {{"V_PUSH_ACTION", "none"}}});
    return v;
}

std::string label(const Config &c) {
    std::string l = c.mode;
    for (const auto &kv : c.keys) l += std::string(" ") + kv.first + "=" + kv.second;
    return l;
}

struct Golden {
    const char *label;
    const char *tokens;  // one token per kInputs entry, space separated
};

// TABLE-BEGIN
const Golden kGolden[] = {
    {"normal", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"off", "0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33"},
    {"timeline", "0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 25:124e1d8c 25:628a44cb 25:628a44cb 25:124e1d8c 39:2f87aaef 39:919bf2de 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 55:edeccd96 11:d82863f1"},
    {"browse", "40:a2d7f847 40:4fc8cfda 40:a2d7f847 40:4fc8cfda 25:124e1d8c 25:628a44cb 25:628a44cb 25:124e1d8c 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:395d2741 0:67f80c33 0:67f80c33 38:437897f8 40:a2d7f847"},
    {"fast", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"tracks", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 1:359a8ee3 1:cf56a8d6 1:cf56a8d6 1:359a8ee3 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 39:b9c744f6 40:f6607ac3"},
    {"volume", "40:85434585 40:8e7b1eab 40:85434585 40:8e7b1eab 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:d36f32bc 0:67f80c33 0:67f80c33 38:437897f8 40:85434585"},
    {"map", "40:8d85613a 40:357461b2 40:8d85613a 40:357461b2 25:124e1d8c 25:628a44cb 25:628a44cb 25:124e1d8c 39:8f0758c5 39:53a737ad 0:67f80c33 0:67f80c33 21:bd87b409 0:67f80c33 0:67f80c33 55:714512ee 51:8a970834"},
    {"media", "40:8d85613a 40:357461b2 40:8d85613a 40:357461b2 25:124e1d8c 25:628a44cb 25:628a44cb 25:124e1d8c 1:afb1a55d 1:afd4da27 0:67f80c33 0:67f80c33 21:bd87b409 0:67f80c33 0:67f80c33 17:4f7a27c4 41:63dbedfa"},
    {"photo", "42:9bb7b969 42:3de4dd37 42:9bb7b969 42:3de4dd37 25:124e1d8c 25:628a44cb 25:628a44cb 25:124e1d8c 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 23:05b7fce6 0:67f80c33 0:67f80c33 16:906c86f5 42:9bb7b969"},
    {"precision", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"sheet", "40:8d85613a 40:357461b2 40:8d85613a 40:357461b2 25:124e1d8c 25:628a44cb 25:628a44cb 25:124e1d8c 39:f158ebd9 39:ca7d73c1 0:67f80c33 0:67f80c33 21:bd87b409 0:67f80c33 0:67f80c33 55:269264ce 51:cf0b6fdf"},
    {"slides", "40:a2d7f847 40:4fc8cfda 40:a2d7f847 40:4fc8cfda 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 21:395d2741 0:67f80c33 0:67f80c33 0:67f80c33 40:a2d7f847"},
    {"tabs", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 75:e9597147 75:9bd13001 75:9bd13001 75:e9597147 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"vertical-only", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 0:67f80c33 40:f6607ac3"},
    {"zoom-pan", "42:9bb7b969 42:3de4dd37 42:9bb7b969 42:3de4dd37 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 39:4e15d7a4 39:bf613859 0:67f80c33 0:67f80c33 23:05b7fce6 0:67f80c33 0:67f80c33 55:64a2f7b6 53:874e462d"},
    {"normal V_ACTION=default", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal V_ACTION=none", "0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 38:437897f8 0:67f80c33"},
    {"normal V_ACTION=wheel_v", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal V_ACTION=wheel_h", "40:3138d2a9 40:2478be04 40:3138d2a9 40:2478be04 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:0c777002 0:67f80c33 0:67f80c33 38:437897f8 40:3138d2a9"},
    {"normal V_ACTION=keys:up,down", "40:8d85613a 40:357461b2 40:8d85613a 40:357461b2 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:bd87b409 0:67f80c33 0:67f80c33 38:437897f8 40:8d85613a"},
    {"normal V_ACTION=keys:ctrl+pageup,ctrl+pagedown", "120:95cd3fd7 120:e6f1e4c4 120:95cd3fd7 120:e6f1e4c4 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 63:c2fcf281 0:67f80c33 0:67f80c33 38:437897f8 120:95cd3fd7"},
    {"normal V_ACTION=step:left,right", "1:5005fbda 1:7e9f9b7e 1:5005fbda 1:7e9f9b7e 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 1:5005fbda 0:67f80c33 0:67f80c33 38:437897f8 1:5005fbda"},
    {"normal V_ACTION=step:pageup,pagedown", "1:f5398cca 1:c84d6b78 1:f5398cca 1:c84d6b78 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 1:f5398cca 0:67f80c33 0:67f80c33 38:437897f8 1:f5398cca"},
    {"normal H_ACTION=default", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_ACTION=none", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_ACTION=wheel_v", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:e62e6843 25:53b76326 25:53b76326 25:e62e6843 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_ACTION=wheel_h", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_ACTION=keys:up,down", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:040ed171 25:3bcb172e 25:3bcb172e 25:040ed171 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_ACTION=keys:ctrl+pageup,ctrl+pagedown", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 75:e9597147 75:9bd13001 75:9bd13001 75:e9597147 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_ACTION=step:left,right", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 1:5005fbda 1:7e9f9b7e 1:7e9f9b7e 1:5005fbda 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 39:b585fc03 40:f6607ac3"},
    {"normal H_ACTION=step:pageup,pagedown", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 1:f5398cca 1:c84d6b78 1:c84d6b78 1:f5398cca 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 39:7325b55c 40:f6607ac3"},
    {"normal TWIST_ACTION=default", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal TWIST_ACTION=none", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 16:c9434d95 40:f6607ac3"},
    {"normal TWIST_ACTION=wheel_v", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 39:e7e13594 39:8ba8d175 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 55:7339fa8d 51:0194d9ca"},
    {"normal TWIST_ACTION=wheel_h", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 39:4e15d7a4 39:bf613859 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 55:64a2f7b6 51:9764107a"},
    {"normal TWIST_ACTION=keys:up,down", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 39:8487e861 39:34e01323 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 55:32016409 51:47ba5dd7"},
    {"normal TWIST_ACTION=keys:ctrl+pageup,ctrl+pagedown", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 117:52e96b34 117:10c8c259 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 133:b92c8b58 73:8323d97a"},
    {"normal TWIST_ACTION=step:left,right", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 1:5005fbda 1:7e9f9b7e 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 17:61f90973 41:79fbada2"},
    {"normal TWIST_ACTION=step:pageup,pagedown", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 1:f5398cca 1:c84d6b78 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 17:911fa77b 41:58e5ceed"},
    {"normal V_PUSH_ACTION=none", "0:67f80c33 0:67f80c33 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 33:639aaad8 38:437897f8 0:67f80c33"},
    {"normal V_PUSH_ACTION=wheel_v", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 66:90b5ba33 38:437897f8 40:f6607ac3"},
    {"normal V_PUSH_ACTION=wheel_h", "40:3138d2a9 40:2478be04 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:0c777002 0:67f80c33 66:3de4c1df 38:437897f8 40:3138d2a9"},
    {"normal V_PUSH_ACTION=keys:up,down", "40:8d85613a 40:357461b2 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:bd87b409 0:67f80c33 66:8c0adc7f 38:437897f8 40:8d85613a"},
    {"normal V_PUSH_ACTION=step:left,right", "1:5005fbda 1:7e9f9b7e 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 1:5005fbda 0:67f80c33 34:4c427653 38:437897f8 1:5005fbda"},
    {"normal V_TILT_ACTION=none", "40:f6607ac3 40:db732391 0:67f80c33 0:67f80c33 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 33:a20ca5d9 38:437897f8 40:f6607ac3"},
    {"normal V_TILT_ACTION=wheel_v", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 66:90b5ba33 38:437897f8 40:f6607ac3"},
    {"normal V_TILT_ACTION=wheel_h", "40:f6607ac3 40:db732391 40:3138d2a9 40:2478be04 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 66:5d418b9c 38:437897f8 40:f6607ac3"},
    {"normal V_TILT_ACTION=keys:up,down", "40:f6607ac3 40:db732391 40:8d85613a 40:357461b2 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 66:1db12910 38:437897f8 40:f6607ac3"},
    {"normal V_TILT_ACTION=step:left,right", "40:f6607ac3 40:db732391 1:5005fbda 1:7e9f9b7e 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 34:3a709472 38:437897f8 40:f6607ac3"},
    {"normal H_PUSH_ACTION=none", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 0:67f80c33 0:67f80c33 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 36:3b5ba76d 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_PUSH_ACTION=wheel_v", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:e62e6843 25:53b76326 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 72:967467fa 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_PUSH_ACTION=wheel_h", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 72:d0e1837f 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_PUSH_ACTION=keys:up,down", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:040ed171 25:3bcb172e 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 72:50ff611e 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_PUSH_ACTION=step:left,right", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 1:5005fbda 1:7e9f9b7e 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 37:b6f8d37a 0:67f80c33 39:b585fc03 40:f6607ac3"},
    {"normal H_TILT_ACTION=none", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 0:67f80c33 0:67f80c33 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 36:9407bcb9 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_TILT_ACTION=wheel_v", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:53b76326 25:e62e6843 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 72:ac2f508d 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_TILT_ACTION=wheel_h", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 72:d0e1837f 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_TILT_ACTION=keys:up,down", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:3bcb172e 25:040ed171 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 72:1c7c1880 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal H_TILT_ACTION=step:left,right", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 1:7e9f9b7e 1:5005fbda 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 37:7df83e2c 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal TWIST_ACTION=zoom", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 0:67f80c33 0:67f80c33 38:437897f8 40:f6607ac3"},
    {"normal V_TILT_ACTION=none V_PUSH_ACTION=keys:up,down", "40:8d85613a 40:357461b2 0:67f80c33 0:67f80c33 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:bd87b409 0:67f80c33 33:8c4f807a 38:437897f8 40:8d85613a"},
    {"normal H_PUSH_ACTION=step:left,right H_TILT_ACTION=keys:pageup,pagedown", "40:f6607ac3 40:db732391 40:f6607ac3 40:db732391 1:5005fbda 1:7e9f9b7e 25:97758b5c 25:f043c3de 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:3903195b 37:ae6764ec 0:67f80c33 39:b585fc03 40:f6607ac3"},
    {"normal V_ACTION=step:up,down STEP_TRIGGER=8", "1:10c6ab5e 1:e91a1281 1:10c6ab5e 1:e91a1281 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 1:10c6ab5e 0:67f80c33 0:67f80c33 38:437897f8 1:10c6ab5e"},
    {"normal V_ACTION=step:up,down STEP_TRIGGER=60", "1:10c6ab5e 1:e91a1281 1:10c6ab5e 1:e91a1281 25:35e10d1e 25:d935a935 25:d935a935 25:35e10d1e 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 1:10c6ab5e 0:67f80c33 0:67f80c33 38:437897f8 1:10c6ab5e"},
    {"normal H_ACTION=wheel_v V_ACTION=wheel_h TWIST_ACTION=keys:plus,minus", "40:3138d2a9 40:2478be04 40:3138d2a9 40:2478be04 25:e62e6843 25:53b76326 25:53b76326 25:e62e6843 39:53a737ad 39:8f0758c5 0:67f80c33 0:67f80c33 21:0c777002 0:67f80c33 0:67f80c33 55:a6dc9689 51:1ba6e573"},
    {"normal V_ACTION=keys:up,down H_ACTION=keys:left,right TWIST_ACTION=none", "40:8d85613a 40:357461b2 40:8d85613a 40:357461b2 25:124e1d8c 25:628a44cb 25:628a44cb 25:124e1d8c 0:67f80c33 0:67f80c33 0:67f80c33 0:67f80c33 21:bd87b409 0:67f80c33 0:67f80c33 16:906c86f5 40:8d85613a"},
    {"tabs V_ACTION=wheel_h", "40:3138d2a9 40:2478be04 40:3138d2a9 40:2478be04 75:e9597147 75:9bd13001 75:9bd13001 75:e9597147 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:0c777002 0:67f80c33 0:67f80c33 38:437897f8 40:3138d2a9"},
    {"browse V_PUSH_ACTION=none", "40:a2d7f847 40:4fc8cfda 40:a2d7f847 40:4fc8cfda 25:124e1d8c 25:628a44cb 25:628a44cb 25:124e1d8c 38:437897f8 38:f724bea4 0:67f80c33 0:67f80c33 21:395d2741 0:67f80c33 0:67f80c33 38:437897f8 40:a2d7f847"},
};
// TABLE-END

int failures = 0;
#define CHECK(cond, ...)                                                                \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            ++failures;                                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond);      \
            std::fprintf(stderr, __VA_ARGS__);                                          \
            std::fprintf(stderr, "\n");                                                 \
        }                                                                               \
    } while (0)


// Readable spot checks of the table: what the functions do in plain numbers.
std::vector<std::string> events(const ModeSet &ms, const Config &c, const char *input) {
    const Settings s = settingsOf(c);
    Rec o;
    ModeRunner r(&o);
    r.setMode(modeOf(ms, c.mode));
    for (const Input &in : kInputs)
        if (std::string(in.name) == input) play(r, s, in);
    return o.ev;
}

void testReadable(const ModeSet &ms) {
    // step: one press per crossing of the trigger, on the side of the deflection; the opposite side fires the other key
    auto e = events(ms, {"normal", {{"V_ACTION", "step:left,right"}}}, "z+");
    CHECK(e.size() == 1 && e[0] == "tap:right", "step z+: %zu events", e.size());
    e = events(ms, {"normal", {{"V_ACTION", "step:left,right"}}}, "z-");
    CHECK(e.size() == 1 && e[0] == "tap:left", "step z-: %zu events", e.size());
    // keys: repeats while deflected
    e = events(ms, {"normal", {{"V_ACTION", "keys:up,down"}}}, "z+");
    CHECK(e.size() > 20 && e[0] == "tap:down", "keys z+: %zu events", e.size());
    // none: nothing at all on that channel
    e = events(ms, {"normal", {{"V_ACTION", "none"}}}, "z+");
    CHECK(e.empty(), "none z+: %zu events", e.size());
    // wheel_h on V: the vertical push comes out as horizontal wheel steps only
    e = events(ms, {"normal", {{"V_ACTION", "wheel_h"}}}, "z+");
    CHECK(!e.empty() && e[0].compare(0, 2, "wh") == 0, "wheel_h z+ starts with %s", e.empty() ? "nothing" : e[0].c_str());
    for (const auto &x : e) CHECK(x.compare(0, 2, "wh") == 0, "wheel_h z+ event %s", x.c_str());
    // the built-in default: V push scrolls vertically, H push horizontally, twist zooms (Ctrl held, wheel, released)
    e = events(ms, {"normal", {}}, "z+");
    CHECK(!e.empty() && e[0].compare(0, 2, "wv") == 0, "default z+ first event");
    e = events(ms, {"normal", {}}, "x+");
    CHECK(!e.empty() && e[0].compare(0, 2, "wh") == 0, "default x+ first event");
    e = events(ms, {"normal", {}}, "ry+");
    CHECK(!e.empty() && e[0] == "+ctrl", "default ry+ first event %s", e.empty() ? "nothing" : e[0].c_str());
    // the lift/press motion (y) has no function today
    for (const char *id : {"normal", "tabs", "browse"}) {
        CHECK(events(ms, {id, {}}, "y+").empty(), "y+ in %s", id);
        CHECK(events(ms, {id, {}}, "y-").empty(), "y- in %s", id);
    }
    // the mode "off" produces nothing for any input
    for (const Input &in : kInputs) CHECK(events(ms, {"off", {}}, in.name).empty(), "off %s", in.name);
    // deterministic: the same input twice gives the same events
    CHECK(events(ms, {"tabs", {}}, "x+") == events(ms, {"tabs", {}}, "x+"), "deterministic");
}

}  // namespace

int main() {
    std::vector<std::string> warnings;
    const ModeSet ms = loadModes("/nonexistent-dofscroll-golden", &warnings);
    const std::vector<Config> cfgs = configs(ms);
    const bool print = std::getenv("GOLDEN_PRINT") != nullptr;
    const size_t golden = sizeof(kGolden) / sizeof(kGolden[0]);

    for (size_t i = 0; i < cfgs.size(); ++i) {
        std::string tokens;
        for (size_t j = 0; j < kInputs.size(); ++j) tokens += (j ? " " : "") + runInput(ms, cfgs[i], kInputs[j]);
        if (print) {
            std::printf("    {\"%s\", \"%s\"},\n", label(cfgs[i]).c_str(), tokens.c_str());
            continue;
        }
        const Golden *g = nullptr;
        for (const Golden &x : kGolden)
            if (x.label && label(cfgs[i]) == x.label) g = &x;
        if (!g) {
            std::fprintf(stderr, "FAIL no golden line for [%s]\n", label(cfgs[i]).c_str());
            ++failures;
            continue;
        }
        if (tokens != g->tokens) {
            ++failures;
            std::fprintf(stderr, "FAIL [%s] differs from the golden master\n", g->label);
            size_t a = 0, b = 0;
            auto next = [](const std::string &s, size_t *p) {
                size_t e = s.find(' ', *p);
                std::string t = s.substr(*p, e == std::string::npos ? e : e - *p);
                *p = e == std::string::npos ? s.size() : e + 1;
                return t;
            };
            for (const Input &in : kInputs) {
                std::string x = next(tokens, &a), y = next(g->tokens, &b);
                if (x != y) std::fprintf(stderr, "    input %s: now %s, golden %s\n", in.name, x.c_str(), y.c_str());
            }
        }
    }
    if (print) return 0;
    CHECK(golden == cfgs.size(), "golden table has %zu lines, %zu configurations run", golden, cfgs.size());
    testReadable(ms);
    if (failures) return 1;
    std::printf("test_golden_motions: %zu configurations x %zu inputs ok\n", cfgs.size(), kInputs.size());
    return 0;
}
