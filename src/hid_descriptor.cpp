// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "hid_descriptor.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace spn {
namespace {

const char *const kAxisNames[6] = {"tx", "ty", "tz", "rx", "ry", "rz"};
constexpr size_t kMaxIds = 16;

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

bool parseUInt(const std::string &s, unsigned long max, unsigned &out) {
    if (s.empty() || s[0] == '-' || s[0] == '+') return false;
    char *end = nullptr;
    errno = 0;
    unsigned long v = std::strtoul(s.c_str(), &end, 0);
    if (errno != 0 || *end != '\0' || v > max) return false;
    out = static_cast<unsigned>(v);
    return true;
}

bool parseType(const std::string &s, HidType &t) {
    static const struct { const char *n; HidType t; } k[] = {
        {"s8", HidType::S8},         {"u8", HidType::U8},         {"s16le", HidType::S16LE},
        {"s16be", HidType::S16BE},   {"u16le", HidType::U16LE},   {"u16be", HidType::U16BE},
        {"s32le", HidType::S32LE},
    };
    for (const auto &e : k)
        if (s == e.n) { t = e.t; return true; }
    return false;
}

size_t typeSize(HidType t) {
    switch (t) {
        case HidType::S8: case HidType::U8: return 1;
        case HidType::S32LE: return 4;
        default: return 2;
    }
}

// Splits "k=v k=v ..." into a lookup; returns false on malformed tokens or duplicates.
bool splitKv(const std::string &s, std::vector<std::pair<std::string, std::string>> &kv) {
    std::istringstream is(s);
    std::string tok;
    while (is >> tok) {
        size_t eq = tok.find('=');
        if (eq == std::string::npos || eq == 0) return false;
        std::string k = tok.substr(0, eq);
        for (const auto &e : kv)
            if (e.first == k) return false;
        kv.emplace_back(k, tok.substr(eq + 1));
    }
    return true;
}

const std::string *find(const std::vector<std::pair<std::string, std::string>> &kv, const char *k) {
    for (const auto &e : kv)
        if (e.first == k) return &e.second;
    return nullptr;
}

bool fail(std::string &err, int line, const std::string &msg) {
    err = "line " + std::to_string(line) + ": " + msg;
    return false;
}

double readValue(const uint8_t *p, HidType t) {
    switch (t) {
        case HidType::S8: return static_cast<int8_t>(p[0]);
        case HidType::U8: return p[0];
        case HidType::S16LE: return static_cast<int16_t>(p[0] | (p[1] << 8));
        case HidType::S16BE: return static_cast<int16_t>((p[0] << 8) | p[1]);
        case HidType::U16LE: return static_cast<uint16_t>(p[0] | (p[1] << 8));
        case HidType::U16BE: return static_cast<uint16_t>((p[0] << 8) | p[1]);
        case HidType::S32LE:
            return static_cast<int32_t>(static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
                                        (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24));
    }
    return 0;
}

}  // namespace

bool parseHidDescriptor(const std::string &text, HidDescriptor &out, std::string &err) {
    HidDescriptor d;
    std::istringstream is(text);
    std::string raw;
    int ln = 0;
    while (std::getline(is, raw)) {
        ++ln;
        // Comment rule: '#' or ';' starts a comment at the start of a line or after
        // whitespace. The value of `name` is taken verbatim (only full-line comments),
        // so "name = Foo #2" is kept whole.
        {
            size_t first = raw.find_first_not_of(" \t\r");
            const bool isName = first != std::string::npos && raw.compare(first, 4, "name") == 0 &&
                                raw.find_first_not_of(" \t", first + 4) != std::string::npos &&
                                raw[raw.find_first_not_of(" \t", first + 4)] == '=';
            for (size_t i = 0; i < raw.size(); ++i) {
                if ((raw[i] == '#' || raw[i] == ';') &&
                    (i == first || (!isName && (i == 0 || raw[i - 1] == ' ' || raw[i - 1] == '\t')))) {
                    raw.erase(i);
                    break;
                }
            }
        }
        std::string line = trim(raw);
        if (line.empty()) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) return fail(err, ln, "expected key = value");
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key == "name") {
            if (val.empty() || val.size() > 128) return fail(err, ln, "invalid name");
            for (char c : val)
                if (static_cast<unsigned char>(c) < 0x20) return fail(err, ln, "invalid name");
            d.name = val;
        } else if (key == "id") {
            // id = 0x256f:0xc635
            size_t colon = val.find(':');
            unsigned vid, pid;
            if (colon == std::string::npos || !parseUInt(trim(val.substr(0, colon)), 0xffff, vid) ||
                !parseUInt(trim(val.substr(colon + 1)), 0xffff, pid))
                return fail(err, ln, "id must be vid:pid (16-bit numbers)");
            if (d.ids.size() >= kMaxIds) return fail(err, ln, "too many id entries");
            d.ids.emplace_back(vid, pid);
        } else if (key == "factory_buttons") {
            unsigned n;
            if (!parseUInt(val, 32, n)) return fail(err, ln, "factory_buttons must be 0..32");
            d.factoryButtons = static_cast<int>(n);
        } else if (key.size() > 5 && key.compare(0, 5, "axis.") == 0) {
            std::string an = key.substr(5);
            int idx = -1;
            for (int i = 0; i < 6; ++i)
                if (an == kAxisNames[i]) idx = i;
            if (idx < 0) return fail(err, ln, "unknown axis '" + an + "'");
            if (d.axes[idx].present) return fail(err, ln, "axis defined twice");
            std::vector<std::pair<std::string, std::string>> kv;
            if (!splitKv(val, kv)) return fail(err, ln, "malformed axis options");
            const std::string *r = find(kv, "report"), *o = find(kv, "offset"), *t = find(kv, "type"),
                              *s = find(kv, "scale");
            if (!r || !o || !t || kv.size() != (s ? 4u : 3u))
                return fail(err, ln, "axis needs report, offset, type and optional scale");
            HidAxis a;
            if (!parseUInt(*r, 255, a.reportId)) return fail(err, ln, "invalid report id");
            if (!parseUInt(*o, 255, a.offset)) return fail(err, ln, "invalid offset");
            if (!parseType(*t, a.type)) return fail(err, ln, "invalid type");
            if (s) {
                char *end = nullptr;
                a.scale = std::strtod(s->c_str(), &end);
                if (s->empty() || *end != '\0' || !std::isfinite(a.scale) || a.scale == 0.0)
                    return fail(err, ln, "invalid scale");
            }
            a.present = true;
            d.axes[idx] = a;
        } else if (key == "buttons") {
            if (d.hasButtons) return fail(err, ln, "buttons defined twice");
            std::vector<std::pair<std::string, std::string>> kv;
            if (!splitKv(val, kv) || kv.size() != 3) return fail(err, ln, "buttons needs report, offset, bits");
            const std::string *r = find(kv, "report"), *o = find(kv, "offset"), *b = find(kv, "bits");
            unsigned bits;
            if (!r || !o || !b) return fail(err, ln, "buttons needs report, offset, bits");
            if (!parseUInt(*r, 255, d.buttonReport)) return fail(err, ln, "invalid report id");
            if (!parseUInt(*o, 255, d.buttonOffset)) return fail(err, ln, "invalid offset");
            if (!parseUInt(*b, 32, bits) || bits == 0) return fail(err, ln, "bits must be 1..32");
            d.buttonBits = bits;
            d.hasButtons = true;
        } else {
            return fail(err, ln, "unknown key '" + key + "'");
        }
    }
    if (d.name.empty()) { err = "missing name"; return false; }
    if (d.ids.empty()) { err = "missing id"; return false; }
    bool anyAxis = false;
    for (const HidAxis &a : d.axes) anyAxis = anyAxis || a.present;
    if (!anyAxis && !d.hasButtons) { err = "no axes or buttons defined"; return false; }
    if (d.factoryButtons > 0 && (!d.hasButtons || static_cast<unsigned>(d.factoryButtons) > d.buttonBits)) {
        err = "factory_buttons exceeds button bits";
        return false;
    }
    out = d;
    return true;
}

bool loadHidDescriptorFile(const std::string &path, HidDescriptor &out, std::string &err) {
    int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) { err = std::strerror(errno); return false; }
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        err = "not a regular file";
        return false;
    }
    if (st.st_size < 0 || static_cast<size_t>(st.st_size) > kMaxDescriptorBytes) {
        ::close(fd);
        err = "file too large (max " + std::to_string(kMaxDescriptorBytes) + " bytes)";
        return false;
    }
    std::string data;
    char buf[4096];
    for (;;) {
        ssize_t n = ::read(fd, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        data.append(buf, static_cast<size_t>(n));
        if (data.size() > kMaxDescriptorBytes) {
            ::close(fd);
            err = "file too large (max " + std::to_string(kMaxDescriptorBytes) + " bytes)";
            return false;
        }
    }
    ::close(fd);
    return parseHidDescriptor(data, out, err);
}

bool loadHidDescriptorDir(const std::string &dir, std::vector<HidDescriptor> &out,
                          std::vector<std::string> &errors) {
    DIR *dp = ::opendir(dir.c_str());
    if (!dp) return false;
    std::vector<std::string> names;
    while (struct dirent *e = ::readdir(dp)) {
        std::string n = e->d_name;
        if (n.size() > 5 && n.compare(n.size() - 5, 5, ".conf") == 0) names.push_back(n);
    }
    ::closedir(dp);
    std::sort(names.begin(), names.end());
    if (names.size() > kMaxDescriptorFiles) {
        errors.push_back(dir + ": too many files, only the first " + std::to_string(kMaxDescriptorFiles) +
                         " are considered");
        names.resize(kMaxDescriptorFiles);
    }
    for (const std::string &n : names) {
        HidDescriptor d;
        std::string err;
        if (loadHidDescriptorFile(dir + "/" + n, d, err))
            out.push_back(d);
        else
            errors.push_back(n + ": " + err);
    }
    return true;
}

bool decodeHidReport(const HidDescriptor &d, const uint8_t *data, size_t len, HidDecoded &out) {
    out = HidDecoded();
    if (!data || len < 1) return false;
    const unsigned rid = data[0];
    const uint8_t *payload = data + 1;
    const size_t plen = len - 1;
    bool any = false;
    for (int i = 0; i < 6; ++i) {
        const HidAxis &a = d.axes[i];
        if (!a.present || a.reportId != rid) continue;
        if (a.offset + typeSize(a.type) > plen) continue;
        out.axis[i] = readValue(payload + a.offset, a.type) * a.scale;
        out.axisSet[i] = true;
        any = true;
    }
    if (d.hasButtons && d.buttonReport == rid) {
        const size_t nbytes = (d.buttonBits + 7) / 8;
        if (d.buttonOffset + nbytes <= plen) {
            uint32_t v = 0;
            for (size_t b = 0; b < nbytes; ++b) v |= static_cast<uint32_t>(payload[d.buttonOffset + b]) << (8 * b);
            if (d.buttonBits < 32) v &= (1u << d.buttonBits) - 1u;
            out.buttons = v;
            out.buttonsSet = true;
            any = true;
        }
    }
    return any;
}

}  // namespace spn
