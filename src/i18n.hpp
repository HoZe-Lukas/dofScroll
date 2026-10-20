// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Localization of UI strings via gettext. Source strings are English; translations live in
// po/<lang>.po (text domain "dofscroll"). Without gettext (DOFSCROLL_NO_GETTEXT or no
// <libintl.h>) _() returns the English text unchanged.
//
// Catalog search order: $DOFSCROLL_LOCALEDIR, <exe_dir>/../share/locale,
// /usr/local/share/locale, /usr/share/locale. The first directory that contains
// <lang>/LC_MESSAGES/dofscroll.mo for the current locale wins.
#pragma once
#include <string>

#if !defined(DOFSCROLL_NO_GETTEXT) && defined(__has_include)
// cppcheck cannot evaluate __has_include
// cppcheck-suppress preprocessorErrorDirective
#if __has_include(<libintl.h>)
#include <libintl.h>
#define DOFSCROLL_HAVE_GETTEXT 1
#endif
#endif

#ifdef DOFSCROLL_HAVE_GETTEXT
#include <clocale>
#include <cstdlib>
#include <string.h>
#include <unistd.h>
#include <vector>
#endif

namespace spn {
namespace i18n {

#ifdef DOFSCROLL_HAVE_GETTEXT
inline const char *tr(const char *s) { return s && *s ? dgettext("dofscroll", s) : s; }
inline const char *trn(const char *s, const char *p, unsigned long n) {
    return dngettext("dofscroll", s, p, n);
}

// Candidate catalog directories in search order (exe_dir may be empty).
inline std::vector<std::string> localeDirs(const char *env_dir, const std::string &exe_dir) {
    std::vector<std::string> v;
    if (env_dir && *env_dir) v.push_back(env_dir);
    if (!exe_dir.empty()) v.push_back(exe_dir + "/../share/locale");
    v.push_back("/usr/local/share/locale");
    v.push_back("/usr/share/locale");
    return v;
}

// Call once at program start, after setlocale(LC_ALL, "").
inline void init() {
    setlocale(LC_ALL, "");
    std::string exe_dir;
    char exe[4096];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = 0;
        const char *slash = strrchr(exe, '/');
        if (slash) exe_dir.assign(exe, size_t(slash - exe));
    }
    std::string chosen;
    for (const std::string &d : localeDirs(getenv("DOFSCROLL_LOCALEDIR"), exe_dir)) {
        // Pick the first directory that has a catalog for any language; gettext then
        // selects the right one for the current locale inside it.
        const char *lang = getenv("LANGUAGE");
        const char *loc = setlocale(LC_MESSAGES, nullptr);
        std::string l = lang && *lang ? lang : (loc ? loc : "");
        // Try "xx_YY.enc" -> "xx_YY" -> "xx" for each ':'-separated language entry.
        size_t pos = 0;
        bool found = false;
        while (pos <= l.size() && !found) {
            size_t e = l.find(':', pos);
            std::string one = l.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
            pos = e == std::string::npos ? l.size() + 1 : e + 1;
            size_t dot = one.find('.');
            if (dot != std::string::npos) one.resize(dot);
            std::vector<std::string> names{one};
            size_t us = one.find('_');
            if (us != std::string::npos) names.push_back(one.substr(0, us));
            for (const std::string &nm : names)
                if (!nm.empty() &&
                    access((d + "/" + nm + "/LC_MESSAGES/dofscroll.mo").c_str(), R_OK) == 0)
                    found = true;
        }
        if (found) {
            chosen = d;
            break;
        }
    }
    if (chosen.empty()) chosen = "/usr/share/locale";
    bindtextdomain("dofscroll", chosen.c_str());
    bind_textdomain_codeset("dofscroll", "UTF-8");
    textdomain("dofscroll");
}
#else
inline const char *tr(const char *s) { return s; }
inline const char *trn(const char *s, const char *p, unsigned long n) { return n == 1 ? s : p; }
inline void init() {}
#endif

}  // namespace i18n
}  // namespace spn

// Translate a string literal (or const char*). xgettext picks up _() by default.
#define _(s) ::spn::i18n::tr(s)
// Plural form: NG_("%d value", "%d values", n)
#define NG_(s, p, n) ::spn::i18n::trn(s, p, static_cast<unsigned long>(n))
// Mark a string for extraction without translating it now (translate later with _()).
#define N_(s) s
