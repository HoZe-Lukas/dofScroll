// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Release paths of the macOS output with a fake output (src/platform/mac/mac_release_guard.hpp).
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <set>

#include "platform/mac/mac_release_guard.hpp"

using namespace spn;

namespace {

// Keeps the set of "pressed" keys like the real outputs and counts the releases.
class FakeOutput : public Output {
public:
    const char *name() const override { return "fake"; }
    bool init(std::string *) override { return true; }
    void wheelV(int) override {}
    void wheelH(int) override {}
    void setKey(Key k, bool down) override {
        if (down) held.insert(static_cast<int>(k));
        else held.erase(static_cast<int>(k));
    }
    void tapKey(Key) override {}
    void mouseButton(int b, bool down) override {
        if (down) buttons.insert(b);
        else buttons.erase(b);
    }
    void releaseAll() override {
        held.clear();
        buttons.clear();
        ++releases;
    }
    void flush() override { ++flushes; }
    std::set<int> held, buttons;
    int releases = 0, flushes = 0;
};

}  // namespace

int main() {
    mac::ReleaseGuard g;
    FakeOutput a, b;
    g.add(&a);
    g.add(&a);  // idempotent
    g.add(&b);
    g.add(nullptr);
    assert(g.count() == 2);

    a.setKey(Key::Ctrl, true);
    a.mouseButton(1, true);
    b.setKey(Key::Left, true);
    assert(g.releaseAll());  // exit / terminate / sleep path
    assert(a.held.empty() && a.buttons.empty() && b.held.empty());
    assert(a.releases == 1 && b.releases == 1 && a.flushes == 1);

    g.remove(&a);  // a destroyed output unregisters: it is never touched again
    assert(g.count() == 1);
    a.setKey(Key::Ctrl, true);
    assert(g.releaseAll());
    assert(a.held.size() == 1 && a.releases == 1 && b.releases == 2);

    assert(g.releaseAll());  // repeated calls are harmless
    g.remove(&b);
    assert(g.count() == 0 && g.releaseAll());

    std::puts("test_mac_release: ok");
    return 0;
}
