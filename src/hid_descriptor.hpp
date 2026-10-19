// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Raw-HID device descriptions (devices/*.conf) and a pure report decoder.
// File format: see docs/devices.md.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace spn {

constexpr size_t kMaxDescriptorFiles = 64;
constexpr size_t kMaxDescriptorBytes = 16 * 1024;

enum class HidType { S8, U8, S16LE, S16BE, U16LE, U16BE, S32LE };

struct HidAxis {
    bool present = false;
    unsigned reportId = 0;
    unsigned offset = 0;  // bytes after the report-id byte
    HidType type = HidType::S16LE;
    double scale = 1.0;
};

struct HidDescriptor {
    std::string name;
    std::vector<std::pair<unsigned, unsigned>> ids;  // (vid, pid)
    int factoryButtons = 0;
    HidAxis axes[6];  // tx ty tz rx ry rz
    bool hasButtons = false;
    unsigned buttonReport = 0;
    unsigned buttonOffset = 0;  // bytes after the report-id byte
    unsigned buttonBits = 0;    // 1..32; bit i (LSB first, little-endian) = button i
};

struct HidDecoded {
    bool axisSet[6] = {false, false, false, false, false, false};
    double axis[6] = {0, 0, 0, 0, 0, 0};
    bool buttonsSet = false;
    uint32_t buttons = 0;
};

// Parses one description (text). Returns false and sets `err` on any error.
bool parseHidDescriptor(const std::string &text, HidDescriptor &out, std::string &err);

// Reads one file (regular file, no symlink, <= kMaxDescriptorBytes) and parses it.
bool loadHidDescriptorFile(const std::string &path, HidDescriptor &out, std::string &err);

// Loads all "*.conf" files of a directory (sorted by name, at most
// kMaxDescriptorFiles considered). Files that fail are skipped and reported in `errors`.
// Returns false only if the directory itself cannot be read.
bool loadHidDescriptorDir(const std::string &dir, std::vector<HidDescriptor> &out,
                          std::vector<std::string> &errors);

// Decodes a raw report (byte 0 = report id) with `d`. Returns true if the report id
// belongs to at least one axis or to the buttons and the data is long enough.
bool decodeHidReport(const HidDescriptor &d, const uint8_t *data, size_t len, HidDecoded &out);

}  // namespace spn
