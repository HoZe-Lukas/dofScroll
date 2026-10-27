// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The UI tests must not depend on the system they run on (issue #186): the "fresh state" the models fall back to
// is an explicit fixture, the shipped default file of the named system, not the file of the host.
#pragma once
#include <string>

#include "ui/model/fresh_state.hpp"

inline void useFreshDefaultOf(const char *os) {
    spn::ui::setFreshSettings(spn::ui::settingsOfFile(std::string("examples/default/") + os + "/dofscroll.conf"));
}
