// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Windows implementations of the platform seams that need no HID/SendInput code: per-user paths,
// the named-event reload channel, the foreground-window provider, and the factories that tie the
// input/output backends together (see docs/port-architecture.md, docs/port-windows.md).
#ifndef _WIN32
#error "win_platform.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <sddl.h>

#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "../platform.hpp"
#include "../fs.hpp"
#include "win_activewin.hpp"
#include "win_driver_input.hpp"
#include "../input_select.hpp"
#include "win_hid_input.hpp"
#include "win_objects.hpp"
#include "win_output.hpp"
#include "win_paths.hpp"
#include "win_util.hpp"

namespace spn {

namespace {

using winutil::envVar;
using winutil::lastErrorText;
using winutil::narrow;
using winutil::widen;

// Full path of the running executable's directory with '/' separators.
std::string exeDirectory() {
    const std::string exe = fs::executablePath();
    const size_t cut = exe.find_last_of('/');
    return cut == std::string::npos ? std::string() : exe.substr(0, cut);
}

// SDDL string "D:P(A;;GA;;;<user>)(A;;GA;;;SY)": full access for the current user and SYSTEM only.
std::wstring privateObjectSddl() {
    HANDLE tok = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &tok)) return L"";
    DWORD len = 0;
    ::GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
    std::wstring sddl;
    if (len) {
        std::vector<unsigned char> buf(len);
        if (::GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) {
            LPWSTR sidStr = nullptr;
            if (::ConvertSidToStringSidW(reinterpret_cast<const TOKEN_USER *>(buf.data())->User.Sid, &sidStr)) {
                sddl = L"D:P(A;;GA;;;" + std::wstring(sidStr) + L")(A;;GA;;;SY)";
                ::LocalFree(sidStr);
            }
        }
    }
    ::CloseHandle(tok);
    return sddl;
}

// ---- reload channel: named event ------------------------------------------------------------------

class EventReloadListener : public ReloadListener {
public:
    ~EventReloadListener() override {
        if (ev_) ::CloseHandle(ev_);
    }
    bool init(std::string *err) override {
        ev_ = static_cast<HANDLE>(createPrivateEvent(reloadEventName(), err));
        return ev_ != nullptr;
    }
    bool pollReload() override {
        return ev_ && ::WaitForSingleObject(ev_, 0) == WAIT_OBJECT_0;
    }

private:
    HANDLE ev_ = nullptr;
};

}  // namespace

// ---- named kernel objects ---------------------------------------------------------------------------

void *createPrivateEvent(const char *name, std::string *err) {
    const std::wstring sddl = privateObjectSddl();
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (sddl.empty() ||
        !::ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &sd, nullptr)) {
        const DWORD e = ::GetLastError();
        if (err) *err = "cannot build the access control list for " + std::string(name) + ": " + lastErrorText(e);
        return nullptr;  // fail closed: never create the event with the default (inheriting) ACL
    }
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof sa;
    sa.lpSecurityDescriptor = sd;
    // auto-reset: one SetEvent = one wake-up, consumed by the first successful wait
    HANDLE ev = ::CreateEventW(&sa, FALSE, FALSE, widen(name).c_str());
    const DWORD e = ::GetLastError();
    ::LocalFree(sd);
    if (!ev) {
        if (err) *err = "CreateEvent(" + std::string(name) + "): " + lastErrorText(e);
        return nullptr;
    }
    if (e == ERROR_ALREADY_EXISTS) {  // not ours: its ACL and owner are unknown
        ::CloseHandle(ev);
        if (err) *err = std::string(name) + " already exists (another process owns it)";
        return nullptr;
    }
    return ev;
}

// platformPaths() and requestReload() (the client side) live in client_win.cpp, which the UI links too.

std::unique_ptr<ReloadListener> makeReloadListener() { return std::make_unique<EventReloadListener>(); }

std::unique_ptr<ActiveWindowProvider> makeActiveWindowProvider() {
    return makeWinForegroundProvider();
}

std::unique_ptr<InputSource> makeInputSource(const std::string &backend, std::string *log) {
    if (backend != "auto" && backend != "hid") {
        if (log) *log = "unknown input backend '" + backend + "' (expected auto or hid)";
        return nullptr;
    }
    std::string dir;
    for (const std::string &c : descriptorDirCandidates(exeDirectory(), envVar(L"DOFSCROLL_DEVICES"))) {
        fs::FileInfo fi;
        if (fs::statPath(c, &fi) && fi.isDir) {
            dir = c;
            break;
        }
    }
    if (dir.empty()) {
        if (log) *log = "no devices directory found next to the executable (set DOFSCROLL_DEVICES)";
        return nullptr;
    }
    // Hybrid input (INPUT = auto | driver | hid, applied by the daemon through setInputMode): raw HID and
    // the COM interface of the installed 3Dconnexion driver. Which one comes first for "auto" is the
    // table in input_select.hpp.
    if (log) *log = "raw HID descriptions from " + dir;
    SwitchingInput::Parts parts;
    parts.driverInstalled = [] {
        static std::mutex mu;
        static std::string last;
        std::string why;
        const bool ok = winDriverInstalled(&why);
        std::lock_guard<std::mutex> lk(mu);
        if (!ok && why != last) std::cerr << "[dofscroll] 3Dconnexion driver interface not usable: " << why << std::endl;
        last = ok ? std::string() : why;
        return ok;
    };
    parts.makeDriver = [] {
        std::string l;
        return makeWinDriverInput(&l);
    };
    parts.makeHid = [dir] {
        std::string l;
        return makeWinHidInput(dir, &l);
    };
    return std::make_unique<SwitchingInput>(std::move(parts), InputOs::Windows);
}

// Windows has only the SendInput backend ("auto" or "sendinput"; anything else falls back to it).
std::unique_ptr<Output> makeOutput(const std::string &backend, std::string *log) {
    if (backend != "auto" && backend != "sendinput" && log) *log = "unknown output backend '" + backend + "', using sendinput";
    std::string ilog;
    std::unique_ptr<Output> out = makeWinOutput(&ilog);
    if (!out) {
        if (log) *log = ilog.empty() ? "SendInput output not available" : ilog;
        return nullptr;
    }
    std::string err;
    if (!out->init(&err)) {
        if (log) *log = "SendInput output: " + err;
        return nullptr;
    }
    return out;
}

}  // namespace spn
