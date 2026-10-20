// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The COM interface of the installed 3Dconnexion driver on Windows ("TDxInput"), used by LATE BINDING
// only: names are looked up through IDispatch::GetIDsOfNames at run time. No type library is imported
// and no SDK header or library is used or shipped; see docs/legal-notes.md. 3Dconnexion (a Logitech
// company) owns the driver, the interface and the names; dofscroll is not affiliated with or endorsed
// by them.
//
// What is declared here and where it comes from (all public, none of it copied as code):
//   ProgID "TDxInput.Device": the #import / CreateObject name in the public samples
//     - https://so-zou.jp/software/tech/device/space-navigator/sample/polling.htm (C++ sample)
//     - 3Dconnexion forum, "TDxInput.dll - problems compiling also under x32" and the python
//       win32com.client.Dispatch("TDxInput.Device") sample in "Where's the SDK?" (forum.3dconnexion.com
//       t=2659 and t=984).
//   Object model (Device -> Sensor, Keyboard; Sensor -> Translation, Rotation; members and their
//     meaning): the interface description at https://spacenav.sourceforge.net/TDxInput.html (spacenav
//     project, a reconstruction of the TDxInput interface, not the vendor's text):
//       Device:   Sensor, Keyboard (properties returning objects), IsConnected (bool), Type,
//                 Connect(), Disconnect(), LoadPreferences(name)
//       Keyboard: Keys (number of keys), IsKeyDown(keyCode) -> bool
//       Sensor:   Translation -> Vector3D (X, Y, Z), Rotation -> AngleAxis (X, Y, Z, Angle)
//     The same names appear in the 3Dconnexion forum samples (C#, VB.NET, python) that read
//     sn.Sensor.Translation.X, Sensor.Rotation.X and the Keyboard events.
//
// NOT established from public sources and therefore assumptions (unverified, listed in
// docs/port-architecture.md): the unit and range of Translation and of Angle ("arbitrary units"), whether
// the AngleAxis axis is a unit vector (a forum report of rotations that only read 1 and -1 suggests it),
// whether key codes start at 0 or at 1 (kWinDriverKeyBase below), the values of Type, the apartment
// the object wants to live in, and whether the object lives in a managed (.NET) server (the code does
// not care: it only uses COM activation).
#pragma once
#ifndef _WIN32
#error "win_driver_api.hpp is for Windows only"
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <oleauto.h>

#include <string>

namespace spn::wintdx {

// Member names for GetIDsOfNames (case-insensitive in COM, written as in the documentation).
constexpr const wchar_t *kDevice_Sensor = L"Sensor";
constexpr const wchar_t *kDevice_Keyboard = L"Keyboard";
constexpr const wchar_t *kDevice_IsConnected = L"IsConnected";
constexpr const wchar_t *kDevice_Connect = L"Connect";
constexpr const wchar_t *kDevice_Disconnect = L"Disconnect";
constexpr const wchar_t *kKeyboard_Keys = L"Keys";
constexpr const wchar_t *kKeyboard_IsKeyDown = L"IsKeyDown";
constexpr const wchar_t *kSensor_Translation = L"Translation";
constexpr const wchar_t *kSensor_Rotation = L"Rotation";
constexpr const wchar_t *kVec_X = L"X";
constexpr const wchar_t *kVec_Y = L"Y";
constexpr const wchar_t *kVec_Z = L"Z";
constexpr const wchar_t *kAxisAngle_Angle = L"Angle";

// A late-bound IDispatch object. Not thread-safe: use it on the thread (apartment) that created it.
class Obj {
public:
    Obj() = default;
    explicit Obj(IDispatch *d) : d_(d) {}  // takes over one reference
    Obj(const Obj &) = delete;
    Obj &operator=(const Obj &) = delete;
    Obj(Obj &&o) noexcept : d_(o.d_) { o.d_ = nullptr; }
    Obj &operator=(Obj &&o) noexcept {
        if (this != &o) {
            reset();
            d_ = o.d_;
            o.d_ = nullptr;
        }
        return *this;
    }
    ~Obj() { reset(); }
    void reset() {
        if (d_) d_->Release();
        d_ = nullptr;
    }
    explicit operator bool() const { return d_ != nullptr; }
    IDispatch *raw() const { return d_; }

    // Property get or method call without arguments; the result is left in *out (VT_EMPTY if none).
    HRESULT get(const wchar_t *name, VARIANT *out) const { return call(name, DISPATCH_PROPERTYGET | DISPATCH_METHOD, nullptr, 0, out); }
    HRESULT getObj(const wchar_t *name, Obj *out) const {
        VARIANT v;
        ::VariantInit(&v);
        HRESULT hr = get(name, &v);
        if (FAILED(hr)) return hr;
        if (v.vt == VT_DISPATCH && v.pdispVal) {
            v.pdispVal->AddRef();
            *out = Obj(v.pdispVal);
            ::VariantClear(&v);
            return S_OK;
        }
        ::VariantClear(&v);
        return DISP_E_TYPEMISMATCH;
    }
    HRESULT getDouble(const wchar_t *name, double *out) const {
        VARIANT v;
        ::VariantInit(&v);
        HRESULT hr = get(name, &v);
        if (FAILED(hr)) return hr;
        VARIANT d;
        ::VariantInit(&d);
        hr = ::VariantChangeType(&d, &v, 0, VT_R8);
        ::VariantClear(&v);
        if (FAILED(hr)) return hr;
        *out = d.dblVal;
        ::VariantClear(&d);
        return S_OK;
    }
    HRESULT getLong(const wchar_t *name, long *out) const {
        VARIANT v;
        ::VariantInit(&v);
        HRESULT hr = get(name, &v);
        if (FAILED(hr)) return hr;
        VARIANT d;
        ::VariantInit(&d);
        hr = ::VariantChangeType(&d, &v, 0, VT_I4);
        ::VariantClear(&v);
        if (FAILED(hr)) return hr;
        *out = d.lVal;
        ::VariantClear(&d);
        return S_OK;
    }
    // VT_BOOL (or anything convertible to it); true = VARIANT_TRUE.
    HRESULT getBool(const wchar_t *name, bool *out) const {
        VARIANT v;
        ::VariantInit(&v);
        HRESULT hr = get(name, &v);
        if (FAILED(hr)) return hr;
        VARIANT d;
        ::VariantInit(&d);
        hr = ::VariantChangeType(&d, &v, 0, VT_BOOL);
        ::VariantClear(&v);
        if (FAILED(hr)) return hr;
        *out = d.boolVal != VARIANT_FALSE;
        ::VariantClear(&d);
        return S_OK;
    }
    // Method with one integer argument that returns a boolean (IsKeyDown(code)).
    HRESULT boolWithInt(const wchar_t *name, long arg, bool *out) const {
        VARIANT a;
        ::VariantInit(&a);
        a.vt = VT_I4;
        a.lVal = arg;
        VARIANT v;
        ::VariantInit(&v);
        HRESULT hr = call(name, DISPATCH_METHOD | DISPATCH_PROPERTYGET, &a, 1, &v);
        if (FAILED(hr)) return hr;
        VARIANT d;
        ::VariantInit(&d);
        hr = ::VariantChangeType(&d, &v, 0, VT_BOOL);
        ::VariantClear(&v);
        if (FAILED(hr)) return hr;
        *out = d.boolVal != VARIANT_FALSE;
        ::VariantClear(&d);
        return S_OK;
    }
    // Method call without arguments, result ignored (Connect, Disconnect).
    HRESULT run(const wchar_t *name) const {
        VARIANT v;
        ::VariantInit(&v);
        const HRESULT hr = call(name, DISPATCH_METHOD, nullptr, 0, &v);
        ::VariantClear(&v);
        return hr;
    }
    // True when the member exists (GetIDsOfNames succeeds).
    bool has(const wchar_t *name) const {
        DISPID id = 0;
        return d_ && SUCCEEDED(lookup(name, &id));
    }

private:
    HRESULT lookup(const wchar_t *name, DISPID *id) const {
        LPOLESTR n = const_cast<LPOLESTR>(name);
        return d_->GetIDsOfNames(IID_NULL, &n, 1, LOCALE_USER_DEFAULT, id);
    }
    HRESULT call(const wchar_t *name, WORD flags, VARIANT *args, UINT nargs, VARIANT *out) const {
        if (!d_) return E_POINTER;
        DISPID id = 0;
        HRESULT hr = lookup(name, &id);
        if (FAILED(hr)) return hr;
        DISPPARAMS dp{};
        dp.rgvarg = args;
        dp.cArgs = nargs;
        EXCEPINFO ex{};
        UINT argErr = 0;
        hr = d_->Invoke(id, IID_NULL, LOCALE_USER_DEFAULT, flags, &dp, out, &ex, &argErr);
        if (FAILED(hr) && hr == DISP_E_EXCEPTION) {
            ::SysFreeString(ex.bstrSource);
            ::SysFreeString(ex.bstrDescription);
            ::SysFreeString(ex.bstrHelpFile);
        }
        return hr;
    }

    IDispatch *d_ = nullptr;
};

}  // namespace spn::wintdx
