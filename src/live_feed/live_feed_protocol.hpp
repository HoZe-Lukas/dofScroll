// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Live feed wire protocol (version 2), see docs/port-architecture.md "Live feed". Pure logic, no
// OS headers: the encoder (daemon) and the validating parser (UI client) are unit-tested.
//
// One-way (daemon -> client), binary, little-endian. A stream is a sequence of frames:
//   offset 0  4 bytes  magic "DFLV"
//   offset 4  u8       protocol version (2; version 1 had no device info frame)
//   offset 5  u8       frame type (1 = sample, 2 = device info, 3 = sample source; other types with a valid
//                      length are skipped, so a parser without type 3 still works)
//   offset 6  u16      payload length (<= kMaxPayload)
//   offset 8  payload
// Sample payload (type 1), exactly 46 + mode_len bytes:
//   0  u32 seq          wraps around
//   4  i32 axes[6]      x y z rx ry rz as the daemon read them (before DSP)
//   28 u32 buttons      bit n = hardware button index n
//   32 f32 v, h, zoom   channel outputs after DSP (mode_runner Info: rate_v, rate_h, twist)
//   44 u8  flags        bit0 fresh, bit1 zooming, bit2 nav, bit3 enabled, bit4 idle
//   45 u8  mode_len     0..32
//   46     mode id      printable ASCII without space
// Device info payload (type 2), sent right after connect and on every device-list change:
//   0  u8 count        0..kLiveMaxDevices
//   per device (9 + name_len bytes):
//      u8 id, u16 vendor, u16 product (0 = unknown), u8 reportedButtons, u8 factoryButtons (0..32:
//      the number the UI must use for configurable buttons), u8 axes (0..6), u8 name_len (0..64),
//      name: strict UTF-8 without control characters
// Sample source payload (type 3), exactly 1 byte, sent right before the sample frame it belongs to:
//   0  u8 id           the device id (see device info) the following sample was read from;
//                      no source frame = the device of the sample is unknown
// The parser treats every byte as untrusted: bad magic, other version, oversize or inconsistent
// length, out-of-range value, NaN/Inf, reserved flag bits or control characters end the stream.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "common.hpp"

namespace spn {

constexpr uint8_t kLiveVersion = 2;
constexpr size_t kLiveHeaderBytes = 8;
constexpr size_t kLiveSampleFixed = 46;
constexpr size_t kLiveMaxMode = 32;
constexpr size_t kLiveMaxName = 64;
constexpr size_t kLiveMaxDevices = 8;
constexpr size_t kLiveDeviceFixed = 9;
constexpr size_t kLiveMaxInfoPayload = 1 + kLiveMaxDevices * (kLiveDeviceFixed + kLiveMaxName);
constexpr size_t kLiveMaxSamplePayload = kLiveSampleFixed + kLiveMaxMode;
constexpr size_t kLiveMaxPayload = kLiveMaxInfoPayload > kLiveMaxSamplePayload ? kLiveMaxInfoPayload : kLiveMaxSamplePayload;
constexpr size_t kLiveMaxFrame = kLiveHeaderBytes + kLiveMaxPayload;
constexpr uint8_t kLiveTypeSample = 1;
constexpr uint8_t kLiveTypeDevices = 2;
constexpr uint8_t kLiveTypeSource = 3;
constexpr size_t kLiveSourcePayload = 1;
constexpr int kLiveNoSource = -1;  // LiveSample::source: the device of the sample is unknown
constexpr int kLiveMaxButtons = 32;
constexpr int kLiveAxisLimit = 100000;     // real devices stay near +-350
constexpr double kLiveRateLimit = 1.0e6;   // channel outputs are steps per second

// One device as the daemon knows it.
struct LiveDevice {
    uint8_t id = 0;
    uint16_t vendor = 0, product = 0;  // 0 = unknown
    int reportedButtons = 0;
    int factoryButtons = 0;            // factoryButtonCount(name, reported)
    int axes = 6;
    std::string name;                  // UTF-8
    bool operator==(const LiveDevice &o) const {
        return id == o.id && vendor == o.vendor && product == o.product && reportedButtons == o.reportedButtons &&
               factoryButtons == o.factoryButtons && axes == o.axes && name == o.name;
    }
};

// One sample: what the UI needs to draw the axes view and to run the axis assistant.
struct LiveSample {
    uint32_t seq = 0;  // filled by the server
    RawAxes axes{};
    uint32_t buttons = 0;
    float v = 0, h = 0, zoom = 0;
    bool fresh = false, zooming = false, nav = false, enabled = true, idle = false;
    std::string mode;
    // Id of the device the sample comes from (LiveDevice::id), kLiveNoSource = unknown. Sent as a
    // separate source frame before the sample (0..255, anything else is not transmitted).
    int source = kLiveNoSource;
    // Daemon-side only (sent as a separate info frame): bump devicesRev whenever `devices` changes.
    std::vector<LiveDevice> devices;
    uint32_t devicesRev = 0;
    bool active = true;  // daemon-side only, not transmitted: false = feed switched off (LIVE_FEED = 0)
};

namespace live_detail {
inline void put16(std::vector<uint8_t> &b, uint32_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
}
inline void put32(std::vector<uint8_t> &b, uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
inline uint32_t get32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}
inline void putF(std::vector<uint8_t> &b, float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    put32(b, u);
}
inline float getF(const uint8_t *p) {
    const uint32_t u = get32(p);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}
inline bool validMode(const std::string &m) {
    if (m.size() > kLiveMaxMode) return false;
    for (char c : m)
        if (c <= 0x20 || c >= 0x7f) return false;
    return true;
}
// Length of the valid UTF-8 sequence at p (0 = invalid). Strict: no overlong forms, surrogates or
// code points above U+10FFFF; control characters (C0, DEL, C1) are invalid.
inline size_t utf8Seq(const unsigned char *p, size_t n) {
    if (n == 0) return 0;
    const unsigned c = p[0];
    if (c < 0x20 || c == 0x7f) return 0;
    if (c < 0x80) return 1;
    auto cont = [&](size_t i) { return i < n && (p[i] & 0xC0) == 0x80; };
    if (c >= 0xC2 && c <= 0xDF) return cont(1) && !(c == 0xC2 && p[1] < 0xA0) ? 2 : 0;  // C1 controls
    if (c >= 0xE0 && c <= 0xEF) {
        if (!cont(1) || !cont(2)) return 0;
        if (c == 0xE0 && p[1] < 0xA0) return 0;
        if (c == 0xED && p[1] > 0x9F) return 0;
        return 3;
    }
    if (c >= 0xF0 && c <= 0xF4) {
        if (!cont(1) || !cont(2) || !cont(3)) return 0;
        if (c == 0xF0 && p[1] < 0x90) return 0;
        if (c == 0xF4 && p[1] > 0x8F) return 0;
        return 4;
    }
    return 0;
}
inline bool validName(const std::string &s) {
    if (s.size() > kLiveMaxName) return false;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(s.data());
    for (size_t i = 0; i < s.size();) {
        const size_t l = utf8Seq(p + i, s.size() - i);
        if (!l) return false;
        i += l;
    }
    return true;
}
// Replaces invalid bytes and control characters by '?', truncates to kLiveMaxName at a code point boundary.
inline std::string sanitizeName(const std::string &in) {
    std::string out;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(in.data());
    for (size_t i = 0; i < in.size();) {
        size_t l = utf8Seq(p + i, in.size() - i);
        const bool bad = l == 0;
        if (bad) l = 1;
        if (out.size() + (bad ? 1 : l) > kLiveMaxName) break;
        if (bad) out.push_back('?');
        else out.append(in, i, l);
        i += l;
    }
    return out;
}
inline bool validRate(float f) { return std::isfinite(f) && std::fabs(static_cast<double>(f)) <= kLiveRateLimit; }
}  // namespace live_detail

// Encodes one sample frame. Values that the parser would reject are clamped (axes, rates) or
// replaced (mode id), so the encoder never produces an invalid stream.
inline std::vector<uint8_t> encodeLiveSample(const LiveSample &s) {
    using namespace live_detail;
    std::string mode;
    for (char c : s.mode) {
        if (mode.size() >= kLiveMaxMode) break;
        mode.push_back((c > 0x20 && c < 0x7f) ? c : '?');
    }
    std::vector<uint8_t> b;
    b.reserve(kLiveHeaderBytes + kLiveSampleFixed + mode.size());
    b.push_back('D'), b.push_back('F'), b.push_back('L'), b.push_back('V');
    b.push_back(kLiveVersion);
    b.push_back(kLiveTypeSample);
    put16(b, static_cast<uint32_t>(kLiveSampleFixed + mode.size()));
    put32(b, s.seq);
    for (int a : s.axes) {
        const int c = a > kLiveAxisLimit ? kLiveAxisLimit : (a < -kLiveAxisLimit ? -kLiveAxisLimit : a);
        put32(b, static_cast<uint32_t>(c));
    }
    put32(b, s.buttons);
    for (float f : {s.v, s.h, s.zoom}) putF(b, validRate(f) ? f : 0.0f);
    b.push_back(static_cast<uint8_t>((s.fresh ? 1 : 0) | (s.zooming ? 2 : 0) | (s.nav ? 4 : 0) | (s.enabled ? 8 : 0) |
                                     (s.idle ? 16 : 0)));
    b.push_back(static_cast<uint8_t>(mode.size()));
    b.insert(b.end(), mode.begin(), mode.end());
    return b;
}

// Encodes the source frame (type 3) for a device id; empty for kLiveNoSource or an id outside 0..255.
inline std::vector<uint8_t> encodeLiveSource(int id) {
    std::vector<uint8_t> b;
    if (id < 0 || id > 255) return b;
    b.push_back('D'), b.push_back('F'), b.push_back('L'), b.push_back('V');
    b.push_back(kLiveVersion);
    b.push_back(kLiveTypeSource);
    live_detail::put16(b, static_cast<uint32_t>(kLiveSourcePayload));
    b.push_back(static_cast<uint8_t>(id));
    return b;
}

// Encodes the device info frame (at most kLiveMaxDevices devices, names sanitized, counts clamped).
inline std::vector<uint8_t> encodeLiveDevices(const std::vector<LiveDevice> &devs) {
    using namespace live_detail;
    auto clampi = [](int v, int hi) { return static_cast<uint8_t>(v < 0 ? 0 : (v > hi ? hi : v)); };
    const size_t n = devs.size() < kLiveMaxDevices ? devs.size() : kLiveMaxDevices;
    std::vector<uint8_t> body;
    body.push_back(static_cast<uint8_t>(n));
    for (size_t i = 0; i < n; i++) {
        const LiveDevice &d = devs[i];
        const std::string name = sanitizeName(d.name);
        body.push_back(d.id);
        put16(body, d.vendor);
        put16(body, d.product);
        body.push_back(clampi(d.reportedButtons, kLiveMaxButtons));
        body.push_back(clampi(d.factoryButtons, kLiveMaxButtons));
        body.push_back(clampi(d.axes, AX_COUNT));
        body.push_back(static_cast<uint8_t>(name.size()));
        body.insert(body.end(), name.begin(), name.end());
    }
    std::vector<uint8_t> b;
    b.reserve(kLiveHeaderBytes + body.size());
    b.push_back('D'), b.push_back('F'), b.push_back('L'), b.push_back('V');
    b.push_back(kLiveVersion);
    b.push_back(kLiveTypeDevices);
    put16(b, static_cast<uint32_t>(body.size()));
    b.insert(b.end(), body.begin(), body.end());
    return b;
}

enum class LiveParseError { Ok, EBadMagic, EVersionMismatch, EOversize, EBadLength, EBadPayload, EBufferOverflow };

inline const char *liveParseErrorText(LiveParseError e) {
    switch (e) {
        case LiveParseError::Ok: return "none";
        case LiveParseError::EBadMagic: return "bad magic";
        case LiveParseError::EVersionMismatch: return "protocol version mismatch (this UI speaks live feed protocol 2: update the daemon and the UI together)";
        case LiveParseError::EOversize: return "frame too large";
        case LiveParseError::EBadLength: return "inconsistent frame length";
        case LiveParseError::EBadPayload: return "invalid field value";
        case LiveParseError::EBufferOverflow: return "input buffer overflow";
    }
    return "?";
}

// Incremental stream parser with a bounded buffer (at most kLiveMaxFrame + one feed() chunk).
// After an error the parser stays in the error state until reset().
class LiveFrameParser {
public:
    // false: the buffer would exceed its bound (error state, input dropped).
    bool feed(const uint8_t *data, size_t n) {
        if (err_ != LiveParseError::Ok) return false;
        if (buf_.size() + n > kLiveMaxFrame + kMaxChunk) return fail(LiveParseError::EBufferOverflow);
        buf_.insert(buf_.end(), data, data + n);
        return true;
    }
    static constexpr size_t kMaxChunk = 4096;

    // true: *out holds the next sample. false: need more bytes or error() != None.
    bool next(LiveSample *out) {
        while (err_ == LiveParseError::Ok) {
            if (buf_.size() < kLiveHeaderBytes) {
                checkPrefix();
                return false;
            }
            if (std::memcmp(buf_.data(), "DFLV", 4) != 0) return fail(LiveParseError::EBadMagic);
            if (buf_[4] != kLiveVersion) return fail(LiveParseError::EVersionMismatch);
            const size_t len = static_cast<size_t>(buf_[6]) | static_cast<size_t>(buf_[7]) << 8;
            if (len > kLiveMaxPayload) return fail(LiveParseError::EOversize);
            if (buf_.size() < kLiveHeaderBytes + len) return false;
            const uint8_t type = buf_[5];
            if (type == kLiveTypeSample && !decode(buf_.data() + kLiveHeaderBytes, len, out)) return false;
            if (type == kLiveTypeDevices && !decodeDevices(buf_.data() + kLiveHeaderBytes, len)) return false;
            if (type == kLiveTypeSource) {
                if (len != kLiveSourcePayload) return fail(LiveParseError::EBadLength);
                source_ = buf_[kLiveHeaderBytes];
            }
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(kLiveHeaderBytes + len));
            if (type == kLiveTypeSample) return true;
        }
        return false;
    }

    // Latest device info received (empty until the first info frame); infoCount() counts frames.
    const std::vector<LiveDevice> &devices() const { return devices_; }
    uint32_t infoCount() const { return infoCount_; }

    LiveParseError error() const { return err_; }
    void reset() {
        buf_.clear();
        devices_.clear();
        infoCount_ = 0;
        source_ = kLiveNoSource;
        err_ = LiveParseError::Ok;
    }
    size_t buffered() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
    std::vector<LiveDevice> devices_;
    uint32_t infoCount_ = 0;
    int source_ = kLiveNoSource;  // from the last source frame, consumed by the next sample
    LiveParseError err_ = LiveParseError::Ok;

    bool fail(LiveParseError e) {
        err_ = e;
        buf_.clear();
        return false;
    }
    // Reject garbage early: as soon as the first bytes cannot be the start of a frame.
    void checkPrefix() {
        static const char magic[] = "DFLV";
        const size_t n = buf_.size() < 4 ? buf_.size() : 4;
        if (n && std::memcmp(buf_.data(), magic, n) != 0) fail(LiveParseError::EBadMagic);
        else if (buf_.size() > 4 && buf_[4] != kLiveVersion) fail(LiveParseError::EVersionMismatch);
    }
    bool decodeDevices(const uint8_t *p, size_t len) {
        using namespace live_detail;
        if (len < 1) return fail(LiveParseError::EBadLength);
        const size_t n = p[0];
        if (n > kLiveMaxDevices) return fail(LiveParseError::EBadPayload);
        std::vector<LiveDevice> devs;
        size_t pos = 1;
        for (size_t i = 0; i < n; i++) {
            if (len - pos < kLiveDeviceFixed) return fail(LiveParseError::EBadLength);
            LiveDevice d;
            d.id = p[pos];
            d.vendor = static_cast<uint16_t>(p[pos + 1] | p[pos + 2] << 8);
            d.product = static_cast<uint16_t>(p[pos + 3] | p[pos + 4] << 8);
            d.reportedButtons = p[pos + 5];
            d.factoryButtons = p[pos + 6];
            d.axes = p[pos + 7];
            const size_t nl = p[pos + 8];
            pos += kLiveDeviceFixed;
            if (d.reportedButtons > kLiveMaxButtons || d.factoryButtons > kLiveMaxButtons || d.axes > AX_COUNT ||
                nl > kLiveMaxName)
                return fail(LiveParseError::EBadPayload);
            if (len - pos < nl) return fail(LiveParseError::EBadLength);
            d.name.assign(reinterpret_cast<const char *>(p + pos), nl);
            pos += nl;
            if (!validName(d.name)) return fail(LiveParseError::EBadPayload);
            devs.push_back(std::move(d));
        }
        if (pos != len) return fail(LiveParseError::EBadLength);
        devices_ = std::move(devs);
        infoCount_++;
        return true;
    }
    bool decode(const uint8_t *p, size_t len, LiveSample *out) {
        using namespace live_detail;
        if (len < kLiveSampleFixed) return fail(LiveParseError::EBadLength);
        const size_t modeLen = p[45];
        if (modeLen > kLiveMaxMode || len != kLiveSampleFixed + modeLen) return fail(LiveParseError::EBadLength);
        LiveSample s;
        s.seq = get32(p);
        for (int i = 0; i < AX_COUNT; i++) {
            const int32_t a = static_cast<int32_t>(get32(p + 4 + 4 * i));
            if (a > kLiveAxisLimit || a < -kLiveAxisLimit) return fail(LiveParseError::EBadPayload);
            s.axes[i] = a;
        }
        s.buttons = get32(p + 28);
        s.v = getF(p + 32);
        s.h = getF(p + 36);
        s.zoom = getF(p + 40);
        if (!validRate(s.v) || !validRate(s.h) || !validRate(s.zoom)) return fail(LiveParseError::EBadPayload);
        const uint8_t fl = p[44];
        if (fl & 0xE0) return fail(LiveParseError::EBadPayload);
        s.fresh = fl & 1, s.zooming = fl & 2, s.nav = fl & 4, s.enabled = fl & 8, s.idle = fl & 16;
        s.mode.assign(reinterpret_cast<const char *>(p + kLiveSampleFixed), modeLen);
        if (!validMode(s.mode)) return fail(LiveParseError::EBadPayload);
        s.source = source_;
        source_ = kLiveNoSource;
        *out = std::move(s);
        return true;
    }
};

}  // namespace spn
