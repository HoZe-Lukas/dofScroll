// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows backend: HID interfaces through SetupAPI, reports through overlapped ReadFile. The device
// is opened exactly like the daemon opens it (win_hid_input.cpp: GENERIC_READ, FILE_SHARE_READ |
// FILE_SHARE_WRITE, never exclusive), so that "hidprobe can read while the 3Dconnexion driver runs"
// answers the same question for dofscroll. Never writes to the device. Windows does not expose the
// raw report descriptor, so readDescriptor() reports that it is unavailable.
#ifndef _WIN32
#error "reader_win.cpp is built on Windows only"
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
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}
// clang-format on

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "hidprobe_backend.hpp"
#include "hidprobe_win.hpp"

namespace hidprobe {
namespace {

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

std::string winErrorText(DWORD e) {
    char buf[256] = "";
    const DWORD n = ::FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0, buf,
                                     sizeof buf - 1, nullptr);
    std::string s(buf, n);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '.')) s.pop_back();
    return std::string("error ") + std::to_string(e) + (s.empty() ? "" : " (" + s + ")");
}

// A 3Dconnexion device: vendor 256f, the multi-axis controller usage (generic desktop 0x08), or the
// older Logitech-era ids. Other HID devices (keyboards, mice) are not listed.
bool isSpaceMouse(unsigned vid, unsigned pid, unsigned usagePage, unsigned usage) {
    if (vid == 0x256f) return true;
    if (usagePage == 0x01 && usage == 0x08) return true;
    return vid == 0x046d && pid >= 0xc603 && pid <= 0xc63f;
}

class WinStream : public ReportStream {
public:
    WinStream(HANDLE h, size_t len) : h_(h), buf_(len) {
        ov_ = OVERLAPPED();
        ov_.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    }
    ~WinStream() override {
        if (pending_) {
            ::CancelIoEx(h_, &ov_);
            DWORD n = 0;
            ::GetOverlappedResult(h_, &ov_, &n, TRUE);  // the buffer must stay valid until here
        }
        if (ov_.hEvent) ::CloseHandle(ov_.hEvent);
        ::CloseHandle(h_);
    }
    WinStream(const WinStream &) = delete;
    WinStream &operator=(const WinStream &) = delete;

    ReadResult read(int timeoutMs, std::vector<uint8_t> &out) override {
        if (!ov_.hEvent || buf_.empty()) return ReadResult::Error;
        if (!pending_) {
            ::ResetEvent(ov_.hEvent);
            DWORD n = 0;
            if (!::ReadFile(h_, buf_.data(), static_cast<DWORD>(buf_.size()), &n, &ov_)) {
                if (::GetLastError() != ERROR_IO_PENDING) return ReadResult::Closed;
                pending_ = true;
            } else {
                return finish(n, out);
            }
        }
        const DWORD w = ::WaitForSingleObject(ov_.hEvent, timeoutMs < 0 ? 0 : static_cast<DWORD>(timeoutMs));
        if (w == WAIT_TIMEOUT) return ReadResult::Timeout;
        if (w != WAIT_OBJECT_0) return ReadResult::Error;
        pending_ = false;
        DWORD n = 0;
        if (!::GetOverlappedResult(h_, &ov_, &n, FALSE)) return ReadResult::Closed;
        return finish(n, out);
    }

private:
    ReadResult finish(DWORD n, std::vector<uint8_t> &out) {
        if (n == 0) return ReadResult::Timeout;
        out.assign(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(n));
        return ReadResult::Report;
    }
    HANDLE h_;
    std::vector<uint8_t> buf_;
    OVERLAPPED ov_;
    bool pending_ = false;
};

class WinBackend : public HidBackend {
public:
    std::vector<DeviceInfo> list() override {
        std::vector<DeviceInfo> v;
        GUID hidGuid;
        ::HidD_GetHidGuid(&hidGuid);
        HDEVINFO set = ::SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE) return v;
        for (DWORD idx = 0;; ++idx) {
            SP_DEVICE_INTERFACE_DATA ifd;
            ifd.cbSize = sizeof ifd;
            if (!::SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, idx, &ifd)) break;
            DWORD need = 0;
            ::SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &need, nullptr);
            if (need < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || need > 8192) continue;
            std::vector<unsigned char> buf(need);
            auto *det = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buf.data());
            det->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!::SetupDiGetDeviceInterfaceDetailW(set, &ifd, det, need, nullptr, nullptr)) continue;
            DeviceInfo di;
            unsigned usagePage = 0, usage = 0;
            USHORT inLen = 0;
            if (!query(det->DevicePath, di, usagePage, usage, inLen)) continue;
            if (inLen == 0 || !isSpaceMouse(di.vid, di.pid, usagePage, usage)) continue;
            di.path = narrow(det->DevicePath);
            v.push_back(di);
        }
        ::SetupDiDestroyDeviceInfoList(set);
        std::sort(v.begin(), v.end(), [](const DeviceInfo &a, const DeviceInfo &b) { return a.path < b.path; });
        return v;
    }

    bool readDescriptor(const DeviceInfo &, std::vector<uint8_t> &, std::string &err) override {
        err = "Windows does not expose the raw report descriptor";
        return false;
    }

    std::unique_ptr<ReportStream> open(const DeviceInfo &dev, std::string &err, bool &permissionDenied) override {
        permissionDenied = false;
        HANDLE h = ::CreateFileW(widen(dev.path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            const DWORD e = ::GetLastError();
            permissionDenied = e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED;
            err = "CreateFile on the HID interface failed: " + winErrorText(e);
            return nullptr;
        }
        PHIDP_PREPARSED_DATA pre = nullptr;
        HIDP_CAPS caps;
        size_t len = 0;
        if (::HidD_GetPreparsedData(h, &pre)) {
            if (::HidP_GetCaps(pre, &caps) == HIDP_STATUS_SUCCESS) len = caps.InputReportByteLength;
            ::HidD_FreePreparsedData(pre);
        }
        if (len == 0) {
            ::CloseHandle(h);
            err = "the interface has no input reports";
            return nullptr;
        }
        return std::unique_ptr<ReportStream>(new WinStream(h, len));
    }

    std::string permissionHelp(const DeviceInfo &) const override {
        return "The device could not be opened for reading: another program, possibly the 3Dconnexion driver, "
               "holds it exclusively (sharing violation) or Windows denies access.\n"
               "This is a finding, not something you did wrong: please report it with the file written by "
               "scripts\\test-driver-coexistence.ps1 (see docs/troubleshooting.md, \"The official 3Dconnexion "
               "driver and dofscroll\"). hidprobe never changes anything on the device or in the driver.";
    }

private:
    // Opens with access 0 (query only: works while others read) and fills the identity and caps.
    static bool query(const wchar_t *path, DeviceInfo &di, unsigned &usagePage, unsigned &usage, USHORT &inLen) {
        HANDLE h = ::CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        HIDD_ATTRIBUTES at;
        at.Size = sizeof at;
        bool ok = false;
        if (::HidD_GetAttributes(h, &at)) {
            di.vid = at.VendorID;
            di.pid = at.ProductID;
            PHIDP_PREPARSED_DATA pre = nullptr;
            HIDP_CAPS caps;
            if (::HidD_GetPreparsedData(h, &pre)) {
                if (::HidP_GetCaps(pre, &caps) == HIDP_STATUS_SUCCESS) {
                    usagePage = caps.UsagePage;
                    usage = caps.Usage;
                    inLen = caps.InputReportByteLength;
                    ok = true;
                }
                ::HidD_FreePreparsedData(pre);
            }
            wchar_t name[128] = L"";
            if (::HidD_GetProductString(h, name, sizeof name - sizeof(wchar_t))) di.name = narrow(name);
            wchar_t ser[128] = L"";
            if (::HidD_GetSerialNumberString(h, ser, sizeof ser - sizeof(wchar_t))) di.serial = narrow(ser);
        }
        ::CloseHandle(h);
        return ok;
    }
};

}  // namespace

std::unique_ptr<HidBackend> makeBackend() { return std::unique_ptr<HidBackend>(new WinBackend); }

}  // namespace hidprobe
