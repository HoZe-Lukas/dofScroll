// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Live feed wire protocol (version 1), see docs/port-architecture.md "Live feed". Pure logic, no
// OS headers: the encoder (daemon) and the validating parser (UI client) are unit-tested.
//
// One-way (daemon -> client), binary, little-endian. A stream is a sequence of frames:
//   offset 0  4 bytes  magic "DFLV"
//   offset 4  u8       protocol version (1)
//   offset 5  u8       frame type (1 = sample; other types with a valid length are skipped)
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

constexpr uint8_t kLiveVersion = 1;
constexpr size_t kLiveHeaderBytes = 8;
constexpr size_t kLiveSampleFixed = 46;
constexpr size_t kLiveMaxMode = 32;
constexpr size_t kLiveMaxPayload = kLiveSampleFixed + kLiveMaxMode;
constexpr size_t kLiveMaxFrame = kLiveHeaderBytes + kLiveMaxPayload;
constexpr uint8_t kLiveTypeSample = 1;
constexpr int kLiveAxisLimit = 100000;     // real devices stay near +-350
constexpr double kLiveRateLimit = 1.0e6;   // channel outputs are steps per second

// One sample: what the UI needs to draw the axes view and to run the axis assistant.
struct LiveSample {
    uint32_t seq = 0;  // filled by the server
    RawAxes axes{};
    uint32_t buttons = 0;
    float v = 0, h = 0, zoom = 0;
    bool fresh = false, zooming = false, nav = false, enabled = true, idle = false;
    std::string mode;
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

enum class LiveParseError { Ok, EBadMagic, EVersionMismatch, EOversize, EBadLength, EBadPayload, EBufferOverflow };

inline const char *liveParseErrorText(LiveParseError e) {
    switch (e) {
        case LiveParseError::Ok: return "none";
        case LiveParseError::EBadMagic: return "bad magic";
        case LiveParseError::EVersionMismatch: return "protocol version mismatch";
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
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(kLiveHeaderBytes + len));
            if (type == kLiveTypeSample) return true;
        }
        return false;
    }

    LiveParseError error() const { return err_; }
    void reset() {
        buf_.clear();
        err_ = LiveParseError::Ok;
    }
    size_t buffered() const { return buf_.size(); }

private:
    std::vector<uint8_t> buf_;
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
        *out = std::move(s);
        return true;
    }
};

}  // namespace spn
