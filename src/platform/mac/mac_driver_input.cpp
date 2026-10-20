// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS driver-interface InputSource (see mac_driver_input.hpp). One thread loads the library,
// installs the handlers, registers the client and then keeps its CFRunLoop turning (the pattern of
// mac_hid_input.cpp); the handlers feed the shared DriverInputCore, whose queue InputSource::wait()
// reads. The driver may call the handlers on its own thread (useSeparateThread = true) or on this
// thread's run loop; both work because the core is thread-safe.
//
// UNVERIFIED without the real driver: that registration succeeds next to CAD programs, whether a
// client that is not the frontmost application receives states (public sources do not say), that raw
// button states arrive with a current driver (Blender switches to application events there), the unit
// of the axis values, and that the added/removed handlers fire for devices attached before the
// registration (the code asks for the device itself after registering).
#include "mac_driver_input.hpp"

#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "../../hid_descriptor.hpp"
#include "../../log_sanitize.hpp"
#include "../driver_input_core.hpp"
#include "../driver_paths.hpp"
#include "../hid_input_core.hpp"
#include "mac_client_api.hpp"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace spn {
namespace {

const char *const LOG = "[dofscroll] ";

class MacDriverInput;

// The driver's callbacks carry no user pointer: one active instance per process. Teardown clears the
// pointer under the mutex before it calls into the driver, so a late callback is ignored.
std::mutex g_activeMu;
MacDriverInput *g_active = nullptr;

class MacDriverInput : public InputSource {
public:
    MacDriverInput(std::string path, DriverModelLookup lookup) : path_(std::move(path)), lookup_(std::move(lookup)) {}
    ~MacDriverInput() override { shutdown(); }

    const char *name() const override { return "3dconnexion-client"; }

    bool open(std::string *err) override {
        if (running_) return true;
        if (thread_.joinable()) {
            if (!threadDone_) {
                if (err) *err = "a previous attempt to register with the driver is still pending";
                return false;
            }
            thread_.join();
        }
        threadDone_ = false;
        core_.open();
        auto ready = std::make_shared<std::promise<std::string>>();
        std::future<std::string> fut = ready->get_future();
        stop_ = false;
        thread_ = std::thread([this, ready] {
            run(ready);
            threadDone_ = true;
        });
        if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            if (err) *err = "timeout while registering with the 3Dconnexion driver";
            stop_ = true;
            return false;
        }
        const std::string res = fut.get();
        if (!res.empty()) {
            if (err) *err = res;
            if (thread_.joinable()) thread_.join();
            return false;
        }
        running_ = true;
        return true;
    }

    WaitResult wait(InputEvent *out, int timeoutMs) override { return core_.wait(out, timeoutMs); }
    std::vector<DeviceInfo> devices() const override { return core_.devices(); }
    void close() override { shutdown(); }

    // ---- driver callbacks (any thread) ----
    void onMessage(uint32_t type, void *arg) {
        if (type != mac3dx::kMsgDeviceState || !arg) return;
        const mac3dx::DeviceState *s = static_cast<const mac3dx::DeviceState *>(arg);
        if (s->client != client_.load()) return;  // broadcast to all clients: only ours
        if (!core_.present()) refreshDevice();
        if (s->command == mac3dx::kCmdHandleAxis) {
            std::array<double, 6> v;
            for (size_t i = 0; i < 6; ++i) v[i] = s->axis[i];
            core_.onAxes(v, kMacDriverAxisScale, kMacDriverAxisScale);
        } else if (s->command == mac3dx::kCmdHandleButtons) {
            core_.onButtons(s->buttons);
        }
    }
    void onAdded() { refreshDevice(); }
    void onRemoved() { core_.deviceRemoved(); }

private:
    void shutdown() {
        running_ = false;
        stop_ = true;
        if (thread_.joinable()) thread_.join();
        core_.close();
    }

    // Asks the driver which model is attached (vendor << 16 | product) and announces the device. A
    // result of 0 or a missing control function gives the generic name.
    void refreshDevice() {
        std::lock_guard<std::mutex> lk(devMu_);
        if (core_.present()) return;
        unsigned vid = 0, pid = 0;
        const uint16_t id = client_.load();
        if (id != 0 && api_.ConnexionClientControl) {
            int32_t result = 0;
            if (api_.ConnexionClientControl(id, mac3dx::kCtlGetDeviceId, 0, &result) == 0) {
                vid = (static_cast<uint32_t>(result) >> 16) & 0xffff;
                pid = static_cast<uint32_t>(result) & 0xffff;
            }
        }
        std::string name;
        int buttons = 0;
        if (vid != 0 && lookup_) {
            if (!lookup_(vid, pid, &name, &buttons)) name.clear();
        }
        core_.deviceAdded(name, vid, pid, buttons);
        std::cerr << LOG << "driver reports a device: " << (name.empty() ? std::string(kDriverFallbackName) : sanitizeForLog(name))
                  << std::endl;
    }

    static void messageThunk(uint32_t, uint32_t type, void *arg) {
        std::lock_guard<std::mutex> lk(g_activeMu);
        if (g_active) g_active->onMessage(type, arg);
    }
    static void addedThunk(uint32_t) {
        std::lock_guard<std::mutex> lk(g_activeMu);
        if (g_active) g_active->onAdded();
    }
    static void removedThunk(uint32_t) {
        std::lock_guard<std::mutex> lk(g_activeMu);
        if (g_active) g_active->onRemoved();
    }

    void pump() {
#ifdef __APPLE__
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.1, false);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
#endif
    }

    void run(const std::shared_ptr<std::promise<std::string>> &ready) {
        std::string err;
        if (!mac3dx::load(path_, &api_, &err)) {
            ready->set_value(err);
            return;
        }
        {
            std::lock_guard<std::mutex> lk(g_activeMu);
            if (g_active) {
                mac3dx::unload(&api_);
                ready->set_value("the driver interface is already in use by this process");
                return;
            }
            g_active = this;
        }
        const int16_t rc = api_.SetConnexionHandlers(&messageThunk, &addedThunk, &removedThunk, true);
        if (rc != 0) {
            release();
            ready->set_value("SetConnexionHandlers failed (code " + std::to_string(rc) + ")");
            return;
        }
        // Pascal string: length byte first.
        static const char kName[] = "\x09"
                                    "dofscroll";
        const uint16_t id = api_.RegisterConnexionClient(mac3dx::kClientSignature, kName, mac3dx::kClientModeTakeOver,
                                                         mac3dx::kMaskAll);
        if (id == 0) {
            release();
            ready->set_value("the 3Dconnexion driver refused the registration (is its service running? "
                             "is the program sandboxed?)");
            return;
        }
        client_.store(id);
        if (api_.SetConnexionClientButtonMask) api_.SetConnexionClientButtonMask(id, mac3dx::kMaskAllButtons);
        ready->set_value("");

        // A device that was attached before the registration: the handlers may not report it.
        if (api_.ConnexionClientControl) {
            int32_t result = 0;
            if (api_.ConnexionClientControl(id, mac3dx::kCtlGetDeviceId, 0, &result) == 0 && result != 0) refreshDevice();
        }
        while (!stop_) pump();

        // Teardown on this thread: neutral state, then leave the driver.
        core_.deviceRemoved();
        release();
    }

    // Stops callbacks, unregisters, cleans up and unloads (this thread only).
    void release() {
        {
            std::lock_guard<std::mutex> lk(g_activeMu);
            if (g_active == this) g_active = nullptr;
        }
        const uint16_t id = client_.exchange(0);
        if (id != 0) api_.UnregisterConnexionClient(id);
        if (api_.CleanupConnexionHandlers) api_.CleanupConnexionHandlers();
        mac3dx::unload(&api_);
    }

    const std::string path_;
    DriverModelLookup lookup_;
    DriverInputCore core_;
    mac3dx::Api api_;
    std::atomic<uint16_t> client_{0};
    std::mutex devMu_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> threadDone_{true};
};

DriverModelLookup makeLookup(const std::string &descriptorDir) {
    if (descriptorDir.empty()) return nullptr;
    std::vector<HidDescriptor> descs;
    std::vector<std::string> errors;
    if (!loadHidDescriptorDir(descriptorDir, descs, errors) || descs.empty()) return nullptr;
    auto core = std::make_shared<HidInputCore>(std::move(descs));
    return [core](unsigned vid, unsigned pid, std::string *name, int *buttons) {
        const int i = core->match(vid, pid);
        if (i < 0) return false;
        const HidDescriptor &d = core->descriptors()[static_cast<size_t>(i)];
        *name = d.name;
        *buttons = d.factoryButtons > 0 ? d.factoryButtons : static_cast<int>(d.buttonBits);
        return true;
    };
}

}  // namespace

bool macDriverInstalled(std::string *why) {
    std::string dummy;
    if (!why) why = &dummy;
    char resolved[PATH_MAX];
    if (!::realpath(kMacClientLibrary, resolved)) {
        *why = "the 3Dconnexion client framework is not installed";
        return false;
    }
    const std::string r = resolved;
    if (!macLibraryPathAllowed(r)) {
        *why = "the client library resolves outside its framework directory";
        return false;
    }
    struct stat st;
    if (::stat(r.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        *why = "the client library is not a regular file";
        return false;
    }
    std::string reason;
    if (!macLibraryOwnerOk(static_cast<uint32_t>(st.st_uid), static_cast<uint32_t>(st.st_mode), &reason)) {
        *why = "the client library is " + reason + ", not loading it";
        return false;
    }
    return true;
}

std::unique_ptr<InputSource> makeMacDriverInput(const std::string &descriptorDir, std::string *log) {
    std::string why;
    if (!macDriverInstalled(&why)) {
        if (log) *log = why;
        return nullptr;
    }
    return std::make_unique<MacDriverInput>(kMacClientLibrary, makeLookup(descriptorDir));
}

std::unique_ptr<InputSource> makeMacDriverInputFromPath(const std::string &libraryPath, DriverModelLookup lookup) {
    return std::make_unique<MacDriverInput>(libraryPath, std::move(lookup));
}

}  // namespace spn
