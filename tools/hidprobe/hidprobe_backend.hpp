// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// The one small interface a platform has to implement for dofscroll-hidprobe.
// Linux: reader_linux.cpp (hidraw). Windows (hid.dll) and macOS (IOHID) can be added by
// implementing HidBackend and returning it from makeBackend() in their own source file.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "hidprobe_core.hpp"

namespace hidprobe {

enum class ReadResult { Report, Timeout, Closed, Error };

// Read-only stream of raw input reports (byte 0 = report id when the device uses ids).
class ReportStream {
public:
    virtual ~ReportStream() = default;
    virtual ReadResult read(int timeoutMs, std::vector<uint8_t> &out) = 0;
};

class HidBackend {
public:
    virtual ~HidBackend() = default;
    virtual std::vector<DeviceInfo> list() = 0;
    // Report descriptor, if the OS exposes it. False + err otherwise.
    virtual bool readDescriptor(const DeviceInfo &dev, std::vector<uint8_t> &out,
                                std::string &err) = 0;
    // Opens the device read-only. On failure returns null and sets err (and permissionDenied).
    virtual std::unique_ptr<ReportStream> open(const DeviceInfo &dev, std::string &err,
                                               bool &permissionDenied) = 0;
    // Platform specific hint printed when open() fails with permissionDenied.
    virtual std::string permissionHelp(const DeviceInfo &dev) const = 0;
};

std::unique_ptr<HidBackend> makeBackend();

}  // namespace hidprobe
