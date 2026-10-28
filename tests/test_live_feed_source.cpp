// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Live feed frame type 3 (sample source, issue #139) beyond tests/test_live_feed.cpp: what the real server puts on the
// wire for each source value, a source frame that is separated from its sample by a device-info frame or by an unknown
// frame type, every split point of the byte stream, and a source id outside 0..255 (unknown, never wrapped).
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <deque>
#include <memory>
#include <vector>

#include "live_feed/live_feed.hpp"

using namespace spn;

namespace {
constexpr int64_t MS = 1000000;

struct FakeConn : FeedConn {
    std::vector<uint8_t> out;
    long read(uint8_t *, size_t) override { return 0; }
    long write(const uint8_t *b, size_t n) override {
        out.insert(out.end(), b, b + n);
        return static_cast<long>(n);
    }
};
struct FakeListener : FeedListener {
    std::deque<std::unique_ptr<FakeConn>> waiting;
    bool open(std::string *) override { return true; }
    std::unique_ptr<FeedConn> accept() override {
        if (waiting.empty()) return nullptr;
        auto c = std::move(waiting.front());
        waiting.pop_front();
        return c;
    }
};

LiveSample sample(int id) {
    LiveSample s;
    s.mode = "normal";
    s.source = id;
    return s;
}

// The ids of the samples the parser yields from `bytes`.
std::vector<int> ids(const std::vector<uint8_t> &bytes) {
    LiveFrameParser p;
    LiveSample o;
    std::vector<int> r;
    assert(p.feed(bytes.data(), bytes.size()));
    while (p.next(&o)) r.push_back(o.source);
    assert(p.error() == LiveParseError::Ok);
    return r;
}
}  // namespace

static void testServerWire() {
    auto *lp = new FakeListener;
    std::unique_ptr<FeedListener> l(lp);
    LiveFeedServer srv(std::move(l));
    assert(srv.start(nullptr));
    auto c = std::make_unique<FakeConn>();
    FakeConn *cp = c.get();
    lp->waiting.push_back(std::move(c));
    // 100 ms between published samples: every one is sent (the rate limit is about 30 per second)
    const int wanted[] = {0, 7, 255, kLiveNoSource, 3, 300, -7, 255, 0};
    int64_t t = 1000 * MS;
    for (int s : wanted) {
        srv.publish(sample(s), t);
        t += 100 * MS;
    }
    const std::vector<int> got = ids(cp->out);
    assert(got.size() == sizeof wanted / sizeof wanted[0]);
    for (size_t i = 0; i < got.size(); i++) {
        const int w = wanted[i];
        assert(got[i] == (w >= 0 && w <= 255 ? w : kLiveNoSource));  // outside 0..255: unknown, never wrapped
    }
    // a type 3 frame is on the wire only for a known device
    size_t type3 = 0;
    for (size_t i = 0; i + kLiveHeaderBytes <= cp->out.size();) {
        assert(cp->out[i] == 'D');
        const size_t len = size_t(cp->out[i + 6]) | size_t(cp->out[i + 7]) << 8;
        type3 += cp->out[i + 5] == kLiveTypeSource;
        i += kLiveHeaderBytes + len;
    }
    assert(type3 == 6);  // 0, 7, 255, 3, 255, 0
}

static void testInterleavedFrames() {
    const LiveSample s = sample(kLiveNoSource);
    std::vector<uint8_t> f = encodeLiveSource(9);
    LiveDevice d;
    d.id = 9, d.name = "Pro";
    const std::vector<uint8_t> dev = encodeLiveDevices({d});
    f.insert(f.end(), dev.begin(), dev.end());  // device info between the id and its sample
    std::vector<uint8_t> unknown = encodeLiveSource(1);
    unknown[5] = 77;  // a frame type of a later version
    f.insert(f.end(), unknown.begin(), unknown.end());
    const std::vector<uint8_t> a = encodeLiveSample(s);
    f.insert(f.end(), a.begin(), a.end());
    assert(ids(f) == std::vector<int>{9});
    // two type 3 frames in a row: the last one wins
    std::vector<uint8_t> g = encodeLiveSource(4);
    const std::vector<uint8_t> g2 = encodeLiveSource(5);
    g.insert(g.end(), g2.begin(), g2.end());
    g.insert(g.end(), a.begin(), a.end());
    assert(ids(g) == std::vector<int>{5});
    // a type 3 frame at the end of the stream has no sample: nothing is yielded
    assert(ids(encodeLiveSource(2)).empty());
}

static void testEverySplitPoint() {
    std::vector<uint8_t> f;
    for (int id : {0, 200, kLiveNoSource, 17}) {
        const std::vector<uint8_t> a = encodeLiveSource(id), b = encodeLiveSample(sample(id));
        f.insert(f.end(), a.begin(), a.end());
        f.insert(f.end(), b.begin(), b.end());
    }
    const std::vector<int> want = {0, 200, kLiveNoSource, 17};
    for (size_t cut = 0; cut <= f.size(); cut++) {
        LiveFrameParser p;
        LiveSample o;
        std::vector<int> got;
        assert(p.feed(f.data(), cut));
        while (p.next(&o)) got.push_back(o.source);
        assert(p.feed(f.data() + cut, f.size() - cut));
        while (p.next(&o)) got.push_back(o.source);
        assert(got == want && p.error() == LiveParseError::Ok);
    }
}

int main() {
    testServerWire();
    testInterleavedFrames();
    testEverySplitPoint();
    std::puts("test_live_feed_source: OK");
    return 0;
}
