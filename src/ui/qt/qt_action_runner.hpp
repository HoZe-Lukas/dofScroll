// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Qt implementation of the models' ActionRunner and StringProvider.
#pragma once
#include <string>

#include "ui/model/action_runner.hpp"
#include "ui/model/strings.hpp"

// openPath: QDesktopServices (default application of the OS); setAutostart and requestReload:
// the platform interface (src/platform/autostart_platform.hpp, platform.hpp).
class QtActionRunner : public spn::ui::ActionRunner {
public:
    bool openPath(const std::string &path, std::string *err) override;
    bool setAutostart(bool enable, std::string *err) override;
    bool requestReload(std::string *err) override;
};

// Translates the models' English source strings through the loaded Qt catalog.
class QtStrings : public spn::ui::StringProvider {
public:
    std::string tr(const char *source) const override;
};
