// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// A FAKE of the Windows 3Dconnexion COM object for tests. It is not the vendor's library and contains
// nothing of it: it is a tiny in-process COM server that answers to the member names that
// src/platform/win/win_driver_api.hpp uses (Device, Sensor, Keyboard, Translation, Rotation, ...), with
// scriptable values. tests/test_win_driver_com.cpp registers it under a test ProgID in HKCU, lets the
// production source activate it through COM, and drives it through the exported Fake* functions.
//
// The object model is late bound (IDispatch without a type library), exactly as the source uses it.
#ifndef _WIN32
#error "fake_tdxinput.cpp is built on Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <objbase.h>
#include <oleauto.h>

#include <cwchar>
#include <mutex>

namespace {

// A class id of our own for the fake (not the vendor's).
const GUID kFakeClsid = {0x5c2d1e0a, 0x7a44, 0x4b8e, {0x9b, 0x77, 0x0f, 0x3a, 0x5e, 0x6d, 0x1c, 0x01}};

struct Script {
    double t[3] = {0, 0, 0};
    double axis[3] = {0, 0, 0};
    double angle = 0;
    unsigned buttons = 0;  // bit n = key code n + 1
    int connected = 1;
    int keys = 4;
    int missingMember = 0;     // 1: the Device has no "Sensor" member
    long connectResult = 0;    // HRESULT of Connect
    int connects = 0, disconnects = 0;
};
std::mutex g_mu;
Script g_s;

enum Kind { kDevice, kSensor, kKeyboard, kVec, kRot };

struct Member {
    const wchar_t *name;
    DISPID id;
};
const Member kDeviceMembers[] = {{L"Sensor", 1}, {L"Keyboard", 2}, {L"IsConnected", 3}, {L"Connect", 4}, {L"Disconnect", 5}, {L"Type", 6}};
const Member kSensorMembers[] = {{L"Translation", 1}, {L"Rotation", 2}};
const Member kKeyboardMembers[] = {{L"Keys", 1}, {L"IsKeyDown", 2}};
const Member kVecMembers[] = {{L"X", 1}, {L"Y", 2}, {L"Z", 3}};
const Member kRotMembers[] = {{L"X", 1}, {L"Y", 2}, {L"Z", 3}, {L"Angle", 4}};

class FakeObject : public IDispatch {
public:
    explicit FakeObject(Kind k) : kind_(k) {}
    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IDispatch) {
            *ppv = static_cast<IDispatch *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return static_cast<ULONG>(::InterlockedIncrement(&ref_)); }
    ULONG STDMETHODCALLTYPE Release() override {
        const LONG n = ::InterlockedDecrement(&ref_);
        if (n == 0) delete this;
        return static_cast<ULONG>(n);
    }
    // IDispatch
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT *n) override {
        *n = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo **) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR *names, UINT n, LCID, DISPID *ids) override {
        if (n != 1) return DISP_E_UNKNOWNNAME;
        int missing;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            missing = g_s.missingMember;
        }
        const Member *m = nullptr;
        size_t count = 0;
        table(&m, &count);
        for (size_t i = 0; i < count; ++i) {
            if (_wcsicmp(m[i].name, names[0]) != 0) continue;
            if (kind_ == kDevice && missing == 1 && m[i].id == 1) break;  // "Sensor" is missing
            ids[0] = m[i].id;
            return S_OK;
        }
        ids[0] = DISPID_UNKNOWN;
        return DISP_E_UNKNOWNNAME;
    }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID id, REFIID, LCID, WORD flags, DISPPARAMS *dp, VARIANT *res, EXCEPINFO *, UINT *) override {
        if (!res) return S_OK;
        ::VariantInit(res);
        Script s;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            s = g_s;
        }
        switch (kind_) {
        case kDevice:
            if (id == 1) return object(new FakeObject(kSensor), res);
            if (id == 2) return object(new FakeObject(kKeyboard), res);
            if (id == 3) return boolean(s.connected != 0, res);
            if (id == 4) {
                std::lock_guard<std::mutex> lk(g_mu);
                ++g_s.connects;
                return static_cast<HRESULT>(s.connectResult);
            }
            if (id == 5) {
                std::lock_guard<std::mutex> lk(g_mu);
                ++g_s.disconnects;
                return S_OK;
            }
            if (id == 6) return integer(1, res);
            break;
        case kSensor:
            if (id == 1) return object(new FakeObject(kVec), res);
            if (id == 2) return object(new FakeObject(kRot), res);
            break;
        case kKeyboard:
            if (id == 1) return integer(s.keys, res);
            if (id == 2) {
                if (!dp || dp->cArgs != 1) return DISP_E_BADPARAMCOUNT;
                VARIANT a;
                ::VariantInit(&a);
                if (FAILED(::VariantChangeType(&a, &dp->rgvarg[0], 0, VT_I4))) return DISP_E_TYPEMISMATCH;
                const long code = a.lVal;
                return boolean(code >= 1 && code <= 32 && (s.buttons & (1u << (code - 1))) != 0, res);
            }
            break;
        case kVec:
            if (id >= 1 && id <= 3) return real(s.t[id - 1], res);
            break;
        case kRot:
            if (id >= 1 && id <= 3) return real(s.axis[id - 1], res);
            if (id == 4) return real(s.angle, res);
            break;
        }
        (void)flags;
        return DISP_E_MEMBERNOTFOUND;
    }

private:
    void table(const Member **m, size_t *n) const {
        switch (kind_) {
        case kDevice: *m = kDeviceMembers; *n = sizeof kDeviceMembers / sizeof *kDeviceMembers; break;
        case kSensor: *m = kSensorMembers; *n = sizeof kSensorMembers / sizeof *kSensorMembers; break;
        case kKeyboard: *m = kKeyboardMembers; *n = sizeof kKeyboardMembers / sizeof *kKeyboardMembers; break;
        case kVec: *m = kVecMembers; *n = sizeof kVecMembers / sizeof *kVecMembers; break;
        case kRot: *m = kRotMembers; *n = sizeof kRotMembers / sizeof *kRotMembers; break;
        }
    }
    static HRESULT object(FakeObject *o, VARIANT *res) {
        res->vt = VT_DISPATCH;
        res->pdispVal = o;  // the new object carries the reference count of 1 for the caller
        return S_OK;
    }
    static HRESULT boolean(bool b, VARIANT *res) {
        res->vt = VT_BOOL;
        res->boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
        return S_OK;
    }
    static HRESULT integer(long v, VARIANT *res) {
        res->vt = VT_I4;
        res->lVal = v;
        return S_OK;
    }
    static HRESULT real(double v, VARIANT *res) {
        res->vt = VT_R8;
        res->dblVal = v;
        return S_OK;
    }

    LONG ref_ = 1;
    Kind kind_;
};

class Factory : public IClassFactory {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory *>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 2; }   // static object
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown *outer, REFIID riid, void **ppv) override {
        if (outer) return CLASS_E_NOAGGREGATION;
        FakeObject *o = new FakeObject(kDevice);
        const HRESULT hr = o->QueryInterface(riid, ppv);
        o->Release();
        return hr;
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};
Factory g_factory;

}  // namespace

// The Windows SDK declares these two without dllexport (a dllexport redefinition is error C2375 on
// MSVC), so they are exported through the linker instead.
#if defined(_MSC_VER)
#pragma comment(linker, "/EXPORT:DllGetClassObject")
#pragma comment(linker, "/EXPORT:DllCanUnloadNow")
#define FAKE_TDX_COM_EXPORT
#else
#define FAKE_TDX_COM_EXPORT __declspec(dllexport)
#endif
extern "C" FAKE_TDX_COM_EXPORT HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID riid, void **ppv) {
    if (!ppv) return E_POINTER;
    *ppv = nullptr;
    if (!IsEqualCLSID(clsid, kFakeClsid)) return CLASS_E_CLASSNOTAVAILABLE;
    return g_factory.QueryInterface(riid, ppv);
}

extern "C" FAKE_TDX_COM_EXPORT HRESULT __stdcall DllCanUnloadNow() { return S_FALSE; }

// ---- test control ---------------------------------------------------------------------------------

extern "C" __declspec(dllexport) void FakeTdx_Clsid(GUID *out) { *out = kFakeClsid; }

extern "C" __declspec(dllexport) void FakeTdx_Reset() {
    std::lock_guard<std::mutex> lk(g_mu);
    g_s = Script();
}

extern "C" __declspec(dllexport) void FakeTdx_Set(double tx, double ty, double tz, double ax, double ay, double az, double angle,
                                                  unsigned buttons, int connected, int keys) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_s.t[0] = tx;
    g_s.t[1] = ty;
    g_s.t[2] = tz;
    g_s.axis[0] = ax;
    g_s.axis[1] = ay;
    g_s.axis[2] = az;
    g_s.angle = angle;
    g_s.buttons = buttons;
    g_s.connected = connected;
    g_s.keys = keys;
}

extern "C" __declspec(dllexport) void FakeTdx_SetBehavior(int missingMember, long connectResult) {
    std::lock_guard<std::mutex> lk(g_mu);
    g_s.missingMember = missingMember;
    g_s.connectResult = connectResult;
}

extern "C" __declspec(dllexport) int FakeTdx_Count(int what) {
    std::lock_guard<std::mutex> lk(g_mu);
    return what == 0 ? g_s.connects : g_s.disconnects;
}
