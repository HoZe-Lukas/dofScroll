// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows raw-HID InputSource: enumerates HID interfaces with SetupAPI, reads input reports with
// overlapped ReadFile (one reader thread per physical device) and converts them with the shared
// HidInputCore. No vendor SDK.
//
// Link: setupapi hid (and the libraries of the core: advapi32 bcrypt).
//
// Design notes
//  - A manager thread rescans the HID class every kRescanMs (hot-plug, sleep/wake: a device that
//    produced a read error is dropped and reopened by the next rescan). RegisterDeviceNotification
//    on a message-only window would deliver arrival/removal without polling; the rescan is simpler
//    and needs no window, at the price of up to kRescanMs latency.
//  - Only interfaces whose vid/pid appears in a loaded devices/*.conf are opened. Interfaces of one
//    physical device are grouped by its container id; one is used (win_hid_path.hpp:
//    pickInterfaces), so there is exactly one DeviceInfo per physical device.
//  - Devices are opened GENERIC_READ with FILE_SHARE_READ|FILE_SHARE_WRITE, never exclusively.
//    Whether the vendor driver's own handle lets a second reader receive reports, or whether the
//    driver opens the device exclusively (then CreateFile fails with a sharing violation), is
//    unverified; the failure is logged and retried at the next rescan.
#ifndef _WIN32
#error "win_hid_input.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// clang-format off
#include <setupapi.h>
#include <initguid.h>
extern "C" {  // some header sets (MinGW-w64) lack the C linkage guards of the HID headers
#include <hidsdi.h>
#include <hidpi.h>
}
// clang-format on

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../hid_descriptor.hpp"
#include "../../log_sanitize.hpp"
#include "../event_queue.hpp"
#include "../hid_input_core.hpp"
#include "win_hid_input.hpp"
#include "win_hid_path.hpp"

namespace spn {

namespace {

constexpr int kRescanMs = 2000;
constexpr unsigned kEmptyReadsFree = 8;  // empty completions tolerated before the reader backs off
const char *const kLog = "[dofscroll] ";

// DEVPKEY_Device_ContainerId {8c7ed206-3f8a-4827-b3ab-ae9e1faefc6c}, 2 (defined here so no
// devpkey.h / INITGUID juggling is needed).
const DEVPROPKEY kContainerIdKey = {{0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}}, 2};

std::string narrow(const wchar_t *w) {
    if (!w || !*w) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return std::string();
    std::string s(static_cast<size_t>(n - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring widen(const std::string &s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string guidString(const GUID &g) {
    char b[64];
    std::snprintf(b, sizeof b, "{%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", static_cast<unsigned long>(g.Data1),
                  g.Data2, g.Data3, g.Data4[0], g.Data4[1], g.Data4[2], g.Data4[3], g.Data4[4], g.Data4[5], g.Data4[6],
                  g.Data4[7]);
    return b;
}

// One HID interface found by a scan.
struct Found {
    std::string path;       // UTF-8 device interface path
    std::string container;  // container id string, "" if unavailable
};

// Lists the HID interfaces whose path has a vid/pid known to `core`.
std::vector<Found> enumerate(const HidInputCore &core) {
    std::vector<Found> out;
    GUID hidGuid;
    ::HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = ::SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return out;
    for (DWORD idx = 0;; ++idx) {
        SP_DEVICE_INTERFACE_DATA ifd;
        ifd.cbSize = sizeof ifd;
        if (!::SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, idx, &ifd)) break;  // ERROR_NO_MORE_ITEMS
        DWORD need = 0;
        ::SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
        if (need < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || need > 8192) continue;
        std::vector<unsigned char> buf(need);
        auto *det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
        det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        SP_DEVINFO_DATA devInfo;
        devInfo.cbSize = sizeof devInfo;
        if (!::SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, &devInfo)) continue;
        Found f;
        f.path = narrow(det->DevicePath);
        unsigned vid = 0, pid = 0;
        if (!parseHidPath(f.path, &vid, &pid) || !core.known(vid, pid)) continue;
        DEVPROPTYPE type = 0;
        GUID cid;
        DWORD sz = 0;
        if (::SetupDiGetDevicePropertyW(set, &devInfo, &kContainerIdKey, &type, reinterpret_cast<PBYTE>(&cid), sizeof cid,
                                        &sz, 0) &&
            type == DEVPROP_TYPE_GUID && sz == sizeof cid)
            f.container = guidString(cid);
        out.push_back(f);
    }
    ::SetupDiDestroyDeviceInfoList(set);
    return out;
}

// An opened candidate, before it is accepted.
struct Probe {
    HidCandidate cand;
    HANDLE h = INVALID_HANDLE_VALUE;
    USHORT inLen = 0;
    unsigned vid = 0, pid = 0;
    std::string product;
};

HANDLE openPath(const std::string &path) {
    return ::CreateFileW(widen(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                         FILE_FLAG_OVERLAPPED, nullptr);
}

// Opens `f`, reads attributes and capabilities. h stays INVALID_HANDLE_VALUE on failure.
Probe probe(const Found &f, std::string *why) {
    Probe p;
    p.cand.path = f.path;
    p.cand.container = f.container;
    HANDLE h = openPath(f.path);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = ::GetLastError();
        *why = (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED)
                   ? "cannot open (in use by another program or driver; error " + std::to_string(e) + ")"
                   : "cannot open (error " + std::to_string(e) + ")";
        return p;
    }
    HIDD_ATTRIBUTES at;
    at.Size = sizeof at;
    PHIDP_PREPARSED_DATA pre = nullptr;
    HIDP_CAPS caps;
    if (!::HidD_GetAttributes(h, &at) || !::HidD_GetPreparsedData(h, &pre)) {
        *why = "not a usable HID device";
        ::CloseHandle(h);
        return p;
    }
    const bool capsOk = ::HidP_GetCaps(pre, &caps) == HIDP_STATUS_SUCCESS;
    ::HidD_FreePreparsedData(pre);
    if (!capsOk || caps.InputReportByteLength == 0) {
        *why = "no input reports";
        ::CloseHandle(h);
        return p;
    }
    p.h = h;
    p.inLen = caps.InputReportByteLength;
    p.cand.usagePage = caps.UsagePage;
    p.cand.usage = caps.Usage;
    p.vid = at.VendorID;
    p.pid = at.ProductID;
    wchar_t name[128] = L"";
    if (::HidD_GetProductString(h, name, sizeof name - sizeof(wchar_t))) p.product = narrow(name);
    return p;
}

// HidD_* calls (attributes, preparsed data, strings) can block for a long time on a misbehaving
// device or driver, and scan() runs on the manager thread that close() joins. So the probe runs on
// a helper thread; the manager waits for it in short slices, up to kProbeTimeoutMs and only while
// the source is running. A probe that does not finish is abandoned: CancelSynchronousIo is
// requested, the helper thread is detached and closes the handle itself once it returns.
constexpr int kProbeTimeoutMs = 3000;
constexpr int kProbeSliceMs = 50;

struct ProbeJob {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    bool abandoned = false;
    Probe result;
    std::string why;
};

Probe probeBounded(const Found &f, const std::atomic<bool> &running, std::string *why) {
    auto job = std::make_shared<ProbeJob>();
    std::thread th([job, f] {
        std::string w;
        Probe p = probe(f, &w);
        std::lock_guard<std::mutex> lk(job->mu);
        if (job->abandoned) {
            if (p.h != INVALID_HANDLE_VALUE) ::CloseHandle(p.h);
            return;
        }
        job->result = std::move(p);
        job->why = std::move(w);
        job->done = true;
        job->cv.notify_all();
    });
    HANDLE native = th.native_handle();
    std::unique_lock<std::mutex> lk(job->mu);
    int waited = 0;
    while (!job->done && running && waited < kProbeTimeoutMs) {
        job->cv.wait_for(lk, std::chrono::milliseconds(kProbeSliceMs));
        waited += kProbeSliceMs;
    }
    if (!job->done) {
        job->abandoned = true;
        ::CancelSynchronousIo(native);  // best effort; the thread is detached either way
        lk.unlock();
        th.detach();
        *why = running ? "probe timed out (device or driver does not answer)" : "stopped";
        return Probe();
    }
    Probe out = std::move(job->result);
    *why = job->why;
    lk.unlock();
    th.join();
    return out;
}

// One accepted device with its reader thread.
struct OpenDev {
    std::string path;
    std::string container;
    HANDLE h = INVALID_HANDLE_VALUE;
    HANDLE stopEvt = nullptr;  // manual-reset, set to stop the reader
    USHORT inLen = 0;
    int id = 0;
    std::thread th;
    std::atomic<bool> done{false};  // reader exited (device gone or read error)
};

class WinHidInputSource : public InputSource {
public:
    explicit WinHidInputSource(std::string dir) : dir_(std::move(dir)) {}
    ~WinHidInputSource() override { closeImpl(); }

    const char *name() const override { return "hid"; }

    bool open(std::string *err) override {
        if (running_) return true;
        std::vector<HidDescriptor> descs;
        std::vector<std::string> errors;
        if (!loadHidDescriptorDir(dir_, descs, errors)) {
            if (err) *err = "cannot read the device descriptions in " + dir_;
            return false;
        }
        for (const std::string &e : errors)
            std::cerr << kLog << "device description " << sanitizeForLog(e) << " ignored" << std::endl;
        if (descs.empty()) {
            if (err) *err = "no device descriptions found in " + dir_;
            return false;
        }
        core_ = std::make_unique<HidInputCore>(std::move(descs));
        queue_.reopen();
        stopMgr_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopMgr_) {
            if (err) *err = "CreateEvent failed";
            return false;
        }
        running_ = true;
        scan();  // devices that are already attached are known when open() returns
        mgr_ = std::thread([this] { manage(); });
        return true;
    }

    WaitResult wait(InputEvent *out, int timeoutMs) override { return queue_.wait(out, timeoutMs); }

    std::vector<DeviceInfo> devices() const override {
        return core_ ? core_->devices() : std::vector<DeviceInfo>();
    }

    void close() override { closeImpl(); }

private:
    // Non-virtual, so the destructor does not dispatch virtually.
    void closeImpl() {
        if (!running_) return;
        running_ = false;
        ::SetEvent(stopMgr_);
        if (mgr_.joinable()) mgr_.join();
        {
            std::lock_guard<std::mutex> lk(devMu_);
            for (auto &d : devs_) stopDevice(*d, /*reset=*/true);
            devs_.clear();
        }
        ::CloseHandle(stopMgr_);
        stopMgr_ = nullptr;
        queue_.close();
    }

    void manage() {
        while (::WaitForSingleObject(stopMgr_, kRescanMs) == WAIT_TIMEOUT) scan();
    }

    // Stops a device's reader and closes its handle. reset: neutralize its state first (buttons up,
    // motion zero) so nothing stays held; then it is removed from the core.
    void stopDevice(OpenDev &d, bool reset) {
        ::SetEvent(d.stopEvt);
        if (d.th.joinable()) d.th.join();
        std::vector<InputEvent> ev;
        if (reset) core_->reset(d.id, &ev);
        core_->removeDevice(d.id, &ev);
        queue_.push(ev);
        if (d.h != INVALID_HANDLE_VALUE) ::CloseHandle(d.h);
        if (d.stopEvt) ::CloseHandle(d.stopEvt);
        d.h = INVALID_HANDLE_VALUE;
        d.stopEvt = nullptr;
    }

    // One hot-plug pass: drop finished/removed devices, open new ones.
    void scan() {
        std::lock_guard<std::mutex> lk(devMu_);
        const std::vector<Found> found = enumerate(*core_);

        // 1. devices whose reader stopped, or whose interface vanished
        std::vector<std::string> presentPaths;
        for (const Found &f : found) presentPaths.push_back(f.path);
        std::vector<std::string> openPaths;
        for (auto &d : devs_) openPaths.push_back(d->path);
        const PathDiff gone = diffPaths(openPaths, presentPaths);
        for (size_t i = 0; i < devs_.size();) {
            OpenDev &d = *devs_[i];
            const bool vanished = std::any_of(gone.removed.begin(), gone.removed.end(),
                                              [&](const std::string &p) { return hidPathKey(p) == hidPathKey(d.path); });
            if (d.done.load() || vanished) {
                std::cerr << kLog << "HID device removed or lost (" << sanitizeForLog(d.path, 96) << ")" << std::endl;
                stopDevice(d, /*reset=*/true);
                devs_.erase(devs_.begin() + static_cast<long>(i));
            } else {
                ++i;
            }
        }
        // forget rejected interfaces that are gone, so a re-plug is inspected again
        for (size_t i = 0; i < rejected_.size();) {
            const bool present = std::any_of(presentPaths.begin(), presentPaths.end(), [&](const std::string &p) {
                return hidPathKey(p) == hidPathKey(rejected_[i]);
            });
            if (present) ++i;
            else rejected_.erase(rejected_.begin() + static_cast<long>(i));
        }

        // 2. new interfaces: not open and not rejected before
        std::vector<std::string> known = rejected_;
        for (auto &d : devs_) known.push_back(d->path);
        const PathDiff nw = diffPaths(known, presentPaths);
        if (nw.added.empty()) return;

        std::vector<Probe> probes;
        for (const std::string &path : nw.added) {
            const auto it = std::find_if(found.begin(), found.end(), [&](const Found &f) { return f.path == path; });
            if (it == found.end()) continue;
            std::string why;
            Probe p = probeBounded(*it, running_, &why);
            if (p.h == INVALID_HANDLE_VALUE) {
                // sharing violations may be temporary (driver start-up): retry at the next rescan
                std::cerr << kLog << "HID interface " << sanitizeForLog(path, 96) << ": " << why << std::endl;
                continue;
            }
            probes.push_back(std::move(p));
        }
        std::vector<HidCandidate> cands;
        for (const Probe &p : probes) cands.push_back(p.cand);
        std::vector<std::string> busy;
        for (auto &d : devs_)
            if (!d->container.empty()) busy.push_back(d->container);
        const std::vector<size_t> pick = pickInterfaces(cands, busy);

        for (size_t i = 0; i < probes.size(); ++i) {
            Probe &p = probes[i];
            if (std::find(pick.begin(), pick.end(), i) == pick.end()) {
                rejected_.push_back(p.cand.path);
                ::CloseHandle(p.h);
                continue;
            }
            std::vector<InputEvent> ev;
            const int id = core_->addDevice(p.vid, p.pid, p.product, &ev);
            if (id == 0) {  // the attributes disagree with the path: not ours
                rejected_.push_back(p.cand.path);
                ::CloseHandle(p.h);
                continue;
            }
            auto d = std::make_unique<OpenDev>();
            d->path = p.cand.path;
            d->container = p.cand.container;
            d->h = p.h;
            d->inLen = p.inLen;
            d->id = id;
            d->stopEvt = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!d->stopEvt) {
                ::CloseHandle(p.h);
                core_->removeDevice(id, nullptr);
                continue;
            }
            queue_.push(ev);
            OpenDev *raw = d.get();
            d->th = std::thread([this, raw] { readLoop(*raw); });
            for (const DeviceInfo &di : core_->devices())
                if (di.id == id)
                    std::cerr << kLog << "HID device attached: " << sanitizeForLog(di.name) << " (" << std::hex << di.vendor
                              << ":" << di.product << std::dec << ")" << std::endl;
            devs_.push_back(std::move(d));
        }
    }

    // Reader thread of one device: overlapped reads until stopped or the device fails.
    void readLoop(OpenDev &d) {
        std::vector<uint8_t> buf(d.inLen);
        OVERLAPPED ov{};
        ov.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!ov.hEvent) {
            d.done = true;
            return;
        }
        std::vector<InputEvent> ev;
        unsigned emptyReads = 0;  // consecutive completed reads with 0 bytes
        for (;;) {
            ::ResetEvent(ov.hEvent);
            DWORD n = 0;
            const BOOL ok = ::ReadFile(d.h, buf.data(), static_cast<DWORD>(buf.size()), &n, &ov);
            if (!ok) {
                if (::GetLastError() != ERROR_IO_PENDING) break;  // device removed, sleep/wake, ...
                HANDLE hs[2] = {ov.hEvent, d.stopEvt};
                const DWORD w = ::WaitForMultipleObjects(2, hs, FALSE, INFINITE);
                if (w != WAIT_OBJECT_0) {  // stop requested (or wait failure)
                    ::CancelIoEx(d.h, &ov);
                    ::GetOverlappedResult(d.h, &ov, &n, TRUE);  // the buffer must stay valid until here
                    break;
                }
                if (!::GetOverlappedResult(d.h, &ov, &n, FALSE)) break;
            }
            if (n == 0) {
                // A read that completes at once with no data would spin this thread at 100% CPU
                // (misbehaving driver): back off, 5 ms doubling up to 500 ms, interruptible by stop.
                if (++emptyReads > kEmptyReadsFree) {
                    const unsigned shift = std::min(emptyReads - kEmptyReadsFree - 1, 7u);
                    const DWORD delay = std::min<DWORD>(500, 5u << shift);
                    if (::WaitForSingleObject(d.stopEvt, delay) == WAIT_OBJECT_0) break;
                }
            } else {
                emptyReads = 0;
            }
            if (n > 0) {
                ev.clear();
                core_->onReport(d.id, buf.data(), n, &ev);
                if (!ev.empty()) queue_.push(ev);
            }
            if (::WaitForSingleObject(d.stopEvt, 0) == WAIT_OBJECT_0) break;
        }
        ::CloseHandle(ov.hEvent);
        d.done = true;
    }

    std::string dir_;
    std::unique_ptr<HidInputCore> core_;
    EventQueue queue_;
    std::mutex devMu_;  // devs_, rejected_
    std::vector<std::unique_ptr<OpenDev>> devs_;
    std::vector<std::string> rejected_;
    std::thread mgr_;
    HANDLE stopMgr_ = nullptr;
    std::atomic<bool> running_{false};
};

}  // namespace

std::unique_ptr<InputSource> makeWinHidInput(const std::string &descriptorDir, std::string *log) {
    if (log) *log = "raw HID (SetupAPI + hid.dll), descriptions from " + descriptorDir;
    return std::make_unique<WinHidInputSource>(descriptorDir);
}

}  // namespace spn
