// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Sleep notification (IORegisterForSystemPower) on its own run-loop thread.
#include "mac_power.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <cstdint>
#include <iostream>
#include <thread>

#include "mac_output.hpp"

namespace spn {
namespace {

io_connect_t g_rootPort = 0;

void onPower(void *, io_service_t, uint32_t message, void *argument) {
    const intptr_t id = reinterpret_cast<intptr_t>(argument);
    switch (message) {
    case kIOMessageCanSystemSleep:
        IOAllowPowerChange(g_rootPort, id);
        break;
    case kIOMessageSystemWillSleep:
        macReleaseGuard().releaseAll();
        IOAllowPowerChange(g_rootPort, id);
        break;
    default:
        break;
    }
}

}  // namespace

void startSleepWatcher() {
    std::thread([] {
        IONotificationPortRef port = nullptr;
        io_object_t notifier = 0;
        g_rootPort = IORegisterForSystemPower(nullptr, &port, &onPower, &notifier);
        if (g_rootPort == 0 || !port) {
            std::cerr << "[dofscroll] WARNING: sleep notification not available (keys are not released on sleep)"
                      << std::endl;
            return;
        }
        CFRunLoopAddSource(CFRunLoopGetCurrent(), IONotificationPortGetRunLoopSource(port), kCFRunLoopCommonModes);
        CFRunLoopRun();
    }).detach();
}

}  // namespace spn
