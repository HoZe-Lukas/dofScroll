// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The Windows driver-interface input source (src/platform/win/win_driver_input.cpp) against a FAKE COM
// server (tests/fake_driver/fake_tdxinput.cpp) that is registered under a test ProgID in HKCU while the
// test runs and removed again afterwards. The production source is created for that test ProgID through
// the test-only factory; the production ProgID and the location checks are only exercised negatively
// (a CI runner has no 3Dconnexion driver). Proves: loading and activation through COM, scripted axis and
// button states, device removal, class not registered, interface mismatch, Connect refused.
#ifndef _WIN32
#error "test_win_driver_com is a Windows test"
#endif
#undef NDEBUG
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <objbase.h>

#include <array>
#include <cassert>
#include <cstdio>
#include <string>

#include "platform/win/win_driver_input.hpp"

#ifndef FAKE_TDX_PATH
#error "FAKE_TDX_PATH must name the fake COM server"
#endif

using namespace spn;

namespace {

std::wstring widenAscii(const std::string &s) { return std::wstring(s.begin(), s.end()); }

// GetProcAddress returns FARPROC; going through void* avoids -Wcast-function-type (an error under MinGW).
template <class F>
F procAddress(HMODULE h, const char *name) {
    return reinterpret_cast<F>(reinterpret_cast<void *>(::GetProcAddress(h, name)));
}

struct Fake {
    HMODULE h = nullptr;
    void (*reset)() = nullptr;
    void (*set)(double, double, double, double, double, double, double, unsigned, int, int) = nullptr;
    void (*setBehavior)(int, long) = nullptr;
    int (*count)(int) = nullptr;
    void (*clsid)(GUID *) = nullptr;
    Fake() {
        h = ::LoadLibraryW(widenAscii(FAKE_TDX_PATH).c_str());
        assert(h);
        reset = procAddress<decltype(reset)>(h, "FakeTdx_Reset");
        set = procAddress<decltype(set)>(h, "FakeTdx_Set");
        setBehavior = procAddress<decltype(setBehavior)>(h, "FakeTdx_SetBehavior");
        count = procAddress<decltype(count)>(h, "FakeTdx_Count");
        clsid = procAddress<decltype(clsid)>(h, "FakeTdx_Clsid");
        assert(reset && set && setBehavior && count && clsid);
        reset();
    }
};

// HKCU\Software\Classes\<progId>\CLSID and ...\CLSID\{guid}\InprocServer32 (+ ThreadingModel=Both).
struct Registration {
    std::wstring progId, clsidText;
    bool ok = false;
    static bool setValue(HKEY root, const std::wstring &key, const wchar_t *name, const std::wstring &value) {
        HKEY k = nullptr;
        if (::RegCreateKeyExW(root, key.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
        const LONG r = ::RegSetValueExW(k, name, 0, REG_SZ, reinterpret_cast<const BYTE *>(value.c_str()),
                                        static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        ::RegCloseKey(k);
        return r == ERROR_SUCCESS;
    }
    Registration(const std::wstring &prog, const GUID &g, const std::wstring &dll) : progId(prog) {
        wchar_t buf[64];
        ::StringFromGUID2(g, buf, 64);
        clsidText = buf;
        const std::wstring base = L"Software\\Classes\\";
        ok = setValue(HKEY_CURRENT_USER, base + progId + L"\\CLSID", nullptr, clsidText) &&
             setValue(HKEY_CURRENT_USER, base + L"CLSID\\" + clsidText + L"\\InprocServer32", nullptr, dll) &&
             setValue(HKEY_CURRENT_USER, base + L"CLSID\\" + clsidText + L"\\InprocServer32", L"ThreadingModel", L"Both");
    }
    ~Registration() {
        const std::wstring base = L"Software\\Classes\\";
        ::RegDeleteTreeW(HKEY_CURRENT_USER, (base + progId).c_str());
        ::RegDeleteTreeW(HKEY_CURRENT_USER, (base + L"CLSID\\" + clsidText).c_str());
    }
};

bool next(InputSource &s, InputEvent *e, int ms = 3000) { return s.wait(e, ms) == WaitResult::Event; }

InputEvent expectType(InputSource &s, InputEventType t) {
    InputEvent e;
    for (int i = 0; i < 400; ++i) {
        assert(next(s, &e));
        if (e.type == t) return e;
    }
    assert(false && "event type not seen");
    return e;
}

}  // namespace

int main() {
    Fake fk;
    GUID g{};
    fk.clsid(&g);
    const std::wstring prog = L"DofscrollTest.FakeTDx." + std::to_wstring(::GetCurrentProcessId());
    Registration reg(prog, g, widenAscii(FAKE_TDX_PATH));
    assert(reg.ok);

    // ---- the production check on a machine without the driver ----
    {
        std::string why;
        if (!winDriverInstalled(&why)) assert(!why.empty());
    }

    // ---- class not registered ----
    {
        auto s = makeWinDriverInputForProgId(L"DofscrollTest.NotRegistered");
        std::string err;
        assert(!s->open(&err) && err.find("not registered") != std::string::npos);
    }

    // ---- activation, scripted states ----
    {
        // translation 10/-20/30; rotation axis (1, 0, 0) angle -25 -> rotation vector (-25, 0, 0); keys 1 and 3 down
        fk.set(10, -20, 30, 1, 0, 0, -25, 0x5u, 1, 4);
        auto s = makeWinDriverInputForProgId(prog);
        std::string err;
        assert(s->open(&err) && err.empty());
        assert(fk.count(0) == 1);  // Connect was called
        expectType(*s, InputEventType::DevicesChanged);
        auto devs = s->devices();
        assert(devs.size() == 1 && devs[0].name == "SpaceMouse (driver)" && devs[0].reportedButtons == 4);

        InputEvent e = expectType(*s, InputEventType::Motion);
        assert(e.axes[0] == 10 && e.axes[1] == -20 && e.axes[2] == 30 && e.axes[3] == -25 && e.axes[4] == 0 && e.axes[5] == 0);
        e = expectType(*s, InputEventType::Button);
        assert(e.button == 0 && e.down);
        e = expectType(*s, InputEventType::Button);
        assert(e.button == 2 && e.down);

        // everything back to rest
        fk.set(0, 0, 0, 0, 0, 0, 0, 0u, 1, 4);
        bool zero = false, up0 = false, up2 = false;
        for (int i = 0; i < 400 && !(zero && up0 && up2); ++i) {
            if (!next(*s, &e)) break;
            if (e.type == InputEventType::Motion && e.axes == std::array<int, 6>{}) zero = true;
            if (e.type == InputEventType::Button && !e.down && e.button == 0) up0 = true;
            if (e.type == InputEventType::Button && !e.down && e.button == 2) up2 = true;
        }
        assert(zero && up0 && up2);

        // unplug: IsConnected turns false -> neutral state and a device list change
        fk.set(5, 0, 0, 0, 0, 0, 0, 0u, 1, 4);
        expectType(*s, InputEventType::Motion);
        fk.set(5, 0, 0, 0, 0, 0, 0, 0u, 0, 4);
        bool sawZero = false, sawChanged = false;
        for (int i = 0; i < 400 && !sawChanged; ++i) {
            if (!next(*s, &e)) break;
            if (e.type == InputEventType::Motion && e.axes == std::array<int, 6>{}) sawZero = true;
            if (e.type == InputEventType::DevicesChanged) sawChanged = true;
        }
        assert(sawZero && sawChanged && s->devices().empty());
        // plug in again
        fk.set(0, 0, 0, 0, 0, 0, 0, 0u, 1, 4);
        expectType(*s, InputEventType::DevicesChanged);
        assert(s->devices().size() == 1);

        s->close();
        assert(fk.count(1) >= 1);  // Disconnect was called
        // reopen after close
        assert(s->open(&err));
        s->close();
    }

    // ---- interface mismatch: the object lacks a member we use ----
    {
        fk.setBehavior(1, 0);
        auto s = makeWinDriverInputForProgId(prog);
        std::string err;
        assert(!s->open(&err) && err.find("interface mismatch") != std::string::npos);
        fk.setBehavior(0, 0);
    }

    // ---- the driver refuses Connect ----
    {
        fk.setBehavior(0, static_cast<long>(E_FAIL));
        auto s = makeWinDriverInputForProgId(prog);
        std::string err;
        assert(!s->open(&err) && err.find("refused Connect") != std::string::npos);
        fk.setBehavior(0, 0);
        assert(s->open(&err));  // the same object can try again
        s->close();
    }

    std::puts("test_win_driver_com: ok");
    return 0;
}
