// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Input path selection (src/platform/input_select.hpp): config value parser, the per-OS default table,
// candidate order, and SwitchingInput with fake sources: opening, fallback, "driver not installed",
// explicit modes, retry rules and the no-flapping guarantees.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "platform/input_select.hpp"

using namespace spn;

namespace {

struct Probe {  // shared state of one scripted source, owned by the test
    bool openOk = true;
    bool hasDevice = false;
    int opens = 0, closes = 0;
    bool open = false;
    std::deque<InputEvent> q;
};

class Script : public InputSource {
public:
    explicit Script(std::shared_ptr<Probe> p, const char *n) : p_(std::move(p)), n_(n) {}
    const char *name() const override { return n_; }
    bool open(std::string *err) override {
        ++p_->opens;
        if (!p_->openOk) {
            if (err) *err = "scripted failure";
            return false;
        }
        p_->open = true;
        return true;
    }
    WaitResult wait(InputEvent *out, int) override {
        if (!p_->open) return WaitResult::Closed;
        if (p_->q.empty()) return WaitResult::Timeout;
        *out = p_->q.front();
        p_->q.pop_front();
        return WaitResult::Event;
    }
    std::vector<DeviceInfo> devices() const override {
        std::vector<DeviceInfo> v;
        if (p_->open && p_->hasDevice) v.push_back(DeviceInfo{});
        return v;
    }
    void close() override {
        if (p_->open) ++p_->closes;
        p_->open = false;
    }

private:
    std::shared_ptr<Probe> p_;
    const char *n_;
};

struct Rig {
    std::shared_ptr<Probe> drv = std::make_shared<Probe>(), hid = std::make_shared<Probe>();
    bool installed = true;
    int64_t now = 1'000'000'000;  // ns, fake clock
    std::vector<std::string> logs;
    std::unique_ptr<SwitchingInput> in;

    explicit Rig(InputOs os, InputMode mode = InputMode::Auto) {
        SwitchingInput::Parts parts;
        parts.driverInstalled = [this] { return installed; };
        parts.makeDriver = [this]() -> std::unique_ptr<InputSource> { return std::make_unique<Script>(drv, "drv"); };
        parts.makeHid = [this]() -> std::unique_ptr<InputSource> { return std::make_unique<Script>(hid, "hid"); };
        SwitchTiming t;
        t.probeWindowMs = 40;
        in = std::make_unique<SwitchingInput>(std::move(parts), os, mode, t, [this] { return now; },
                                              [this](const std::string &s) { logs.push_back(s); });
    }
    // One supervision step: the daemon's wait() timed out, `secs` seconds passed.
    WaitResult tick(int secs = 0) {
        now += static_cast<int64_t>(secs) * 1'000'000'000;
        InputEvent e;
        WaitResult r = in->wait(&e, 0);
        if (r == WaitResult::Event && e.type == InputEventType::DevicesChanged) r = in->wait(&e, 0);  // the republish note
        return r;
    }
    bool loggedContains(const std::string &s) const {
        for (const auto &l : logs)
            if (l.find(s) != std::string::npos) return true;
        return false;
    }
};

InputEvent motion() {
    InputEvent e;
    e.type = InputEventType::Motion;
    e.axes = {1, 0, 0, 0, 0, 0};
    return e;
}

}  // namespace

int main() {
    // ---- names and the config value ----
    {
        InputMode m = InputMode::Hid;
        assert(parseInputMode("auto", &m) && m == InputMode::Auto);
        assert(parseInputMode("  Driver\t", &m) && m == InputMode::Driver);
        assert(parseInputMode("HID", &m) && m == InputMode::Hid);
        assert(!parseInputMode("", &m) && !parseInputMode("usb", &m) && m == InputMode::Hid);
        assert(std::string(inputModeName(InputMode::Driver)) == "driver" && std::string(inputPathId(InputPath::Hid)) == "hid");
        assert(std::string(inputPathText(InputPath::Driver)) == "3Dconnexion driver");
        assert(std::string(inputPathText(InputPath::Hid)) == "direct (HID)");
        assert(inputPathFromId("driver") == InputPath::Driver && inputPathFromId("x") == InputPath::None);
    }

    // ---- THE defaults table and the candidate order ----
    {
        assert(autoOrder(InputOs::Mac).first == InputPath::Driver && autoOrder(InputOs::Mac).second == InputPath::Hid);
        assert(autoOrder(InputOs::Windows).first == InputPath::Hid && autoOrder(InputOs::Windows).second == InputPath::Driver);
        using V = std::vector<InputPath>;
        assert(candidatePaths(InputMode::Auto, InputOs::Mac, true) == (V{InputPath::Driver, InputPath::Hid}));
        assert(candidatePaths(InputMode::Auto, InputOs::Mac, false) == (V{InputPath::Hid}));
        assert(candidatePaths(InputMode::Auto, InputOs::Windows, true) == (V{InputPath::Hid, InputPath::Driver}));
        assert(candidatePaths(InputMode::Auto, InputOs::Windows, false) == (V{InputPath::Hid}));
        assert(candidatePaths(InputMode::Driver, InputOs::Windows, true) == (V{InputPath::Driver}));
        assert(candidatePaths(InputMode::Driver, InputOs::Windows, false).empty());
        assert(candidatePaths(InputMode::Hid, InputOs::Mac, true) == (V{InputPath::Hid}));
    }

    // ---- auto, macOS: driver first, labels, log line ----
    {
        Rig r(InputOs::Mac);
        std::string err;
        assert(r.in->open(&err));
        assert(r.in->pathLabel() == "driver" && r.drv->opens == 1 && r.hid->opens == 0);
        assert(r.loggedContains("Input: 3Dconnexion driver (automatic)"));
        InputEvent e;
        assert(r.in->wait(&e, 0) == WaitResult::Event && e.type == InputEventType::DevicesChanged);  // device list republished
        r.in->close();
        assert(r.in->pathLabel().empty() && r.drv->closes == 1);
    }
    // ---- auto, Windows: HID first ----
    {
        Rig r(InputOs::Windows);
        std::string err;
        assert(r.in->open(&err) && r.in->pathLabel() == "hid" && r.drv->opens == 0);
        assert(r.loggedContains("Input: direct (HID) (automatic)"));
    }
    // ---- auto: the preferred path fails to open -> the other one ----
    {
        Rig r(InputOs::Mac);
        r.drv->openOk = false;
        std::string err;
        assert(r.in->open(&err) && r.in->pathLabel() == "hid" && r.drv->opens == 1 && r.hid->opens == 1);
        Rig w(InputOs::Windows);
        w.hid->openOk = false;
        assert(w.in->open(&err) && w.in->pathLabel() == "driver");
        // both fail: open fails with both reasons, logged once for repeated attempts
        w.in->close();
        w.drv->openOk = false;
        assert(!w.in->open(&err) && err.find("direct (HID)") != std::string::npos && err.find("3Dconnexion driver") != std::string::npos);
        const size_t n = w.logs.size();
        assert(!w.in->open(&err) && w.logs.size() == n);
    }
    // ---- driver not installed: only HID is tried, the driver is never created ----
    {
        Rig r(InputOs::Mac);
        r.installed = false;
        std::string err;
        assert(r.in->open(&err) && r.in->pathLabel() == "hid" && r.drv->opens == 0);
    }
    // ---- explicit modes: exactly that path, no fallback ----
    {
        Rig r(InputOs::Windows, InputMode::Driver);
        r.drv->openOk = false;
        std::string err;
        assert(!r.in->open(&err) && r.hid->opens == 0 && r.in->pathLabel().empty());
        r.installed = false;
        assert(!r.in->open(&err) && err.find("not installed") != std::string::npos);
        Rig h(InputOs::Mac, InputMode::Hid);
        h.hid->openOk = false;
        assert(!h.in->open(&err) && h.drv->opens == 0);
        h.hid->openOk = true;
        assert(h.in->open(&err) && h.in->pathLabel() == "hid");
        assert(h.loggedContains("INPUT=hid"));
    }

    // ---- events pass through; Closed from the source is Closed ----
    {
        Rig r(InputOs::Mac);
        std::string err;
        assert(r.in->open(&err));
        InputEvent e;
        assert(r.in->wait(&e, 0) == WaitResult::Event);  // the synthetic DevicesChanged
        r.drv->q.push_back(motion());
        assert(r.in->wait(&e, 0) == WaitResult::Event && e.type == InputEventType::Motion && e.axes[0] == 1);
        r.drv->open = false;  // the source is gone for good
        assert(r.in->wait(&e, 0) == WaitResult::Closed);
    }

    // ---- no flapping: a working path stays, even when the other one would also work ----
    {
        Rig r(InputOs::Mac);
        std::string err;
        assert(r.in->open(&err));
        r.drv->hasDevice = true;
        r.hid->hasDevice = true;
        for (int i = 0; i < 20; ++i) assert(r.tick(60) == WaitResult::Timeout);
        assert(r.in->pathLabel() == "driver" && r.hid->opens == 0);
    }
    // ---- nothing plugged in anywhere: no switching, no probing storm ----
    {
        Rig r(InputOs::Windows);
        std::string err;
        assert(r.in->open(&err));
        for (int i = 0; i < 20; ++i) assert(r.tick(30) == WaitResult::Timeout);
        assert(r.in->pathLabel() == "hid");
        assert(r.drv->opens <= 20 / 1 && r.drv->opens >= 1);  // probed, found no device, closed again
        assert(r.drv->opens == r.drv->closes);
        // reprobe interval: 20 ticks of 30 s with a 30 s interval would be 20 probes; never more than once per interval
        assert(r.drv->opens <= 20);
    }
    // ---- current path without a device, the other one has one -> switch after the delay ----
    {
        Rig r(InputOs::Windows);  // HID first
        std::string err;
        assert(r.in->open(&err));
        r.drv->hasDevice = true;  // the device is only visible through the driver
        for (int i = 0; i < 3; ++i) assert(r.tick(5) == WaitResult::Timeout);  // 15 s: not yet 20
        assert(r.in->pathLabel() == "hid" && r.drv->opens == 0);
        assert(r.tick(10) == WaitResult::Timeout);  // 25 s without a device
        assert(r.in->pathLabel() == "driver" && r.hid->closes == 1);
        assert(r.loggedContains("(switched)"));
        InputEvent e;
        assert(r.in->wait(&e, 0) == WaitResult::Event && e.type == InputEventType::DevicesChanged);
        // the device is not visible on HID yet: the driver path stays however long nothing happens
        for (int i = 0; i < 20; ++i) assert(r.tick(60) == WaitResult::Timeout);
        assert(r.in->pathLabel() == "driver");
        // now it appears on HID as well: the quiet fallback moves to the preferred path once and stays there
        r.hid->hasDevice = true;
        assert(r.tick(60) == WaitResult::Timeout && r.in->pathLabel() == "hid");
        const int opens = r.hid->opens + r.drv->opens;
        for (int i = 0; i < 20; ++i) assert(r.tick(60) == WaitResult::Timeout);
        assert(r.in->pathLabel() == "hid" && r.hid->opens + r.drv->opens == opens);
    }
    // ---- on the fallback path, quiet, and the preferred path now has a device -> move to it (once) ----
    {
        Rig r(InputOs::Mac);
        r.drv->openOk = false;  // the driver service was not running yet
        std::string err;
        assert(r.in->open(&err) && r.in->pathLabel() == "hid");
        r.hid->hasDevice = true;
        r.drv->openOk = true;
        r.drv->hasDevice = true;
        assert(r.tick(1) == WaitResult::Timeout && r.in->pathLabel() == "hid");  // not quiet long enough
        assert(r.tick(15) == WaitResult::Timeout);
        assert(r.in->pathLabel() == "driver");
        for (int i = 0; i < 20; ++i) assert(r.tick(60) == WaitResult::Timeout);
        assert(r.in->pathLabel() == "driver" && r.drv->closes == 0);
    }
    // ---- while the fallback path delivers, the preferred path is not probed ----
    {
        Rig r(InputOs::Mac);
        r.drv->openOk = false;
        std::string err;
        assert(r.in->open(&err));
        r.hid->hasDevice = true;
        r.drv->openOk = true;
        r.drv->hasDevice = true;
        InputEvent e;
        assert(r.in->wait(&e, 0) == WaitResult::Event);
        for (int i = 0; i < 10; ++i) {
            r.hid->q.push_back(motion());
            assert(r.in->wait(&e, 0) == WaitResult::Event);  // an event every 5 s: never quiet for 10 s
            r.now += 5'000'000'000;
            assert(r.tick(0) == WaitResult::Timeout);
        }
        assert(r.in->pathLabel() == "hid");
    }
    // ---- INPUT changed while running ----
    {
        Rig r(InputOs::Mac);  // auto: driver
        std::string err;
        r.hid->hasDevice = true;
        r.drv->hasDevice = true;
        assert(r.in->open(&err) && r.in->pathLabel() == "driver");
        r.in->setInputMode(InputMode::Hid);
        assert(r.tick(0) == WaitResult::Closed);  // reopen with the new mode
        r.in->close();
        assert(r.in->open(&err) && r.in->pathLabel() == "hid");
        r.in->setInputMode(InputMode::Auto);  // toward the preferred path, which has a device
        assert(r.tick(0) == WaitResult::Timeout && r.in->pathLabel() == "driver");
        r.in->setInputMode(InputMode::Driver);  // already there: no reopen
        assert(r.tick(0) == WaitResult::Timeout && r.in->pathLabel() == "driver");
        // auto toward a preferred path without a device stays where it is
        Rig q(InputOs::Mac, InputMode::Hid);
        q.hid->hasDevice = true;
        assert(q.in->open(&err));
        q.in->setInputMode(InputMode::Auto);
        assert(q.tick(0) == WaitResult::Timeout && q.in->pathLabel() == "hid");
    }

    std::puts("test_input_select: ok");
    return 0;
}
