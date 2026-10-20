// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The macOS driver-interface input source (src/platform/mac/mac_driver_input.cpp) against a FAKE driver
// library (tests/fake_driver/fake_3dx_client.cpp). The source is POSIX code, so this test runs on macOS
// and on Linux. The library path is passed explicitly: the production code only knows the fixed system
// location. Proves: loading, registration, scripted axis and button states, device removal, driver not
// installed, interface mismatch, failure to register and to install the handlers.
#undef NDEBUG
#include <dlfcn.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <string>

#include "platform/mac/mac_client_api.hpp"
#include "platform/mac/mac_driver_input.hpp"

#ifndef FAKE_DRIVER_PATH
#error "FAKE_DRIVER_PATH must name the fake client library"
#endif

using namespace spn;

struct Fake {
    void *h = nullptr;
    void (*reset)() = nullptr;
    void (*setBehavior)(int, int, int32_t) = nullptr;
    int (*added)() = nullptr;
    int (*removed)() = nullptr;
    int (*sendState)(int, const int16_t *, uint32_t, int) = nullptr;
    int (*sendOther)() = nullptr;
    int (*query)(int) = nullptr;

    explicit Fake(const char *path) {
        h = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
        assert(h && "cannot load the fake driver");
        reset = reinterpret_cast<void (*)()>(::dlsym(h, "Fake3dx_Reset"));
        setBehavior = reinterpret_cast<void (*)(int, int, int32_t)>(::dlsym(h, "Fake3dx_SetBehavior"));
        added = reinterpret_cast<int (*)()>(::dlsym(h, "Fake3dx_Added"));
        removed = reinterpret_cast<int (*)()>(::dlsym(h, "Fake3dx_Removed"));
        sendState = reinterpret_cast<int (*)(int, const int16_t *, uint32_t, int)>(::dlsym(h, "Fake3dx_SendState"));
        sendOther = reinterpret_cast<int (*)()>(::dlsym(h, "Fake3dx_SendOtherMessage"));
        query = reinterpret_cast<int (*)(int)>(::dlsym(h, "Fake3dx_Query"));
        assert(reset && setBehavior && added && removed && sendState && sendOther && query);
        reset();
    }
};
enum Q { Registrations, Unregistrations, Cleanups, Mask, ButtonMask, Mode, Signature, Client, HandlersSet, NameOk };

static bool next(InputSource &s, InputEvent *e, int ms = 2000) { return s.wait(e, ms) == WaitResult::Event; }

static InputEvent expectType(InputSource &s, InputEventType t) {
    InputEvent e;
    for (int i = 0; i < 20; ++i) {
        assert(next(s, &e));
        if (e.type == t) return e;
    }
    assert(false && "event type not seen");
    return e;
}

int main() {
    Fake fk(FAKE_DRIVER_PATH);
    namespace m = mac3dx;

    // ---- the loader on its own ----
    {
        m::Api api;
        std::string err;
        assert(m::load(FAKE_DRIVER_PATH, &api, &err) && api.SetConnexionHandlers && api.ConnexionClientControl);
        m::unload(&api);
        assert(!m::load("/nonexistent/3DconnexionClient", &api, &err) && !err.empty() && !api.module);
    }

    // ---- driver not installed: the loader fails, the source does not open ----
    {
        auto s = makeMacDriverInputFromPath("/nonexistent/3DconnexionClient", nullptr);
        std::string err;
        assert(!s->open(&err) && err.find("cannot load") != std::string::npos);
    }
    // the production check says the same on a machine without the framework (Linux and CI runners)
    {
        std::string why;
        const bool installed = macDriverInstalled(&why);
        if (!installed) assert(!why.empty());
    }

    // ---- registration parameters, a device, scripted states ----
    {
        // vendor 0x256f, product 0xc635 (SpaceMouse Compact in devices/)
        fk.setBehavior(0, 0, (0x256f << 16) | 0xc635);
        DriverModelLookup lookup = [](unsigned v, unsigned p, std::string *name, int *buttons) {
            if (v == 0x256f && p == 0xc635) {
                *name = "SpaceMouse Compact";
                *buttons = 2;
                return true;
            }
            return false;
        };
        auto s = makeMacDriverInputFromPath(FAKE_DRIVER_PATH, lookup);
        std::string err;
        assert(s->open(&err) && err.empty());
        assert(fk.query(Registrations) == 1 && fk.query(Client) == 7 && fk.query(Mode) == m::kClientModeTakeOver);
        assert(fk.query(Mask) == static_cast<int>(m::kMaskAll) && fk.query(ButtonMask) == -1 /* 0xffffffff */);
        assert(fk.query(Signature) == static_cast<int>(m::kClientSignature) && fk.query(NameOk) == 1);

        // the device that was attached before the registration is found through the control call
        InputEvent e = expectType(*s, InputEventType::DevicesChanged);
        (void)e;
        auto devs = s->devices();
        assert(devs.size() == 1 && devs[0].name == "SpaceMouse Compact" && devs[0].vendor == 0x256f &&
               devs[0].product == 0xc635 && devs[0].reportedButtons == 2);

        // axes: tx ty tz rx ry rz in the order of the message, factor 1.0
        const int16_t ax[6] = {10, -20, 30, -40, 50, -60};
        assert(fk.sendState(m::kCmdHandleAxis, ax, 0, 0) == 1);
        e = expectType(*s, InputEventType::Motion);
        assert(e.axes[0] == 10 && e.axes[1] == -20 && e.axes[2] == 30 && e.axes[3] == -40 && e.axes[4] == 50 &&
               e.axes[5] == -60 && e.device == 1);
        // buttons: bit n = hardware index n; one event per changed bit
        assert(fk.sendState(m::kCmdHandleButtons, nullptr, 0x5, 0) == 1);
        e = expectType(*s, InputEventType::Button);
        assert(e.button == 0 && e.down);
        e = expectType(*s, InputEventType::Button);
        assert(e.button == 2 && e.down);
        assert(fk.sendState(m::kCmdHandleButtons, nullptr, 0x4, 0) == 1);
        e = expectType(*s, InputEventType::Button);
        assert(e.button == 0 && !e.down);
        // a state for another client and an unknown message type are ignored
        const int16_t other[6] = {1, 1, 1, 1, 1, 1};
        assert(fk.sendState(m::kCmdHandleAxis, other, 0, 99) == 1);
        assert(fk.sendOther() == 1);
        InputEvent none;
        assert(s->wait(&none, 100) == WaitResult::Timeout);

        // removal: neutral state first (button 2 released, axes zero), then the device list changes
        assert(fk.removed() == 1);
        bool sawButtonUp = false, sawZero = false, sawChanged = false;
        for (int i = 0; i < 6 && !sawChanged; ++i) {
            if (!next(*s, &e)) break;
            if (e.type == InputEventType::Button && e.button == 2 && !e.down) sawButtonUp = true;
            if (e.type == InputEventType::Motion && e.axes == std::array<int, 6>{}) sawZero = true;
            if (e.type == InputEventType::DevicesChanged) sawChanged = true;
        }
        assert(sawButtonUp && sawZero && sawChanged && s->devices().empty());
        // a state while no device is present produces nothing (and the source re-announces the device on demand)
        assert(fk.added() == 1);
        expectType(*s, InputEventType::DevicesChanged);
        assert(s->devices().size() == 1);

        s->close();
        assert(fk.query(Unregistrations) == 1 && fk.query(Cleanups) >= 1);
        while (s->wait(&none, 10) == WaitResult::Event) {
        }  // the teardown events are drained first
        assert(s->wait(&none, 10) == WaitResult::Closed);
        // reopen after close works
        assert(s->open(&err));
        s->close();
    }

    // ---- unknown model: generic name ----
    {
        fk.setBehavior(0, 0, 0x1234abcd);
        auto s = makeMacDriverInputFromPath(FAKE_DRIVER_PATH, [](unsigned, unsigned, std::string *, int *) { return false; });
        std::string err;
        assert(s->open(&err));
        expectType(*s, InputEventType::DevicesChanged);
        auto d = s->devices();
        assert(d.size() == 1 && d[0].name == "SpaceMouse (driver)" && d[0].reportedButtons == 0);
        s->close();
    }

    // ---- no device at registration, the driver announces it later ----
    {
        fk.setBehavior(0, 0, 0);
        auto s = makeMacDriverInputFromPath(FAKE_DRIVER_PATH, nullptr);
        std::string err;
        assert(s->open(&err));
        assert(s->devices().empty());
        fk.setBehavior(0, 0, (0x046d << 16) | 0xc62b);
        assert(fk.added() == 1);
        expectType(*s, InputEventType::DevicesChanged);
        assert(s->devices().size() == 1 && s->devices()[0].vendor == 0x046d);
        s->close();
    }

    // ---- registration refused (driver service not running, sandbox) ----
    {
        fk.setBehavior(0, 1, 0);
        auto s = makeMacDriverInputFromPath(FAKE_DRIVER_PATH, nullptr);
        std::string err;
        assert(!s->open(&err) && err.find("refused the registration") != std::string::npos);
        assert(fk.query(HandlersSet) == 0);  // cleaned up again
        fk.setBehavior(0, 0, 0);
        assert(s->open(&err));  // and the same object can try again
        s->close();
    }

    // ---- handlers cannot be installed ----
    {
        fk.setBehavior(5, 0, 0);
        auto s = makeMacDriverInputFromPath(FAKE_DRIVER_PATH, nullptr);
        std::string err;
        assert(!s->open(&err) && err.find("SetConnexionHandlers failed (code 5)") != std::string::npos);
        fk.setBehavior(0, 0, 0);
    }

    // ---- interface mismatch: a library without the required unregister function ----
#ifdef FAKE_DRIVER_MISSING_PATH
    {
        auto s = makeMacDriverInputFromPath(FAKE_DRIVER_MISSING_PATH, nullptr);
        std::string err;
        assert(!s->open(&err) && err.find("interface mismatch") != std::string::npos);
    }
#endif
    // ---- optional functions missing: still works, generic device name ----
#ifdef FAKE_DRIVER_NOCONTROL_PATH
    {
        auto s = makeMacDriverInputFromPath(FAKE_DRIVER_NOCONTROL_PATH, nullptr);
        std::string err;
        assert(s->open(&err));
        s->close();
    }
#endif

    // ---- two sources at once are refused (the driver's callbacks have no user pointer) ----
    {
        fk.setBehavior(0, 0, 0);
        auto a = makeMacDriverInputFromPath(FAKE_DRIVER_PATH, nullptr);
        auto b = makeMacDriverInputFromPath(FAKE_DRIVER_PATH, nullptr);
        std::string err;
        assert(a->open(&err));
        assert(!b->open(&err) && err.find("already in use") != std::string::npos);
        a->close();
        assert(b->open(&err));
        b->close();
    }

    std::puts("test_mac_driver_client: ok");
    return 0;
}
