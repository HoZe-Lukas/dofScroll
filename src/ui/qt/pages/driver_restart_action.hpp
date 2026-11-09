// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// "Restart the 3Dconnexion driver" as one action for the General page and the cleanup assistant (Windows,
// macOS). The rules and the order of the steps are in src/ui/model/driver_restart.hpp, the operating
// system calls in src/platform/driver_restart_os.hpp; this asks first and says in words what happened.
#pragma once
#include <QString>

#include "ui/model/driver_restart.hpp"

class QWidget;

namespace spn::ui::qt {

// What the restart will do, for the question before it (the same text everywhere).
QString driverRestartQuestion();
// The outcome in words (one paragraph, plain text).
QString driverRestartSummary(const DriverRestartReport &rep);
// Asks (unless `confirm` is false), restarts with the busy cursor and returns the outcome text; *ran is false when the
// user said no or there is nothing to restart here.
QString restartDriverInteractive(QWidget *parent, bool confirm, bool *ran = nullptr);

}  // namespace spn::ui::qt
