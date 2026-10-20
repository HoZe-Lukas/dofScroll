// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Where the documentation lives, for the "Documentation" buttons of the interface. The pages are
// the Markdown files of the repository (docs/), shown by the project host; the interface hands the
// address to ActionRunner::openUrl. No toolkit.
#pragma once
#include <string>

#include "ui/model/action_runner.hpp"

namespace spn::ui {

constexpr const char *kDocsBaseUrl = "https://github.com/HoZe-Lukas/dofScroll/blob/main/docs/";

// Address of one page of docs/ ("ui.md", "modes.md"). Only plain file names of Markdown files
// are accepted (letters, digits, '-' and '_' before ".md"); anything else gives the user
// interface page, so an address is never built from arbitrary text.
inline std::string docsUrl(const std::string &file = "ui.md") {
    bool ok = file.size() > 3 && file.compare(file.size() - 3, 3, ".md") == 0;
    for (size_t i = 0; ok && i + 3 < file.size(); ++i) {
        const char c = file[i];
        ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    }
    return std::string(kDocsBaseUrl) + (ok ? file : std::string("ui.md"));
}

// "Documentation" button: opens a page of docs/ through the runner (fakeable in tests).
inline bool openDocs(ActionRunner &runner, const std::string &file, std::string *err) {
    return runner.openUrl(docsUrl(file), err);
}

// Only https addresses without spaces or control characters are opened.
inline bool isOpenableUrl(const std::string &url) {
    if (url.size() < 9 || url.size() > 2048 || url.compare(0, 8, "https://") != 0) return false;
    for (char c : url)
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) return false;
    return true;
}

}  // namespace spn::ui
