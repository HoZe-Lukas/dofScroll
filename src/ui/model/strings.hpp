// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// String provider of the toolkit-free UI models. The models only know English source strings;
// the view passes a provider that translates them (gettext, Qt tr, ...). No toolkit dependency.
#pragma once
#include <initializer_list>
#include <string>

namespace spn::ui {

class StringProvider {
public:
    virtual ~StringProvider() = default;
    // Translation of an English source string (return the source if there is none).
    virtual std::string tr(const char *source) const = 0;
};

// Provider without translation (tests, fallback).
class IdentityStrings : public StringProvider {
public:
    std::string tr(const char *source) const override { return source; }
};

// Replaces %1, %2, ... in text by the arguments (1-based). Unknown/missing placeholders stay.
inline std::string format(const std::string &text, std::initializer_list<std::string> args) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 1 < text.size() && text[i + 1] >= '1' && text[i + 1] <= '9') {
            const size_t n = size_t(text[i + 1] - '1');
            if (n < args.size()) {
                out += *(args.begin() + n);
                ++i;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

}  // namespace spn::ui
