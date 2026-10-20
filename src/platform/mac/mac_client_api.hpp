// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The client interface of the installed 3Dconnexion driver on macOS, as far as it is documented
// publicly. NOTHING of the vendor is included: these declarations were written for dofscroll from the
// public sources named at each item, and the library itself is loaded at runtime from the installed
// driver (dlopen) - see docs/legal-notes.md. 3Dconnexion (a Logitech company) owns the driver, the
// interface and the names; dofscroll is not affiliated with or endorsed by them.
//
// Primary public source for every item marked [B]: Blender, a GPL-2.0-or-later program that declares
// the same interface for its own use: intern/ghost/intern/GHOST_NDOFManagerCocoa.mm (project
// https://projects.blender.org/blender/blender, mirror https://github.com/blender/blender), which was
// read for the facts, not copied. Items marked [O] come from the plan text of another open-source
// project (issue 175 of https://github.com/openfollowapp/openfollow: raw 6DOF data only through the
// legacy framework; SetConnexionHandlers takes a "useSeparateThread" flag).
//
// NOT established from public sources and therefore left out: the client mode values other than
// take-over (a plug-in mode exists, its value is not documented publicly), the wildcard client
// signature, the message types other than device state, and the meaning of the "version" field. They
// are listed in docs/port-architecture.md ("Driver interface backend").
#pragma once

#include <cstdint>
#include <string>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace spn {
namespace mac3dx {

// ---- constants -----------------------------------------------------------------------------------
constexpr uint16_t kClientModeTakeOver = 1;       // [B] kConnexionClientModeTakeOver
constexpr uint32_t kMaskAll = 0x3fff;              // [B] kConnexionMaskAll: axes and buttons
constexpr uint32_t kMaskAxis = 0x3f00;             // [B] kConnexionMaskAxis: axes only
constexpr uint32_t kMaskAllButtons = 0xffffffffu;  // [B] kConnexionMaskAllButtons (SetConnexionClientButtonMask)
constexpr uint16_t kCmdHandleButtons = 2;          // [B] kConnexionCmdHandleButtons: state.buttons is valid
constexpr uint16_t kCmdHandleAxis = 3;             // [B] kConnexionCmdHandleAxis: state.axis[] is valid
constexpr uint32_t kMsgDeviceState = 0x33645352;   // [B] kConnexionMsgDeviceState = '3dSR'
constexpr uint32_t kCtlGetDeviceId = 0x33646964;   // [B] kConnexionCtlGetDeviceID = '3did'; result = vendor << 16 | product
constexpr uint32_t kClientSignature = 0x646f6673;  // 'dofs': our own four-letter code (the driver uses it to look up per-application settings; unknown to the driver)

// ---- the device state message --------------------------------------------------------------------
// [B] ConnexionDeviceState, packed on 2-byte boundaries. `axis` is tx, ty, tz, rx, ry, rz. Messages are
// broadcast to all clients: only those with state.client == our client id are for us.
#pragma pack(push, 2)
struct DeviceState {
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
static_assert(sizeof(DeviceState) == 48, "ConnexionDeviceState layout (2-byte packing) changed");

// ---- function types ------------------------------------------------------------------------------
// [B] callback and function signatures. SetConnexionHandlers returns 0 on success; RegisterConnexionClient
// returns the client id (0 = failure, e.g. registration blocked by the App Sandbox [O]); the name is a
// Pascal string (first byte = length) [B].
using MessageHandler = void (*)(uint32_t productId, uint32_t messageType, void *messageArgument);
using AddedHandler = void (*)(uint32_t productId);
using RemovedHandler = void (*)(uint32_t productId);
using SetConnexionHandlers_t = int16_t (*)(MessageHandler, AddedHandler, RemovedHandler, bool useSeparateThread);
using CleanupConnexionHandlers_t = void (*)();
using RegisterConnexionClient_t = uint16_t (*)(uint32_t signature, const char *pascalName, uint16_t mode, uint32_t mask);
using SetConnexionClientButtonMask_t = void (*)(uint16_t clientId, uint32_t buttonMask);
using UnregisterConnexionClient_t = void (*)(uint16_t clientId);
using ConnexionClientControl_t = int16_t (*)(uint16_t clientId, uint32_t message, int32_t param, int32_t *result);

struct Api {
    void *module = nullptr;
    SetConnexionHandlers_t SetConnexionHandlers = nullptr;
    CleanupConnexionHandlers_t CleanupConnexionHandlers = nullptr;
    RegisterConnexionClient_t RegisterConnexionClient = nullptr;
    SetConnexionClientButtonMask_t SetConnexionClientButtonMask = nullptr;  // optional
    UnregisterConnexionClient_t UnregisterConnexionClient = nullptr;
    ConnexionClientControl_t ConnexionClientControl = nullptr;  // optional (device model)
};

#ifndef _WIN32
// Loads the library at `path` (an absolute path chosen by the caller) and resolves the symbols. The
// four functions needed to register and unregister must exist (this is also what [B] requires:
// SetConnexionHandlers decides whether the driver counts as loaded); a library without them is
// "interface mismatch". Optional ones may be missing. On failure nothing stays loaded.
inline bool load(const std::string &path, Api *api, std::string *err) {
    *api = Api();
    void *m = ::dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
    if (!m) {
        const char *e = ::dlerror();
        if (err) *err = "cannot load " + path + (e ? std::string(": ") + e : std::string());
        return false;
    }
    auto sym = [&](const char *n) { return ::dlsym(m, n); };
    Api a;
    a.module = m;
    a.SetConnexionHandlers = reinterpret_cast<SetConnexionHandlers_t>(sym("SetConnexionHandlers"));
    a.CleanupConnexionHandlers = reinterpret_cast<CleanupConnexionHandlers_t>(sym("CleanupConnexionHandlers"));
    a.RegisterConnexionClient = reinterpret_cast<RegisterConnexionClient_t>(sym("RegisterConnexionClient"));
    a.UnregisterConnexionClient = reinterpret_cast<UnregisterConnexionClient_t>(sym("UnregisterConnexionClient"));
    a.SetConnexionClientButtonMask = reinterpret_cast<SetConnexionClientButtonMask_t>(sym("SetConnexionClientButtonMask"));
    a.ConnexionClientControl = reinterpret_cast<ConnexionClientControl_t>(sym("ConnexionClientControl"));
    if (!a.SetConnexionHandlers || !a.CleanupConnexionHandlers || !a.RegisterConnexionClient || !a.UnregisterConnexionClient) {
        ::dlclose(m);
        if (err) *err = path + " does not export the expected client interface (interface mismatch)";
        return false;
    }
    *api = a;
    return true;
}
inline void unload(Api *api) {
    if (api->module) ::dlclose(api->module);
    *api = Api();
}
#endif

}  // namespace mac3dx
}  // namespace spn
