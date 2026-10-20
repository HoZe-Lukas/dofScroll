// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows driver-interface InputSource (see win_driver_input.hpp, win_driver_api.hpp).
//
// One thread (a single-threaded apartment with a message pump) creates the driver's COM object,
// calls Connect and then polls Sensor.Translation, Sensor.Rotation and Keyboard.IsKeyDown about every
// 5 ms (the daemon ticks at 250 Hz by default); IsConnected is polled every 250 ms and drives the
// device-added/removed events. Values go through the shared DriverInputCore, whose queue
// InputSource::wait() reads. Polling was chosen over the driver's COM events: it needs no event sink
// implementation and the daemon only wants the latest state anyway.
//
// Security: the COM class is activated only after the registration was checked (resolveInstalled):
// machine-wide registry only, no user override, server file under Program Files. Link: ole32 oleaut32
// uuid shell32 advapi32.
//
// UNVERIFIED without the real driver: everything about it (see win_driver_api.hpp): that the object
// can be created from this process next to a running driver service and next to CAD programs, the
// units of the values, the key numbering, and the apartment.
#ifndef _WIN32
#error "win_driver_input.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <objbase.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../../log_sanitize.hpp"
#include "../driver_input_core.hpp"
#include "../driver_paths.hpp"
#include "win_driver_api.hpp"
#include "win_driver_input.hpp"
#include "win_util.hpp"

namespace spn {
namespace {

using winutil::narrow;
using winutil::widen;

const char *const kLog = "[dofscroll] ";

std::string hex(HRESULT hr) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08lx", static_cast<unsigned long>(hr));
    return b;
}

// ---- registry --------------------------------------------------------------------------------------

bool readString(HKEY root, const std::wstring &subkey, const wchar_t *value, std::wstring *out) {
    HKEY k = nullptr;
    if (::RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    bool ok = false;
    if (::RegQueryValueExW(k, value, nullptr, &type, nullptr, &size) == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ) &&
        size >= sizeof(wchar_t) && size <= 4096 * sizeof(wchar_t)) {
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, L'\0');
        DWORD sz = size;
        if (::RegQueryValueExW(k, value, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &sz) == ERROR_SUCCESS) {
            std::wstring s(buf.data());
            if (type == REG_EXPAND_SZ) {
                wchar_t ex[4096];
                const DWORD n = ::ExpandEnvironmentStringsW(s.c_str(), ex, 4096);
                if (n > 0 && n <= 4096) s = ex;
            }
            *out = s;
            ok = true;
        }
    }
    ::RegCloseKey(k);
    return ok;
}

bool keyExists(HKEY root, const std::wstring &subkey) {
    HKEY k = nullptr;
    if (::RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    ::RegCloseKey(k);
    return true;
}

bool firstSubkey(HKEY root, const std::wstring &subkey, std::wstring *name) {
    HKEY k = nullptr;
    if (::RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[256];
    DWORD n = 256;
    const bool ok = ::RegEnumKeyExW(k, 0, buf, &n, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    ::RegCloseKey(k);
    if (ok) *name = std::wstring(buf, n);
    return ok;
}

std::string folder(int csidl) {
    wchar_t buf[MAX_PATH];
    if (FAILED(::SHGetFolderPathW(nullptr, csidl, nullptr, 0 /* SHGFP_TYPE_CURRENT */, buf))) return "";
    return narrow(buf);
}

// Final path of an existing regular file after links and junctions are resolved ("" on failure).
std::string finalPathOfFile(const std::wstring &path) {
    HANDLE h = ::CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "";
    BY_HANDLE_FILE_INFORMATION info{};
    std::string res;
    if (::GetFileInformationByHandle(h, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        wchar_t buf[2048];
        const DWORD n = ::GetFinalPathNameByHandleW(h, buf, 2048, VOLUME_NAME_DOS);
        if (n > 0 && n < 2048) {
            std::wstring w(buf, n);
            if (w.compare(0, 4, L"\\\\?\\") == 0 && w.compare(0, 8, L"\\\\?\\UNC\\") != 0) w = w.substr(4);
            res = narrow(w);
        }
    }
    ::CloseHandle(h);
    return res;
}

// "file:///C:/Program Files/x/y.dll" (a .NET code base) -> "C:\Program Files\x\y.dll"; "" if it is not a local file URL.
std::wstring codeBaseToPath(const std::wstring &url) {
    static const wchar_t kPrefix[] = L"file:///";
    if (url.size() <= 8 || _wcsnicmp(url.c_str(), kPrefix, 8) != 0) return L"";
    std::wstring p = url.substr(8);
    for (wchar_t &c : p)
        if (c == L'/') c = L'\\';
    return p;
}

bool classLooksOverridden(const std::wstring &progId, const std::wstring &clsidText) {
    return keyExists(HKEY_CURRENT_USER, L"Software\\Classes\\" + progId) ||
           keyExists(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsidText);
}

// Machine-wide, unmodified, installed below Program Files. On success *clsid is the class to create.
bool resolveInstalled(const std::wstring &progId, CLSID *clsid, std::string *why) {
    std::string dummy;
    if (!why) why = &dummy;
    std::wstring clsidText;
    if (!readString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Classes\\" + progId + L"\\CLSID", nullptr, &clsidText)) {
        *why = "the 3Dconnexion driver is not installed (its COM class is not registered)";
        return false;
    }
    if (FAILED(::CLSIDFromString(clsidText.c_str(), clsid))) {
        *why = "the registered 3Dconnexion COM class id is invalid";
        return false;
    }
    if (classLooksOverridden(progId, clsidText)) {
        *why = "the 3Dconnexion COM class is overridden for the current user, not using it";
        return false;
    }
    const std::wstring serverKey = L"SOFTWARE\\Classes\\CLSID\\" + clsidText + L"\\InprocServer32";
    std::wstring server;
    if (!readString(HKEY_LOCAL_MACHINE, serverKey, nullptr, &server)) {
        *why = "the 3Dconnexion COM class has no in-process server";
        return false;
    }
    if (server.size() >= 2 && server.front() == L'"' && server.back() == L'"') server = server.substr(1, server.size() - 2);

    std::vector<std::string> programFiles;
    for (int csidl : {CSIDL_PROGRAM_FILES, CSIDL_PROGRAM_FILESX86}) {
        const std::string f = folder(csidl);
        if (!f.empty()) programFiles.push_back(f);
    }
    std::vector<std::string> systemDirs;
    {
        wchar_t buf[MAX_PATH];
        UINT n = ::GetSystemDirectoryW(buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) systemDirs.push_back(narrow(std::wstring(buf, n)));
        n = ::GetSystemWow64DirectoryW(buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) systemDirs.push_back(narrow(std::wstring(buf, n)));
    }

    const std::string serverUtf8 = narrow(server);
    std::string lower = serverUtf8;
    for (char &c : lower)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    static const std::string kHost = "mscoree.dll";
    const bool managed = lower.size() >= kHost.size() && lower.compare(lower.size() - kHost.size(), kHost.size(), kHost) == 0;
    std::wstring file;
    if (managed) {
        // A .NET COM server: the host is mscoree.dll (system directory or just the file name, resolved by the
        // system), the real file is the code base below the class key. A GAC assembly has none: accepted.
        const bool hostOk = lower == kHost || winDriverPathAllowed(serverUtf8, systemDirs);
        if (!hostOk) {
            *why = "the 3Dconnexion COM server host is not a system library, not using it";
            return false;
        }
        std::wstring cb;
        if (!readString(HKEY_LOCAL_MACHINE, serverKey, L"CodeBase", &cb)) {
            std::wstring sub;
            if (firstSubkey(HKEY_LOCAL_MACHINE, serverKey, &sub)) readString(HKEY_LOCAL_MACHINE, serverKey + L"\\" + sub, L"CodeBase", &cb);
        }
        if (cb.empty()) return true;  // GAC
        file = codeBaseToPath(cb);
        if (file.empty()) {
            *why = "the 3Dconnexion COM code base is not a local file";
            return false;
        }
    } else {
        file = server;
    }
    const std::string fin = finalPathOfFile(file);
    if (fin.empty()) {
        *why = "the 3Dconnexion COM server file does not exist";
        return false;
    }
    if (!winDriverPathAllowed(narrow(file), programFiles) || !winDriverPathAllowed(fin, programFiles)) {
        *why = "the 3Dconnexion COM server is not installed under Program Files, not loading it";
        return false;
    }
    return true;
}

// ---- the source ------------------------------------------------------------------------------------

class WinDriverInput : public InputSource {
public:
    // trusted = false: production (registration checks at every open); true: test (no checks).
    WinDriverInput(std::wstring progId, bool checked) : progId_(std::move(progId)), checked_(checked) {}
    ~WinDriverInput() override {
        shutdown();
        if (stopEvt_) ::CloseHandle(stopEvt_);
    }

    const char *name() const override { return "3dconnexion-com"; }

    bool open(std::string *err) override {
        if (running_) return true;
        if (thread_.joinable()) {
            if (!threadDone_) {
                if (err) *err = "a previous attempt to connect to the driver is still pending";
                return false;
            }
            thread_.join();
        }
        CLSID clsid{};
        std::string why;
        if (checked_) {
            if (!resolveInstalled(progId_, &clsid, &why)) {
                if (err) *err = why;
                return false;
            }
        } else if (FAILED(::CLSIDFromProgID(progId_.c_str(), &clsid))) {
            if (err) *err = "the COM class is not registered";
            return false;
        }
        if (!stopEvt_) stopEvt_ = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);  // owned by this object, closed in the destructor
        if (stopEvt_) ::ResetEvent(stopEvt_);
        threadDone_ = false;
        failed_ = false;
        core_.open();
        auto ready = std::make_shared<std::promise<std::string>>();
        std::future<std::string> fut = ready->get_future();
        stop_ = false;
        thread_ = std::thread([this, ready, clsid] {
            run(ready, clsid);
            threadDone_ = true;
        });
        if (fut.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
            if (err) *err = "timeout while connecting to the 3Dconnexion driver";
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

    WaitResult wait(InputEvent *out, int timeoutMs) override {
        const WaitResult r = core_.wait(out, timeoutMs);
        if (r == WaitResult::Timeout && failed_) return WaitResult::Closed;  // the driver went away: reconnect
        return r;
    }
    std::vector<DeviceInfo> devices() const override { return core_.devices(); }
    void close() override { shutdown(); }

private:
    void shutdown() {
        running_ = false;
        stop_ = true;
        if (stopEvt_) ::SetEvent(stopEvt_);
        if (thread_.joinable()) thread_.join();
        core_.close();
    }

    static void pumpMessages() {
        MSG m;
        while (::PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&m);
            ::DispatchMessageW(&m);
        }
    }

    void run(const std::shared_ptr<std::promise<std::string>> &ready, const CLSID &clsid) {
        const HRESULT ci = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(ci)) {
            ready->set_value("COM initialization failed (" + hex(ci) + ")");
            return;
        }
        {
            wintdx::Obj dev, sensor, kb;
            const std::string res = connect(clsid, &dev, &sensor, &kb);
            if (!res.empty()) {
                ready->set_value(res);
            } else {
                ready->set_value("");
                poll(dev, sensor, kb);
                core_.deviceRemoved();
                dev.run(wintdx::kDevice_Disconnect);
            }
        }  // objects released before CoUninitialize
        ::CoUninitialize();
    }

    // Creates the object, checks that it has the members we use ("interface mismatch" otherwise) and
    // connects. Empty string = success.
    std::string connect(const CLSID &clsid, wintdx::Obj *dev, wintdx::Obj *sensor, wintdx::Obj *kb) {
        IDispatch *p = nullptr;
        const HRESULT hr = ::CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IDispatch, reinterpret_cast<void **>(&p));
        if (FAILED(hr) || !p) return "cannot create the 3Dconnexion COM object (" + hex(hr) + ")";
        *dev = wintdx::Obj(p);
        for (const wchar_t *n : {wintdx::kDevice_Sensor, wintdx::kDevice_Keyboard, wintdx::kDevice_IsConnected,
                                 wintdx::kDevice_Connect, wintdx::kDevice_Disconnect})
            if (!dev->has(n)) return "the 3Dconnexion COM object lacks the member " + narrow(n) + " (interface mismatch)";
        if (FAILED(dev->getObj(wintdx::kDevice_Sensor, sensor)) || FAILED(dev->getObj(wintdx::kDevice_Keyboard, kb)))
            return "the 3Dconnexion COM object returned no sensor or keyboard (interface mismatch)";
        for (const wchar_t *n : {wintdx::kSensor_Translation, wintdx::kSensor_Rotation})
            if (!sensor->has(n)) return "the 3Dconnexion sensor lacks the member " + narrow(n) + " (interface mismatch)";
        for (const wchar_t *n : {wintdx::kKeyboard_Keys, wintdx::kKeyboard_IsKeyDown})
            if (!kb->has(n)) return "the 3Dconnexion keyboard lacks the member " + narrow(n) + " (interface mismatch)";
        const HRESULT c = dev->run(wintdx::kDevice_Connect);
        if (FAILED(c)) return "the 3Dconnexion driver refused Connect (" + hex(c) + ")";
        return "";
    }

    bool readAxes(const wintdx::Obj &sensor, std::array<double, 6> *v) {
        wintdx::Obj tr, rot;
        if (FAILED(sensor.getObj(wintdx::kSensor_Translation, &tr)) || FAILED(sensor.getObj(wintdx::kSensor_Rotation, &rot))) return false;
        double t[3], r[3], angle = 0;
        if (FAILED(tr.getDouble(wintdx::kVec_X, &t[0])) || FAILED(tr.getDouble(wintdx::kVec_Y, &t[1])) ||
            FAILED(tr.getDouble(wintdx::kVec_Z, &t[2])) || FAILED(rot.getDouble(wintdx::kVec_X, &r[0])) ||
            FAILED(rot.getDouble(wintdx::kVec_Y, &r[1])) || FAILED(rot.getDouble(wintdx::kVec_Z, &r[2])) ||
            FAILED(rot.getDouble(wintdx::kAxisAngle_Angle, &angle)))
            return false;
        const std::array<double, 3> rv = axisAngleToVector(r[0], r[1], r[2], angle);
        *v = {t[0], t[1], t[2], rv[0], rv[1], rv[2]};
        return true;
    }

    void poll(const wintdx::Obj &dev, const wintdx::Obj &sensor, const wintdx::Obj &kb) {
        long keys = 0;
        bool connected = false;
        int failures = 0;
        auto lastConn = std::chrono::steady_clock::time_point();
        while (!stop_) {
            const auto now = std::chrono::steady_clock::now();
            if (lastConn == std::chrono::steady_clock::time_point() || now - lastConn >= std::chrono::milliseconds(250)) {
                lastConn = now;
                bool c = false;
                if (FAILED(dev.getBool(wintdx::kDevice_IsConnected, &c))) {
                    if (++failures >= 20) {
                        failed_ = true;
                        std::cerr << kLog << "the 3Dconnexion driver stopped answering" << std::endl;
                        return;
                    }
                } else {
                    failures = 0;
                    if (c && !connected) {
                        long k = 0;
                        if (FAILED(kb.getLong(wintdx::kKeyboard_Keys, &k))) k = 0;
                        keys = std::max<long>(0, std::min<long>(32, k));
                        core_.deviceAdded("", 0, 0, static_cast<int>(keys));
                        std::cerr << kLog << "driver reports a device (" << keys << " keys)" << std::endl;
                    } else if (!c && connected) {
                        core_.deviceRemoved();
                    }
                    connected = c;
                }
            }
            if (connected) {
                std::array<double, 6> v{};
                if (readAxes(sensor, &v)) {
                    core_.onAxes(v, kWinDriverTranslationScale, kWinDriverRotationScale);
                    uint32_t mask = 0;
                    for (long i = 0; i < keys; ++i) {
                        bool down = false;
                        if (SUCCEEDED(kb.boolWithInt(wintdx::kKeyboard_IsKeyDown, kWinDriverKeyBase + i, &down)) && down)
                            mask |= (1u << i);
                    }
                    core_.onButtons(mask);
                    failures = 0;
                } else if (++failures >= 200) {
                    failed_ = true;
                    std::cerr << kLog << "the 3Dconnexion driver stopped answering" << std::endl;
                    return;
                }
            }
            HANDLE h = stopEvt_;
            ::MsgWaitForMultipleObjects(h ? 1 : 0, h ? &h : nullptr, FALSE, 5, QS_ALLINPUT);
            pumpMessages();
        }
    }

    const std::wstring progId_;
    const bool checked_;
    DriverInputCore core_;
    std::thread thread_;
    HANDLE stopEvt_ = nullptr;  // manual-reset; created in open(), closed in the destructor
    std::atomic<bool> running_{false};
    std::atomic<bool> stop_{false};
    std::atomic<bool> failed_{false};
    std::atomic<bool> threadDone_{true};
};

}  // namespace

bool winDriverInstalled(std::string *why) {
    CLSID c{};
    return resolveInstalled(widen(kWinDriverProgId), &c, why);
}

std::unique_ptr<InputSource> makeWinDriverInput(std::string *log) {
    std::string why;
    if (!winDriverInstalled(&why)) {
        if (log) *log = why;
        return nullptr;
    }
    return std::make_unique<WinDriverInput>(widen(kWinDriverProgId), true);
}

std::unique_ptr<InputSource> makeWinDriverInputForProgId(const std::wstring &progId) {
    return std::make_unique<WinDriverInput>(progId, false);
}

}  // namespace spn
