// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Filter boxes of the interface (Overview profiles, Channels settings) and the "Config key" hint
// of a setting row. Matching folds ASCII case only (docs/encoding.md). No toolkit.
#pragma once
#include <string>
#include <vector>

#include "ui/model/strings.hpp"
#include "utf8.hpp"

namespace spn::ui {

// True if every word of `filter` (separated by white space) occurs in at least one of the
// fields, ignoring ASCII case. An empty filter matches everything.
inline bool matchesFilter(const std::string &filter, const std::vector<std::string> &fields) {
    std::vector<std::string> lowered;
    for (const std::string &f : fields) lowered.push_back(spn::utf8::lowerAscii(f));
    size_t pos = 0;
    while (pos < filter.size()) {
        const size_t b = filter.find_first_not_of(" \t\r\n", pos);
        if (b == std::string::npos) break;
        size_t e = filter.find_first_of(" \t\r\n", b);
        if (e == std::string::npos) e = filter.size();
        const std::string word = spn::utf8::lowerAscii(filter.substr(b, e - b));
        bool found = false;
        for (const std::string &f : lowered) found = found || f.find(word) != std::string::npos;
        if (!found) return false;
        pos = e;
    }
    return true;
}

// "Config key: SCROLL_V_SENS", the line of the tooltip and the accessible description of a row.
inline std::string configKeyLine(const std::string &key, const StringProvider &s) {
    return format(s.tr("Config key: %1"), {key});
}

// Help text of a row followed by its config key line (tooltip of the editor).
inline std::string helpWithKey(const std::string &help, const std::string &key, const StringProvider &s) {
    return (help.empty() ? std::string() : help + "\n") + configKeyLine(key, s);
}

}  // namespace spn::ui
