// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Pure, platform independent parts of dofscroll-hidprobe: sysfs/uevent parsing, report
// descriptor summary, check/verdict logic, report scrubbing. Header only so that
// tests/test_hidprobe.cpp can exercise it without hardware.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "hid_descriptor.hpp"

namespace hidprobe {

struct DeviceInfo {
    std::string path;  // OS specific handle, e.g. /dev/hidraw3
    unsigned vid = 0;
    unsigned pid = 0;
    std::string name;
    std::string serial;  // never printed or stored verbatim
};

// ---- small helpers -------------------------------------------------------------------
inline int popcount32(uint32_t v) {  // portable: __builtin_popcount does not exist on MSVC
    int n = 0;
    for (; v; v &= v - 1) ++n;
    return n;
}

inline std::string hex2(unsigned v) {
    char b[8];
    std::snprintf(b, sizeof b, "%02x", v & 0xff);
    return b;
}

inline std::string hexBytes(const uint8_t *d, size_t n) {
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ' ';
        s += hex2(d[i]);
    }
    return s;
}

inline std::string vidpid(unsigned vid, unsigned pid) {
    char b[16];
    std::snprintf(b, sizeof b, "%04x:%04x", vid & 0xffff, pid & 0xffff);
    return b;
}

// Short stable fingerprint (FNV-1a, 32 bit) so reports from one device can be correlated
// without revealing the serial number. Empty serial gives "none".
inline std::string hashSerial(const std::string &serial) {
    if (serial.empty()) return "none";
    uint32_t h = 2166136261u;
    for (unsigned char c : serial) {
        h ^= c;
        h *= 16777619u;
    }
    char b[16];
    std::snprintf(b, sizeof b, "%08x", h);
    return b;
}

// Replaces every occurrence of `secret` (if at least 2 chars long) by `repl`.
inline std::string replaceAll(std::string s, const std::string &secret, const std::string &repl) {
    if (secret.size() < 2) return s;
    for (size_t p = s.find(secret); p != std::string::npos; p = s.find(secret, p + repl.size()))
        s.replace(p, secret.size(), repl);
    return s;
}

// Removes home directory and user name from text that goes into an issue report.
inline std::string scrubReport(const std::string &text, const std::string &home,
                               const std::string &user) {
    return replaceAll(replaceAll(text, home, "~"), user, "<user>");
}

// ---- sysfs uevent ------------------------------------------------------------------------
// Parses the text of /sys/class/hidraw/hidrawN/device/uevent. Needs HID_ID=bus:vid:pid.
inline bool parseUevent(const std::string &text, DeviceInfo &out) {
    bool gotId = false;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = text.substr(pos, nl - pos);
        pos = nl + 1;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "HID_ID") {
            unsigned bus = 0, vid = 0, pid = 0;
            if (std::sscanf(v.c_str(), "%x:%x:%x", &bus, &vid, &pid) == 3) {
                out.vid = vid & 0xffff;
                out.pid = pid & 0xffff;
                gotId = true;
            }
        } else if (k == "HID_NAME") {
            out.name = v;
        } else if (k == "HID_UNIQ") {
            out.serial = v;
        }
    }
    return gotId;
}

inline const spn::HidDescriptor *findDescriptor(const std::vector<spn::HidDescriptor> &all,
                                                unsigned vid, unsigned pid) {
    for (const auto &d : all)
        for (const auto &id : d.ids)
            if (id.first == vid && id.second == pid) return &d;
    return nullptr;
}

// ---- report descriptor summary --------------------------------------------------------------
struct DescriptorSummary {
    std::vector<std::string> lines;           // human readable
    std::map<unsigned, unsigned> inputBytes;  // report id -> total length incl. id byte
    bool usesReportIds = false;
};

inline std::string usagePageName(unsigned p) {
    switch (p) {
        case 0x01: return "Generic Desktop";
        case 0x02: return "Simulation";
        case 0x08: return "LED";
        case 0x09: return "Button";
        case 0x0c: return "Consumer";
        default: break;
    }
    if (p >= 0xff00) return "Vendor defined";
    return "Other";
}

inline std::string usageName(unsigned page, unsigned u) {
    if (page == 0x01) {
        switch (u) {
            case 0x01: return "Pointer";
            case 0x02: return "Mouse";
            case 0x04: return "Joystick";
            case 0x06: return "Keyboard";
            case 0x08: return "Multi-axis Controller";
            default: break;
        }
    }
    return "";
}

inline DescriptorSummary summarizeDescriptor(const std::vector<uint8_t> &d) {
    DescriptorSummary s;
    unsigned page = 0, rid = 0, rsize = 0, rcount = 0, depth = 0;
    long usage = -1;
    std::map<unsigned, unsigned> inBits;
    size_t i = 0;
    char buf[240];
    while (i < d.size()) {
        uint8_t b = d[i];
        if (b == 0xfe) {  // long item: skip
            if (i + 1 >= d.size()) break;
            i += 3 + d[i + 1];
            continue;
        }
        unsigned sz = b & 3;
        if (sz == 3) sz = 4;
        unsigned type = (b >> 2) & 3, tag = b >> 4;
        if (i + 1 + sz > d.size()) {
            s.lines.push_back("(descriptor truncated)");
            break;
        }
        uint32_t v = 0;
        for (unsigned k = 0; k < sz; ++k) v |= uint32_t(d[i + 1 + k]) << (8 * k);
        i += 1 + sz;
        if (type == 1) {  // global
            if (tag == 0) {
                page = v;
            } else if (tag == 7) {
                rsize = v;
            } else if (tag == 8) {
                rid = v;
                s.usesReportIds = true;
            } else if (tag == 9) {
                rcount = v;
            }
        } else if (type == 2) {  // local
            if (tag == 0) usage = long(v);
        } else if (type == 0) {  // main
            if (tag == 0xa) {
                if (depth == 0) {
                    const char *kind = v == 1 ? "Application" : v == 0 ? "Physical" : "Other";
                    std::string un = usage >= 0 ? usageName(page, unsigned(usage)) : "";
                    std::snprintf(buf, sizeof buf,
                                  "Top-level collection (%s): usage page 0x%02x (%s), usage 0x%02lx%s%s",
                                  kind, page, usagePageName(page).c_str(), usage < 0 ? 0 : usage,
                                  un.empty() ? "" : " = ", un.c_str());
                    s.lines.push_back(buf);
                }
                ++depth;
            } else if (tag == 0xc) {
                if (depth) --depth;
            } else if (tag == 8) {
                inBits[rid] += rsize * rcount;
            }
            usage = -1;
        }
    }
    for (const auto &kv : inBits) {
        unsigned bytes = (kv.second + 7) / 8 + (s.usesReportIds ? 1 : 0);
        s.inputBytes[kv.first] = bytes;
        std::snprintf(buf, sizeof buf, "Input report id %u: %u bits, %u bytes including the id byte",
                      kv.first, kv.second, bytes);
        s.lines.push_back(buf);
    }
    if (s.lines.empty()) s.lines.push_back("(no items recognised)");
    return s;
}

// ---- checking ---------------------------------------------------------------------------------
enum class Status { Ok, Warn, Mismatch, Info };

struct Verdict {
    Status status;
    std::string text;
};

inline const char *statusTag(Status s) {
    switch (s) {
        case Status::Ok: return "[ OK ]";
        case Status::Warn: return "[WARN]";
        case Status::Mismatch: return "[FAIL]";
        default: return "[INFO]";
    }
}

inline unsigned typeSize(spn::HidType t) {
    switch (t) {
        case spn::HidType::S8:
        case spn::HidType::U8: return 1;
        case spn::HidType::S32LE: return 4;
        default: return 2;
    }
}

inline const char *axisName(int i) {
    static const char *n[6] = {"tx", "ty", "tz", "rx", "ry", "rz"};
    return n[i];
}

// Collects statistics over a stream of raw reports and judges them against a description.
class Checker {
public:
    explicit Checker(const spn::HidDescriptor &d) : d_(d) {}

    // Lengths (incl. id byte) derived from the device's own report descriptor, if known.
    void setDescriptorLengths(const std::map<unsigned, unsigned> &m) { descLen_ = m; }

    void feed(const uint8_t *data, size_t len) {
        if (len == 0) return;
        ++reports_;
        Stat &st = perId_[data[0]];
        ++st.count;
        st.minLen = st.count == 1 ? len : std::min(st.minLen, len);
        st.maxLen = std::max(st.maxLen, len);
        spn::HidDecoded dec;
        if (spn::decodeHidReport(d_, data, len, dec)) {
            for (int a = 0; a < 6; ++a) {
                if (!dec.axisSet[a]) continue;
                if (!everAxis_[a]) minV_[a] = maxV_[a] = dec.axis[a];
                everAxis_[a] = true;
                minV_[a] = std::min(minV_[a], dec.axis[a]);
                maxV_[a] = std::max(maxV_[a], dec.axis[a]);
                cur_[a] = dec.axis[a];
            }
            if (dec.buttonsSet) {
                seenBits_ |= dec.buttons;
                curButtons_ = dec.buttons;
            }
        }
        if (d_.hasButtons && data[0] == d_.buttonReport) {
            // raw bits beyond the configured width (up to 32) reveal a too small `bits=`
            size_t start = 1 + d_.buttonOffset;
            uint64_t raw = 0;
            for (size_t k = 0; k < 4 && start + k < len; ++k)
                raw |= uint64_t(data[start + k]) << (8 * k);
            rawBits_ |= uint32_t(raw);
        }
    }

    size_t reportCount() const { return reports_; }

    // Compact single line with the latest decoded state.
    std::string liveLine() const {
        std::string s;
        char b[48];
        for (int a = 0; a < 6; ++a) {
            if (!d_.axes[a].present) continue;
            std::snprintf(b, sizeof b, "%s=%+7.1f ", axisName(a), cur_[a]);
            s += b;
        }
        std::snprintf(b, sizeof b, "btn=%08x", curButtons_);
        return s + b;
    }

    const double *current() const { return cur_; }
    bool axisMoved(int a) const { return everAxis_[a] && maxV_[a] != minV_[a]; }
    double axisMin(int a) const { return minV_[a]; }
    double axisMax(int a) const { return maxV_[a]; }
    uint32_t seenButtons() const { return seenBits_; }
    uint32_t rawButtons() const { return rawBits_; }

    static int highestBit(uint32_t v) {
        int h = -1;
        for (int i = 0; i < 32; ++i)
            if (v & (1u << i)) h = i;
        return h;
    }

    std::vector<Verdict> verdicts() const {
        std::vector<Verdict> v;
        char b[320];
        if (reports_ == 0) {
            v.push_back({Status::Mismatch,
                         "No reports received. Move the cap and press buttons while checking. If "
                         "still nothing: stop spacenavd briefly, check the cable/receiver, or try "
                         "another /dev/hidraw node of the same device."});
            return v;
        }
        // 1) report ids
        std::set<unsigned> cfg;
        for (int a = 0; a < 6; ++a)
            if (d_.axes[a].present) cfg.insert(d_.axes[a].reportId);
        if (d_.hasButtons) cfg.insert(d_.buttonReport);
        std::string seen;
        for (const auto &kv : perId_) seen += (seen.empty() ? "" : ", ") + std::to_string(kv.first);
        v.push_back({Status::Info, "Report ids seen: " + seen});
        for (unsigned id : cfg) {
            auto it = perId_.find(id);
            if (it == perId_.end()) {
                std::snprintf(b, sizeof b,
                              "Configured report id %u was never seen (not triggered, or the "
                              "configured id is wrong).",
                              id);
                v.push_back({Status::Warn, b});
            } else {
                std::snprintf(b, sizeof b, "Configured report id %u seen %zu times.", id,
                              it->second.count);
                v.push_back({Status::Ok, b});
            }
        }
        for (const auto &kv : perId_)
            if (!cfg.count(kv.first)) {
                std::snprintf(b, sizeof b,
                              "Report id %u (%zu reports, length %zu) is not used by the description.",
                              kv.first, kv.second.count, kv.second.maxLen);
                v.push_back({Status::Info, b});
            }
        // 2) lengths
        for (const auto &kv : perId_) {
            size_t need = requiredLength(kv.first);
            if (need) {
                if (kv.second.minLen >= need) {
                    std::snprintf(b, sizeof b,
                                  "Report id %u length %zu covers the configured fields (need %zu).",
                                  kv.first, kv.second.maxLen, need);
                    v.push_back({Status::Ok, b});
                } else {
                    std::snprintf(b, sizeof b,
                                  "Report id %u length %zu is SHORTER than the %zu bytes the "
                                  "description needs.",
                                  kv.first, kv.second.minLen, need);
                    v.push_back({Status::Mismatch, b});
                }
            }
            if (kv.second.minLen != kv.second.maxLen) {
                std::snprintf(b, sizeof b, "Report id %u had varying lengths %zu..%zu.", kv.first,
                              kv.second.minLen, kv.second.maxLen);
                v.push_back({Status::Warn, b});
            }
            auto it = descLen_.find(kv.first);
            if (it != descLen_.end() && it->second != kv.second.maxLen) {
                std::snprintf(b, sizeof b,
                              "Report id %u: report descriptor says %u bytes, observed %zu.",
                              kv.first, it->second, kv.second.maxLen);
                v.push_back({Status::Warn, b});
            }
        }
        // 3) axes
        for (int a = 0; a < 6; ++a) {
            if (!d_.axes[a].present) continue;
            if (!everAxis_[a]) {
                std::snprintf(b, sizeof b,
                              "Axis %s: no data decoded (report %u too short or never seen).",
                              axisName(a), d_.axes[a].reportId);
                v.push_back({Status::Warn, b});
            } else if (!axisMoved(a)) {
                std::snprintf(b, sizeof b, "Axis %s: did not move (stayed at %.1f). Move it to verify.",
                              axisName(a), minV_[a]);
                v.push_back({Status::Warn, b});
            } else {
                double big = std::max(std::fabs(minV_[a]), std::fabs(maxV_[a]));
                std::snprintf(b, sizeof b,
                              "Axis %s: observed %.1f .. %.1f (configured scale %g, report %u offset %u).",
                              axisName(a), minV_[a], maxV_[a], d_.axes[a].scale, d_.axes[a].reportId,
                              d_.axes[a].offset);
                Status st = Status::Ok;
                std::string txt = b;
                if (big * 200 < limit(d_.axes[a])) {
                    txt += " Range is tiny compared to the data type; push harder or check type/scale.";
                    st = Status::Warn;
                } else if (big >= limit(d_.axes[a])) {
                    txt += " Value hit the type limit: type/endianness is probably wrong.";
                    st = Status::Mismatch;
                }
                v.push_back({st, txt});
            }
        }
        // 4) buttons
        if (d_.hasButtons) {
            int hi = highestBit(seenBits_);
            int rawHi = highestBit(rawBits_);
            std::string bits;
            for (int i = 0; i < 32; ++i)
                if (seenBits_ & (1u << i)) bits += (bits.empty() ? "" : ",") + std::to_string(i);
            if (seenBits_ == 0 && rawBits_ == 0) {
                v.push_back({Status::Warn, "No button bit seen: press every button to verify."});
            } else {
                v.push_back({Status::Info, "Button bits seen (0-based): " + bits});
                if (rawHi >= int(d_.buttonBits)) {
                    std::snprintf(b, sizeof b,
                                  "Raw bit %d is set but the description only has bits=%u: increase bits.",
                                  rawHi, d_.buttonBits);
                    v.push_back({Status::Mismatch, b});
                }
                if (d_.factoryButtons > 0 && hi >= 0) {
                    if (hi + 1 > d_.factoryButtons) {
                        std::snprintf(b, sizeof b,
                                      "Highest bit %d (button %d) exceeds factory_buttons=%d: bits are "
                                      "not numbered sequentially or the count is wrong.",
                                      hi, hi + 1, d_.factoryButtons);
                        v.push_back({Status::Warn, b});
                    } else {
                        int n = popcount32(seenBits_);
                        bool all = n >= d_.factoryButtons;
                        std::snprintf(b, sizeof b,
                                      "%d distinct button bit(s) seen, highest %d, factory_buttons=%d%s.",
                                      n, hi, d_.factoryButtons,
                                      all ? "" : " (not all buttons pressed yet)");
                        v.push_back({all ? Status::Ok : Status::Warn, b});
                    }
                }
            }
        }
        return v;
    }

private:
    struct Stat {
        size_t count = 0, minLen = 0, maxLen = 0;
    };
    static double limit(const spn::HidAxis &a) {
        double raw = 32767;
        switch (a.type) {
            case spn::HidType::S8: raw = 127; break;
            case spn::HidType::U8: raw = 255; break;
            case spn::HidType::U16LE:
            case spn::HidType::U16BE: raw = 65535; break;
            case spn::HidType::S32LE: raw = 2147483647.0; break;
            default: break;
        }
        return raw * a.scale;
    }
    size_t requiredLength(unsigned id) const {
        size_t need = 0;
        for (int a = 0; a < 6; ++a)
            if (d_.axes[a].present && d_.axes[a].reportId == id)
                need = std::max<size_t>(need, 1 + d_.axes[a].offset + typeSize(d_.axes[a].type));
        if (d_.hasButtons && d_.buttonReport == id)
            need = std::max<size_t>(need, 1 + d_.buttonOffset + (d_.buttonBits + 7) / 8);
        return need;
    }

    const spn::HidDescriptor &d_;
    std::map<unsigned, unsigned> descLen_;
    std::map<unsigned, Stat> perId_;
    size_t reports_ = 0;
    bool everAxis_[6] = {false, false, false, false, false, false};
    double minV_[6] = {0, 0, 0, 0, 0, 0};
    double maxV_[6] = {0, 0, 0, 0, 0, 0};
    double cur_[6] = {0, 0, 0, 0, 0, 0};
    uint32_t seenBits_ = 0, rawBits_ = 0, curButtons_ = 0;
};

// Guided mode: the axis that deviates most between two snapshots, or -1 if none changed
// by at least `threshold`.
inline int dominantAxis(const double *start, const double *now, double threshold) {
    int best = -1;
    double bestD = threshold;
    for (int a = 0; a < 6; ++a) {
        double dlt = std::fabs(now[a] - start[a]);
        if (dlt >= bestD) {
            bestD = dlt;
            best = a;
        }
    }
    return best;
}

}  // namespace hidprobe
