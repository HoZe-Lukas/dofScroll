// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Running-programs list on X11 (see platform/running_apps.hpp). The matchString comes from the
// same helper as the active-window provider (joinWmClass), so both agree byte for byte.
#include "platform/running_apps.hpp"
#include "platform/running_apps_util.hpp"
#include "xerrors.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace spn {

namespace {

constexpr size_t kMaxTitleLen = 512;
constexpr unsigned kMaxTopLevel = 4096;

class X11RunningApps : public RunningAppsProvider {
public:
    bool list(std::vector<RunningApp> *out, std::string *err) override {
        if (out) out->clear();
        const char *st = std::getenv("XDG_SESSION_TYPE");
        if (st && std::strcmp(st, "wayland") == 0) {
            if (err) *err = "Wayland session: the running programs cannot be listed";
            return false;
        }
        Display *d = XOpenDisplay(nullptr);
        if (!d) {
            if (err) *err = "no X11 display available";
            return false;
        }
        installXErrorHandlers();
        dpy_ = d;
        std::vector<RunningApp> wins;
        for (Window w : clientWindows()) {
            RunningApp a;
            if (!readWindow(w, &a)) continue;
            wins.push_back(std::move(a));
        }
        XCloseDisplay(d);
        dpy_ = nullptr;
        if (out) *out = dedupeApps(wins);
        return true;
    }

private:
    Display *dpy_ = nullptr;

    // Reads a property; returns false on error/absent. *data must be XFree'd by the caller.
    bool prop(Window w, Atom name, Atom reqType, unsigned long maxLen, Atom *type, int *fmt,
              unsigned long *n, unsigned char **data) {
        unsigned long after = 0;
        *data = nullptr;
        g_xerror = 0;
        int rc = XGetWindowProperty(dpy_, w, name, 0, (long)maxLen, False, reqType, type, fmt, n,
                                    &after, data);
        XSync(dpy_, False);
        if (rc != Success || g_xerror || !*data || *n == 0) {
            if (*data) XFree(*data);
            *data = nullptr;
            return false;
        }
        return true;
    }

    std::string textProp(Window w, const char *atomName, bool utf8) {
        Atom name = XInternAtom(dpy_, atomName, False);
        Atom req = utf8 ? XInternAtom(dpy_, "UTF8_STRING", False) : AnyPropertyType;
        Atom type = None;
        int fmt = 0;
        unsigned long n = 0;
        unsigned char *data = nullptr;
        if (!prop(w, name, req, kMaxTitleLen / 4 + 1, &type, &fmt, &n, &data)) return std::string();
        std::string r;
        if (fmt == 8) r.assign((const char *)data, std::min<size_t>(n, kMaxTitleLen));
        XFree(data);
        return r;
    }

    std::vector<Window> clientWindows() {
        std::vector<Window> res;
        Window root = DefaultRootWindow(dpy_);
        Atom type = None;
        int fmt = 0;
        unsigned long n = 0;
        unsigned char *data = nullptr;
        if (prop(root, XInternAtom(dpy_, "_NET_CLIENT_LIST", False), XA_WINDOW, kMaxTopLevel, &type,
                 &fmt, &n, &data)) {
            if (type == XA_WINDOW && fmt == 32) {
                const long *p = (const long *)data;  // Xlib returns 32-bit items as long
                for (unsigned long i = 0; i < n; i++) res.push_back((Window)p[i]);
            }
            XFree(data);
            if (!res.empty()) return res;
        }
        // Fallback: direct children of the root that carry WM_CLASS (non-reparenting WMs)
        Window rt = 0, par = 0, *kids = nullptr;
        unsigned int nk = 0;
        g_xerror = 0;
        Status ok = XQueryTree(dpy_, root, &rt, &par, &kids, &nk);
        XSync(dpy_, False);
        if (ok && !g_xerror && kids) res.assign(kids, kids + std::min<unsigned>(nk, kMaxTopLevel));
        if (kids) XFree(kids);
        return res;
    }

    bool readWindow(Window w, RunningApp *a) {
        XClassHint ch{nullptr, nullptr};
        g_xerror = 0;
        Status ok = XGetClassHint(dpy_, w, &ch);
        XSync(dpy_, False);
        if (!ok || g_xerror) {
            if (ch.res_name) XFree(ch.res_name);
            if (ch.res_class) XFree(ch.res_class);
            return false;  // vanished or no class (root children without WM_CLASS)
        }
        a->matchString = joinWmClass(ch.res_name, ch.res_class);
        a->displayName = ch.res_class && *ch.res_class ? ch.res_class : (ch.res_name ? ch.res_name : "");
        std::string icon = joinWmClass(ch.res_class && *ch.res_class ? ch.res_class : ch.res_name,
                                       nullptr);
        if (ch.res_name) XFree(ch.res_name);
        if (ch.res_class) XFree(ch.res_class);
        if (a->matchString.empty()) return false;

        a->windowTitle = textProp(w, "_NET_WM_NAME", true);
        if (a->windowTitle.empty()) a->windowTitle = textProp(w, "WM_NAME", false);
        a->iconHint = icon;  // lowercase class name: the usual icon theme name

        Atom type = None;
        int fmt = 0;
        unsigned long n = 0;
        unsigned char *data = nullptr;
        if (prop(w, XInternAtom(dpy_, "_NET_WM_PID", False), XA_CARDINAL, 1, &type, &fmt, &n,
                 &data)) {
            if (type == XA_CARDINAL && fmt == 32) a->pid = (int)*(const long *)data;
            XFree(data);
        }
        return true;
    }
};

}  // namespace

std::unique_ptr<RunningAppsProvider> makeRunningAppsProvider() {
    return std::unique_ptr<RunningAppsProvider>(new X11RunningApps);
}

}  // namespace spn
