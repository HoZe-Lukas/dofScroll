// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure helpers of the running-programs picker (no X11 needed, unit-tested).
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>
#include <vector>

#include "running_apps.hpp"
#include "../utf8.hpp"

namespace spn {

constexpr size_t kMaxWmClassLen = 256;

// WM_CLASS hint (instance, class) -> "instance class" lowercase, max kMaxWmClassLen bytes.
// Single source of the rule, used by activewin.cpp and the picker. Empty if both are empty.
inline std::string joinWmClass(const char *instance, const char *cls) {
    std::string inst = instance ? instance : "";
    std::string c = cls ? cls : "";
    std::string r = inst;
    if (!c.empty()) r += (r.empty() ? "" : " ") + c;
    if (r.size() > kMaxWmClassLen) r.resize(kMaxWmClassLen);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char ch) { return spn::utf8::lowerChar(ch); });
    return r;
}

// True if a match string belongs to the dofscroll UI itself (any token starts with "dofscroll").
inline bool isOwnAppString(const std::string &s) {
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) i++;
        if (s.compare(i, 9, "dofscroll") == 0) return true;
        while (i < s.size() && !std::isspace((unsigned char)s[i])) i++;
    }
    return false;
}

// Folds windows (in stacking order, oldest first) into one entry per matchString: first-seen
// order is kept, title/pid of the LAST window win, windowCount counts the windows. Entries with
// an empty matchString or belonging to the dofscroll UI are dropped.
inline std::vector<RunningApp> dedupeApps(const std::vector<RunningApp> &windows) {
    std::vector<RunningApp> out;
    std::unordered_map<std::string, size_t> idx;
    for (const RunningApp &w : windows) {
        if (w.matchString.empty() || isOwnAppString(w.matchString)) continue;
        auto it = idx.find(w.matchString);
        if (it == idx.end()) {
            idx.emplace(w.matchString, out.size());
            out.push_back(w);
            out.back().windowCount = 1;
        } else {
            RunningApp &e = out[it->second];
            e.windowCount++;
            e.windowTitle = w.windowTitle;
            if (w.pid) e.pid = w.pid;
            if (e.iconHint.empty()) e.iconHint = w.iconHint;
        }
    }
    return out;
}

}  // namespace spn
