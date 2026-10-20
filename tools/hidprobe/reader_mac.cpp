// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// macOS backend: IOHIDManager (IOKit C API). Like the daemon (src/platform/mac/mac_hid_input.cpp) it
// opens with kIOHIDOptionsTypeNone and never seizes the device, so "hidprobe can read while the
// 3Dconnexion driver runs" answers the same question for dofscroll. Never writes to the device.
// Reading needs the Input Monitoring permission for the program that runs hidprobe (Terminal).
#ifndef __APPLE__
#error "reader_mac.cpp is built on macOS only"
#endif
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <dlfcn.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include "hidprobe_backend.hpp"
#include "platform/mac/mac_hid_ids.hpp"

namespace hidprobe {
namespace {

bool cfInt(CFTypeRef t, int64_t *out) {
    if (!t || CFGetTypeID(t) != CFNumberGetTypeID()) return false;
    return CFNumberGetValue(static_cast<CFNumberRef>(t), kCFNumberSInt64Type, out);
}

std::string cfString(CFTypeRef t) {
    if (!t || CFGetTypeID(t) != CFStringGetTypeID()) return "";
    const CFStringRef s = static_cast<CFStringRef>(t);
    const CFIndex max = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
    if (max <= 1 || max > 65536) return "";
    std::string buf(static_cast<size_t>(max), '\0');
    if (!CFStringGetCString(s, &buf[0], max, kCFStringEncodingUTF8)) return "";
    buf.resize(std::strlen(buf.c_str()));
    return buf;
}

int64_t prop(IOHIDDeviceRef d, CFStringRef key) {
    int64_t v = 0;
    (void)cfInt(IOHIDDeviceGetProperty(d, key), &v);
    return v;
}

bool isSpaceMouse(unsigned vid, unsigned pid, unsigned usagePage, unsigned usage) {
    if (vid == 0x256f) return true;
    if (usagePage == 0x01 && usage == 0x08) return true;
    return vid == 0x046d && pid >= 0xc603 && pid <= 0xc63f;
}

// "iokit:<location id>:<vid>:<pid>:<n>": the location id tells devices apart, n the interfaces of one.
std::string pathOf(IOHIDDeviceRef d, unsigned vid, unsigned pid, int n) {
    char b[96];
    std::snprintf(b, sizeof b, "iokit:%08llx:%04x:%04x:%d",
                  static_cast<unsigned long long>(prop(d, CFSTR(kIOHIDLocationIDKey))), vid, pid, n);
    return b;
}

// The manager is opened like the daemon does (kIOHIDOptionsTypeNone). Its open result is kept, so
// that "exclusive access" shows up in open().
class Manager {
public:
    Manager() {
        mgr_ = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
        if (!mgr_) return;
        IOHIDManagerSetDeviceMatching(mgr_, nullptr);  // all HID devices, filtered in list()
        ret_ = IOHIDManagerOpen(mgr_, kIOHIDOptionsTypeNone);
    }
    ~Manager() {
        if (mgr_) {
            if (ret_ == kIOReturnSuccess) IOHIDManagerClose(mgr_, kIOHIDOptionsTypeNone);
            CFRelease(mgr_);
        }
    }
    Manager(const Manager &) = delete;
    Manager &operator=(const Manager &) = delete;
    IOReturn result() const { return mgr_ ? ret_ : kIOReturnNoMemory; }
    IOHIDManagerRef ref() const { return mgr_; }

    // Visits the SpaceMice with their path.
    template <class F>
    void each(F f) const {
        if (!mgr_) return;
        CFSetRef set = IOHIDManagerCopyDevices(mgr_);
        if (!set) return;
        const CFIndex n = CFSetGetCount(set);
        std::vector<const void *> devs(static_cast<size_t>(n));
        if (n > 0) CFSetGetValues(set, devs.data());
        int idx = 0;
        for (const void *p : devs) {
            IOHIDDeviceRef d = static_cast<IOHIDDeviceRef>(const_cast<void *>(p));
            const unsigned vid = static_cast<unsigned>(prop(d, CFSTR(kIOHIDVendorIDKey)));
            const unsigned pid = static_cast<unsigned>(prop(d, CFSTR(kIOHIDProductIDKey)));
            const unsigned up = static_cast<unsigned>(prop(d, CFSTR(kIOHIDPrimaryUsagePageKey)));
            const unsigned us = static_cast<unsigned>(prop(d, CFSTR(kIOHIDPrimaryUsageKey)));
            if (!isSpaceMouse(vid, pid, up, us)) continue;
            f(d, vid, pid, pathOf(d, vid, pid, idx++));
        }
        CFRelease(set);
    }

private:
    IOHIDManagerRef mgr_ = nullptr;
    IOReturn ret_ = kIOReturnError;
};

class MacStream : public ReportStream {
public:
    // Takes over the manager (keeps the device list alive) and the opened device.
    MacStream(std::unique_ptr<Manager> m, IOHIDDeviceRef dev, size_t bufLen)
        : mgr_(std::move(m)), dev_(dev), buf_(std::max<size_t>(64, bufLen + 1), 0) {
        IOHIDDeviceRegisterInputReportCallback(dev_, buf_.data(), static_cast<CFIndex>(buf_.size()),
                                               &MacStream::onReport, this);
        IOHIDDeviceScheduleWithRunLoop(dev_, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    }
    ~MacStream() override {
        IOHIDDeviceRegisterInputReportCallback(dev_, buf_.data(), static_cast<CFIndex>(buf_.size()), nullptr, nullptr);
        IOHIDDeviceUnscheduleFromRunLoop(dev_, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
        IOHIDDeviceClose(dev_, kIOHIDOptionsTypeNone);
    }
    MacStream(const MacStream &) = delete;
    MacStream &operator=(const MacStream &) = delete;

    ReadResult read(int timeoutMs, std::vector<uint8_t> &out) override {
        if (q_.empty()) CFRunLoopRunInMode(kCFRunLoopDefaultMode, (timeoutMs < 0 ? 0 : timeoutMs) / 1000.0, true);
        if (q_.empty()) return ReadResult::Timeout;
        out = std::move(q_.front());
        q_.pop_front();
        return ReadResult::Report;
    }

private:
    static void onReport(void *ctx, IOReturn, void *, IOHIDReportType, uint32_t reportId, uint8_t *report,
                         CFIndex length) {
        MacStream *s = static_cast<MacStream *>(ctx);
        if (length <= 0 || s->q_.size() > 4096) return;
        std::vector<uint8_t> raw;
        spn::macRawReport(reportId, report, static_cast<size_t>(length), false, &raw);
        s->q_.push_back(std::move(raw));
    }
    std::unique_ptr<Manager> mgr_;
    IOHIDDeviceRef dev_;
    std::vector<uint8_t> buf_;
    std::deque<std::vector<uint8_t>> q_;
};

class MacBackend : public HidBackend {
public:
    std::vector<DeviceInfo> list() override {
        std::vector<DeviceInfo> v;
        Manager m;
        m.each([&](IOHIDDeviceRef d, unsigned vid, unsigned pid, const std::string &path) {
            DeviceInfo di;
            di.path = path;
            di.vid = vid;
            di.pid = pid;
            di.name = cfString(IOHIDDeviceGetProperty(d, CFSTR(kIOHIDProductKey)));
            di.serial = cfString(IOHIDDeviceGetProperty(d, CFSTR(kIOHIDSerialNumberKey)));
            v.push_back(di);
        });
        std::sort(v.begin(), v.end(), [](const DeviceInfo &a, const DeviceInfo &b) { return a.path < b.path; });
        return v;
    }

    bool readDescriptor(const DeviceInfo &dev, std::vector<uint8_t> &out, std::string &err) override {
        Manager m;
        bool found = false, ok = false;
        m.each([&](IOHIDDeviceRef d, unsigned, unsigned, const std::string &path) {
            if (found || path != dev.path) return;
            found = true;
            CFTypeRef t = IOHIDDeviceGetProperty(d, CFSTR(kIOHIDReportDescriptorKey));
            if (t && CFGetTypeID(t) == CFDataGetTypeID()) {
                CFDataRef data = static_cast<CFDataRef>(t);
                const UInt8 *p = CFDataGetBytePtr(data);
                out.assign(p, p + CFDataGetLength(data));
                ok = true;
            }
        });
        if (!found) err = "device not found";
        else if (!ok) err = "the report descriptor is not exposed by IOKit for this device";
        return ok;
    }

    std::unique_ptr<ReportStream> open(const DeviceInfo &dev, std::string &err, bool &permissionDenied) override {
        permissionDenied = false;
        std::unique_ptr<Manager> m(new Manager);
        if (m->result() != kIOReturnSuccess) {
            err = std::string("IOHIDManagerOpen: ") + spn::macOpenFailureMessage(static_cast<int32_t>(m->result())) +
                  " (IOReturn 0x" + hex(static_cast<unsigned>(m->result())) + ")";
            permissionDenied = true;
            return nullptr;
        }
        IOHIDDeviceRef target = nullptr;
        m->each([&](IOHIDDeviceRef d, unsigned, unsigned, const std::string &path) {
            if (!target && path == dev.path) target = d;
        });
        if (!target) {
            err = "device not found (unplugged?)";
            return nullptr;
        }
        const IOReturn r = IOHIDDeviceOpen(target, kIOHIDOptionsTypeNone);  // never kIOHIDOptionsTypeSeizeDevice
        if (r != kIOReturnSuccess) {
            err = std::string("IOHIDDeviceOpen: ") + spn::macOpenFailureMessage(static_cast<int32_t>(r)) +
                  " (IOReturn 0x" + hex(static_cast<unsigned>(r)) + ")";
            permissionDenied = true;
            return nullptr;
        }
        const size_t maxIn = static_cast<size_t>(prop(target, CFSTR(kIOHIDMaxInputReportSizeKey)));
        return std::unique_ptr<ReportStream>(new MacStream(std::move(m), target, maxIn));
    }

    std::string permissionHelp(const DeviceInfo &) const override {
        std::string s;
        using CheckFn = uint32_t (*)(uint32_t);
        const CheckFn check = reinterpret_cast<CheckFn>(dlsym(RTLD_DEFAULT, "IOHIDCheckAccess"));
        if (check) {
            const spn::MacAccess a = spn::macAccessFromInt(static_cast<int>(check(1)));  // 1 = listen event
            s = spn::macPermissionMessage(a) + " (for the program that runs hidprobe, usually Terminal)\n";
        }
        return s +
               "If Input Monitoring is granted, the device may be held by another program, possibly the "
               "3Dconnexion driver. This is a finding, not something you did wrong: please report it with "
               "the file written by scripts/test-driver-coexistence.sh (see docs/troubleshooting.md, \"The "
               "official 3Dconnexion driver and dofscroll\"). hidprobe never changes anything on the device "
               "or in the driver.";
    }

private:
    static std::string hex(unsigned v) {
        char b[16];
        std::snprintf(b, sizeof b, "%08X", v);
        return b;
    }
};

}  // namespace

std::unique_ptr<HidBackend> makeBackend() { return std::unique_ptr<HidBackend>(new MacBackend); }

}  // namespace hidprobe
