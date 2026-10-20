// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// A FAKE of the macOS 3Dconnexion client library for tests. It is not the vendor's library and contains
// nothing of it: it only exports the function names that src/platform/mac/mac_client_api.hpp declares
// (written from public knowledge), with scriptable behavior, so that the loader and the input source
// can be tested without the real driver. Built as a shared module (.dylib on macOS, .so on Linux) and
// loaded by tests/test_mac_driver_client.cpp through an explicit path.
//
// The control functions (Fake3dx_*) are exported from the same module; the test finds them with
// dlsym on the handle it opened itself. FAKE_3DX_NO_CONTROL builds a variant that lacks
// ConnexionClientControl and SetConnexionClientButtonMask (optional) and FAKE_3DX_MISSING builds one that
// lacks UnregisterConnexionClient (required): the "interface mismatch" case.
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#if defined(_WIN32)
#define FAKE_EXPORT extern "C" __declspec(dllexport)
#else
#define FAKE_EXPORT extern "C" __attribute__((visibility("default")))
#endif

namespace {

#pragma pack(push, 2)
struct State {
    uint16_t version;
    uint16_t client;
    uint16_t command;
    int16_t param;
    int32_t value;
    uint64_t time;
    uint8_t report[8];
    uint16_t appEventPressed;
    int16_t axis[6];
    uint16_t address;
    uint32_t buttons;
};
#pragma pack(pop)

using MessageHandler = void (*)(uint32_t, uint32_t, void *);
using AddedHandler = void (*)(uint32_t);
using RemovedHandler = void (*)(uint32_t);

std::mutex g_mu;
MessageHandler g_msg = nullptr;
AddedHandler g_added = nullptr;
RemovedHandler g_removed = nullptr;
uint16_t g_client = 0;
int g_handlersError = 0;      // SetConnexionHandlers return value
bool g_registerFails = false;  // RegisterConnexionClient returns 0
int32_t g_deviceId = 0;        // result of the GetDeviceID control (vendor << 16 | product)
int g_registrations = 0, g_unregistrations = 0, g_cleanups = 0;
uint32_t g_lastMask = 0, g_lastButtonMask = 0;
char g_lastName[64] = {0};
uint16_t g_lastMode = 0;
uint32_t g_lastSignature = 0;

}  // namespace

FAKE_EXPORT int16_t SetConnexionHandlers(MessageHandler m, AddedHandler a, RemovedHandler r, bool) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_handlersError) return static_cast<int16_t>(g_handlersError);
    g_msg = m;
    g_added = a;
    g_removed = r;
    return 0;
}

FAKE_EXPORT void CleanupConnexionHandlers() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_msg = nullptr;
    g_added = nullptr;
    g_removed = nullptr;
    ++g_cleanups;
}

FAKE_EXPORT uint16_t RegisterConnexionClient(uint32_t signature, const char *pascalName, uint16_t mode, uint32_t mask) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_registerFails) return 0;
    g_client = 7;
    ++g_registrations;
    g_lastSignature = signature;
    g_lastMode = mode;
    g_lastMask = mask;
    const unsigned len = static_cast<unsigned char>(pascalName[0]);
    std::memset(g_lastName, 0, sizeof g_lastName);
    std::memcpy(g_lastName, pascalName + 1, len < sizeof g_lastName - 1 ? len : sizeof g_lastName - 1);
    return g_client;
}

#ifndef FAKE_3DX_MISSING
FAKE_EXPORT void UnregisterConnexionClient(uint16_t) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_client = 0;
    ++g_unregistrations;
}
#endif

#ifndef FAKE_3DX_NO_CONTROL
FAKE_EXPORT void SetConnexionClientButtonMask(uint16_t, uint32_t mask) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_lastButtonMask = mask;
}

FAKE_EXPORT int16_t ConnexionClientControl(uint16_t, uint32_t message, int32_t, int32_t *result) {
    std::lock_guard<std::mutex> lk(g_mu);
    if (message != 0x33646964 || !result) return -1;  // '3did'
    *result = g_deviceId;
    return 0;
}
#endif

// ---- test control ---------------------------------------------------------------------------------

FAKE_EXPORT void Fake3dx_Reset() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_handlersError = 0;
    g_registerFails = false;
    g_deviceId = 0;
    g_registrations = g_unregistrations = g_cleanups = 0;
}
FAKE_EXPORT void Fake3dx_SetBehavior(int handlersError, int registerFails, int32_t deviceId) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_handlersError = handlersError;
    g_registerFails = registerFails != 0;
    g_deviceId = deviceId;
}
// The driver announces a device (or a removal) through the handlers; returns 0 when no handler is installed.
FAKE_EXPORT int Fake3dx_Added() {
    AddedHandler a;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        a = g_added;
    }
    if (!a) return 0;
    a(0);
    return 1;
}
FAKE_EXPORT int Fake3dx_Removed() {
    RemovedHandler r;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        r = g_removed;
    }
    if (!r) return 0;
    r(0);
    return 1;
}
// Sends a device state to the registered handler as the driver would. client 0 = the registered client id
// (a state addressed to another client is sent with the explicit id).
FAKE_EXPORT int Fake3dx_SendState(int command, const int16_t *axis, uint32_t buttons, int clientOverride) {
    MessageHandler m;
    uint16_t id;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        m = g_msg;
        id = g_client;
    }
    if (!m) return 0;
    State s;
    std::memset(&s, 0, sizeof s);
    s.version = 1;
    s.client = clientOverride ? static_cast<uint16_t>(clientOverride) : id;
    s.command = static_cast<uint16_t>(command);
    if (axis)
        for (int i = 0; i < 6; ++i) s.axis[i] = axis[i];
    s.buttons = buttons;
    m(0, 0x33645352 /* '3dSR' */, &s);
    return 1;
}
FAKE_EXPORT int Fake3dx_SendOtherMessage() {
    MessageHandler m;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        m = g_msg;
    }
    if (!m) return 0;
    State s;
    std::memset(&s, 0, sizeof s);
    m(0, 0x12345678, &s);
    return 1;
}
FAKE_EXPORT int Fake3dx_Query(int what) {
    std::lock_guard<std::mutex> lk(g_mu);
    switch (what) {
    case 0: return g_registrations;
    case 1: return g_unregistrations;
    case 2: return g_cleanups;
    case 3: return static_cast<int>(g_lastMask);
    case 4: return static_cast<int>(g_lastButtonMask);
    case 5: return g_lastMode;
    case 6: return static_cast<int>(g_lastSignature);
    case 7: return g_client;
    case 8: return g_msg ? 1 : 0;
    case 9: return std::strcmp(g_lastName, "dofscroll") == 0 ? 1 : 0;
    }
    return -1;
}
