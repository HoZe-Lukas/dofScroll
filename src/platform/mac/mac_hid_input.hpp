// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Raw-HID InputSource for Mac (declaration shared by the backend files; see docs/port-architecture.md).
#pragma once
#include <memory>
#include <string>

#include "../platform.hpp"

namespace spn {

// Creates the raw-HID input source. `descriptorDir` is the directory with the devices/*.conf
// descriptions (loaded with loadHidDescriptorDir). `log` (may be null) receives one-line notes.
std::unique_ptr<InputSource> makeMacHidInput(const std::string &descriptorDir, std::string *log);

}  // namespace spn
