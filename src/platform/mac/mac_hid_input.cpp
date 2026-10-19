// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS raw-HID InputSource on top of IOHIDManager (IOKit C API, no vendor SDK). Link:
// -framework IOKit -framework CoreFoundation (cmake/macos.cmake).
//
// One reader thread owns the CFRunLoop on which the IOHIDManager is scheduled; device matching and
// removal callbacks (hot-plug) and input-report callbacks all run there. Raw reports are decoded
// by the shared HidInputCore (src/platform/hid_input_core.hpp) and handed to InputSource::wait()
// through the shared EventQueue. The pure parts (matching pairs, report reassembly, messages) live in
// mac_hid_ids.hpp and are unit-tested.
//
// UNVERIFIED without hardware: that a device opened with kIOHIDOptionsTypeNone (not seized) delivers
// reports while the 3Dconnexion driver is running, whether the report buffer carries the report id
// (both cases are handled, see macRawReport), sleep/wake behavior, and the exact permission prompts.
#include "mac_hid_input.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "../../hid_descriptor.hpp"
#include "../../log_sanitize.hpp"
#include "../event_queue.hpp"
#include "../hid_input_core.hpp"
#include "mac_hid_ids.hpp"

namespace spn {
namespace {

const char *const LOG = "[dofscroll] ";

// Input Monitoring: IOHIDCheckAccess / IOHIDRequestAccess (macOS 10.15+) are looked up with dlsym. The
// runner's SDK does not expose the declarations through IOHIDLib.h in C++, and dlsym also keeps the binary
// loadable on older systems (no permission check there). IOHIDRequestType kIOHIDRequestTypeListenEvent = 1
// (PostEvent = 0); IOHIDAccessType granted = 0, denied = 1, unknown = 2.
constexpr uint32_t kListenEvent = 1;

bool cfInt(CFTypeRef t, int64_t *out) {
    if (!t || CFGetTypeID(t) != CFNumberGetTypeID()) return false;
    return CFNumberGetValue(static_cast<CFNumberRef>(t), kCFNumberSInt64Type, out);
}

std::string cfString(CFTypeRef t) {
    if (!t || CFGetTypeID(t) != CFStringGetTypeID()) return "";
    const CFStringRef s = static_cast<CFStringRef>(t);
    char buf[256];
    if (!CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8)) return "";
    return buf;
}

CFDictionaryRef makeMatch(unsigned vid, unsigned pid) {
    CFMutableDictionaryRef d = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
                                                         &kCFTypeDictionaryValueCallBacks);
    const int32_t v = static_cast<int32_t>(vid), p = static_cast<int32_t>(pid);
    CFNumberRef nv = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &v);
    CFNumberRef np = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &p);
    CFDictionarySetValue(d, CFSTR(kIOHIDVendorIDKey), nv);
    CFDictionarySetValue(d, CFSTR(kIOHIDProductIDKey), np);
    CFRelease(nv);
    CFRelease(np);
    return d;
}

class MacHidInput;

struct DevCtx {
    MacHidInput *self = nullptr;
    IOHIDDeviceRef dev = nullptr;
    int id = 0;
    std::vector<uint8_t> buf;
};

class MacHidInput : public InputSource {
public:
    explicit MacHidInput(std::vector<HidDescriptor> descs)
        : descs_(std::move(descs)), core_(descs_), prepend_(envFlag("DOFSCROLL_MAC_PREPEND_ID")) {}
    ~MacHidInput() override { shutdown(); }

    const char *name() const override { return "iokit"; }

    bool open(std::string *err) override {
        if (running_) return true;
        queue_.reopen();
        checkPermission();
        auto ready = std::make_shared<std::promise<std::string>>();
        std::future<std::string> fut = ready->get_future();
        stop_ = false;
        thread_ = std::thread([this, ready] { run(ready); });
        if (fut.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
            if (err) *err = "timeout while opening the IOHIDManager";
            stop_ = true;
            if (thread_.joinable()) thread_.join();
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

    WaitResult wait(InputEvent *out, int timeoutMs) override { return queue_.wait(out, timeoutMs); }

    std::vector<DeviceInfo> devices() const override { return core_.devices(); }

    void close() override { shutdown(); }

private:
    void shutdown() {
        running_ = false;
        stop_ = true;
        CFRunLoopRef rl = loop_.load();
        if (rl) CFRunLoopStop(rl);
        if (thread_.joinable()) thread_.join();
        queue_.close();
    }

    static bool envFlag(const char *n) {
        const char *v = std::getenv(n);
        return v && *v == '1';
    }

    void checkPermission() {
        if (permissionChecked_) return;
        permissionChecked_ = true;
        using CheckFn = uint32_t (*)(uint32_t);
        using RequestFn = bool (*)(uint32_t);
        const CheckFn check = reinterpret_cast<CheckFn>(dlsym(RTLD_DEFAULT, "IOHIDCheckAccess"));
        if (!check) return;  // macOS before 10.15: no Input Monitoring permission
        const MacAccess a = macAccessFromInt(static_cast<int>(check(kListenEvent)));
        if (a == MacAccess::Granted) return;
        std::cerr << LOG << "WARNING: " << macPermissionMessage(a) << std::endl;
        if (a == MacAccess::Unknown) {
            const RequestFn request = reinterpret_cast<RequestFn>(dlsym(RTLD_DEFAULT, "IOHIDRequestAccess"));
            if (request) (void)request(kListenEvent);
        }
    }

    void run(const std::shared_ptr<std::promise<std::string>> &ready) {
        IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
        if (!mgr) {
            ready->set_value("IOHIDManagerCreate failed");
            return;
        }
        CFMutableArrayRef arr = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
        for (const auto &p : macMatchingPairs(descs_)) {
            CFDictionaryRef d = makeMatch(p.first, p.second);
            CFArrayAppendValue(arr, d);
            CFRelease(d);
        }
        IOHIDManagerSetDeviceMatchingMultiple(mgr, arr);
        CFRelease(arr);
        IOHIDManagerRegisterDeviceMatchingCallback(mgr, &MacHidInput::onMatch, this);
        IOHIDManagerRegisterDeviceRemovalCallback(mgr, &MacHidInput::onRemove, this);
        CFRunLoopRef rl = CFRunLoopGetCurrent();
        IOHIDManagerScheduleWithRunLoop(mgr, rl, kCFRunLoopDefaultMode);
        const IOReturn r = IOHIDManagerOpen(mgr, kIOHIDOptionsTypeNone);  // never kIOHIDOptionsTypeSeizeDevice
        if (r != kIOReturnSuccess) {
            IOHIDManagerUnscheduleFromRunLoop(mgr, rl, kCFRunLoopDefaultMode);
            CFRelease(mgr);
            ready->set_value(macOpenFailureMessage(static_cast<int32_t>(r)));
            return;
        }
        CFRetain(rl);
        loop_ = rl;
        ready->set_value("");

        // Matching callbacks for already attached devices fire on the first run-loop passes.
        while (!stop_) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, false);

        // Teardown on the run-loop thread: neutral state first, then release everything.
        std::vector<InputEvent> ev;
        for (auto &kv : devs_) {
            IOHIDDeviceRegisterInputReportCallback(kv.second->dev, kv.second->buf.data(),
                                                   static_cast<CFIndex>(kv.second->buf.size()), nullptr, nullptr);
            core_.reset(kv.second->id, &ev);
            core_.removeDevice(kv.second->id, &ev);
        }
        devs_.clear();
        queue_.push(ev);
        IOHIDManagerUnscheduleFromRunLoop(mgr, rl, kCFRunLoopDefaultMode);
        IOHIDManagerClose(mgr, kIOHIDOptionsTypeNone);
        CFRelease(mgr);
        loop_ = nullptr;
        CFRelease(rl);
    }

    static void onMatch(void *ctx, IOReturn, void *, IOHIDDeviceRef dev) {
        static_cast<MacHidInput *>(ctx)->deviceAdded(dev);
    }
    static void onRemove(void *ctx, IOReturn, void *, IOHIDDeviceRef dev) {
        static_cast<MacHidInput *>(ctx)->deviceRemoved(dev);
    }
    static void onReport(void *ctx, IOReturn, void *, IOHIDReportType, uint32_t reportId, uint8_t *report,
                         CFIndex length) {
        DevCtx *d = static_cast<DevCtx *>(ctx);
        d->self->handleReport(d, reportId, report, length);
    }

    void deviceAdded(IOHIDDeviceRef dev) {
        int64_t vid = 0, pid = 0, maxIn = 0;
        if (!cfInt(IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDVendorIDKey)), &vid) ||
            !cfInt(IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDProductIDKey)), &pid))
            return;
        (void)cfInt(IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDMaxInputReportSizeKey)), &maxIn);
        const std::string product = cfString(IOHIDDeviceGetProperty(dev, CFSTR(kIOHIDProductKey)));
        std::vector<InputEvent> ev;
        const int id = core_.addDevice(static_cast<unsigned>(vid), static_cast<unsigned>(pid), product, &ev);
        if (id == 0) return;
        auto ctx = std::make_unique<DevCtx>();
        ctx->self = this;
        ctx->dev = dev;
        ctx->id = id;
        ctx->buf.assign(macReportBufferSize(static_cast<long>(maxIn)), 0);
        IOHIDDeviceRegisterInputReportCallback(dev, ctx->buf.data(), static_cast<CFIndex>(ctx->buf.size()),
                                               &MacHidInput::onReport, ctx.get());
        std::cerr << LOG << "HID device attached: " << sanitizeForLog(product) << " (" << std::hex << vid << ":" << pid
                  << std::dec << ")" << std::endl;
        devs_[dev] = std::move(ctx);
        queue_.push(ev);
    }

    void deviceRemoved(IOHIDDeviceRef dev) {
        auto it = devs_.find(dev);
        if (it == devs_.end()) return;
        std::vector<InputEvent> ev;
        core_.reset(it->second->id, &ev);
        core_.removeDevice(it->second->id, &ev);
        IOHIDDeviceRegisterInputReportCallback(dev, it->second->buf.data(),
                                               static_cast<CFIndex>(it->second->buf.size()), nullptr, nullptr);
        devs_.erase(it);
        queue_.push(ev);
    }

    void handleReport(DevCtx *d, uint32_t reportId, const uint8_t *data, CFIndex len) {
        if (len <= 0) return;
        std::vector<uint8_t> raw;
        macRawReport(reportId, data, static_cast<size_t>(len), prepend_, &raw);
        std::vector<InputEvent> ev;
        core_.onReport(d->id, raw.data(), raw.size(), &ev);
        if (!ev.empty()) queue_.push(ev);
    }

    std::vector<HidDescriptor> descs_;
    HidInputCore core_;
    EventQueue queue_;
    const bool prepend_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<CFRunLoopRef> loop_{nullptr};
    bool permissionChecked_ = false;
    std::map<IOHIDDeviceRef, std::unique_ptr<DevCtx>> devs_;  // run-loop thread only
};

}  // namespace

std::unique_ptr<InputSource> makeMacHidInput(const std::string &descriptorDir, std::string *log) {
    std::vector<HidDescriptor> descs;
    std::vector<std::string> errors;
    if (!loadHidDescriptorDir(descriptorDir, descs, errors)) {
        if (log) *log = "cannot read device descriptions in " + descriptorDir;
        return nullptr;
    }
    if (log) {
        *log = "iokit: " + std::to_string(descs.size()) + " device description(s) from " + descriptorDir;
        for (const std::string &e : errors) *log += "; " + sanitizeForLog(e);
    }
    if (descs.empty()) return nullptr;
    return std::make_unique<MacHidInput>(std::move(descs));
}

}  // namespace spn
