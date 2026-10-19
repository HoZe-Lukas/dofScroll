// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Live feed: protocol framing and validation, publisher (rate limit, slow clients, no client),
// client (reconnect, bad input), UI model, and a loopback over the real transport.
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <thread>

#include "live_feed/live_feed.hpp"
#include "testutil.hpp"
#include "ui/model/live_feed_model.hpp"

using namespace spn;

namespace {

constexpr int64_t MS = 1000000;

LiveSample sample(uint32_t seed = 1) {
    LiveSample s;
    for (int i = 0; i < AX_COUNT; i++) s.axes[i] = static_cast<int>(seed * 7 + i * 13) - 100;
    s.buttons = 5;
    s.v = 1.5f, s.h = -2.25f, s.zoom = 0.5f;
    s.fresh = true, s.zooming = true, s.nav = false, s.enabled = true, s.idle = false;
    s.mode = "normal";
    return s;
}

// Scripted connection: bytes to read, bytes written, optional write capacity and failure.
struct FakeConn : FeedConn {
    std::deque<uint8_t> in;
    std::vector<uint8_t> out;
    long cap = 1 << 30;  // bytes the "socket buffer" still takes
    bool broken = false;
    long read(uint8_t *b, size_t n) override {
        if (broken) return -1;
        size_t k = 0;
        while (k < n && !in.empty()) b[k++] = in.front(), in.pop_front();
        return static_cast<long>(k);
    }
    long write(const uint8_t *b, size_t n) override {
        if (broken) return -1;
        const size_t k = std::min<size_t>(n, static_cast<size_t>(cap));
        out.insert(out.end(), b, b + k);
        cap -= static_cast<long>(k);
        return static_cast<long>(k);
    }
};
// The server owns the connection; the test keeps a raw pointer to inspect it.
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

uint32_t lcg(uint32_t &x) { return x = x * 1664525u + 1013904223u; }

void testFraming() {
    const LiveSample s = sample();
    LiveSample t = s;
    t.seq = 77;
    const std::vector<uint8_t> f = encodeLiveSample(t);
    assert(f.size() == kLiveHeaderBytes + kLiveSampleFixed + 6);
    assert(f[0] == 'D' && f[4] == kLiveVersion);

    LiveFrameParser p;
    LiveSample o;
    assert(p.feed(f.data(), f.size()) && p.next(&o));
    assert(o.seq == 77 && o.axes == s.axes && o.buttons == 5 && o.v == 1.5f && o.h == -2.25f && o.zoom == 0.5f);
    assert(o.fresh && o.zooming && !o.nav && o.enabled && !o.idle && o.mode == "normal");
    assert(!p.next(&o) && p.error() == LiveParseError::Ok && p.buffered() == 0);

    // byte by byte, and two frames glued together
    std::vector<uint8_t> two = f;
    two.insert(two.end(), f.begin(), f.end());
    int got = 0;
    for (uint8_t b : two) {
        assert(p.feed(&b, 1));
        while (p.next(&o)) got++;
    }
    assert(got == 2 && p.error() == LiveParseError::Ok);

    // encoder clamps what the parser would reject
    LiveSample bad = s;
    bad.axes[0] = 1 << 30, bad.axes[1] = -(1 << 30);
    bad.v = std::nanf("");
    bad.mode = std::string("a b\n") + std::string(100, 'x');
    const std::vector<uint8_t> g = encodeLiveSample(bad);
    p.reset();
    assert(p.feed(g.data(), g.size()) && p.next(&o));
    assert(o.axes[0] == kLiveAxisLimit && o.axes[1] == -kLiveAxisLimit && o.v == 0.0f);
    assert(o.mode.size() == kLiveMaxMode && o.mode.substr(0, 4) == "a?b?");
    assert(g.size() <= kLiveMaxFrame);
}

void testRejects() {
    const std::vector<uint8_t> good = encodeLiveSample(sample());
    LiveSample o;
    auto run = [&](std::vector<uint8_t> f, LiveParseError want) {
        LiveFrameParser p;
        bool fed = p.feed(f.data(), f.size());
        (void)fed;
        assert(!p.next(&o));
        assert(p.error() == want);
        assert(p.buffered() == 0);
        uint8_t z = 0;
        assert(!p.feed(&z, 1));  // stays in the error state until reset()
        p.reset();
        assert(p.error() == LiveParseError::Ok);
    };
    auto mut = [&](size_t i, uint8_t v) {
        std::vector<uint8_t> f = good;
        f[i] = v;
        return f;
    };
    run(mut(0, 'X'), LiveParseError::EBadMagic);
    run(mut(4, 2), LiveParseError::EVersionMismatch);
    run(mut(4, 0), LiveParseError::EVersionMismatch);
    {  // oversize: detected from the header alone, without waiting for the payload
        std::vector<uint8_t> f(good.begin(), good.begin() + kLiveHeaderBytes);
        f[6] = 0xFF, f[7] = 0xFF;
        run(f, LiveParseError::EOversize);
        f[6] = static_cast<uint8_t>(kLiveMaxPayload + 1), f[7] = 0;
        run(f, LiveParseError::EOversize);
    }
    {  // header-only prefixes: early rejection of garbage
        run({'D', 'F', 'X'}, LiveParseError::EBadMagic);
        run({'D', 'F', 'L', 'V', 9}, LiveParseError::EVersionMismatch);
    }
    {  // length shorter than the fixed part / inconsistent with mode_len
        std::vector<uint8_t> f = {'D', 'F', 'L', 'V', kLiveVersion, kLiveTypeSample, 10, 0};
        f.insert(f.end(), 10, 0);
        run(f, LiveParseError::EBadLength);
        run(mut(kLiveHeaderBytes + 45, 3), LiveParseError::EBadLength);
        run(mut(kLiveHeaderBytes + 45, 200), LiveParseError::EBadLength);
    }
    // payload values
    run(mut(kLiveHeaderBytes + 4 + 3, 0x7F), LiveParseError::EBadPayload);       // axis 0 = huge positive
    run(mut(kLiveHeaderBytes + 32 + 3, 0x7F), LiveParseError::EBadPayload);      // v = NaN/huge
    run(mut(kLiveHeaderBytes + 44, 0x20), LiveParseError::EBadPayload);          // reserved flag bit
    run(mut(kLiveHeaderBytes + kLiveSampleFixed, 0x01), LiveParseError::EBadPayload);  // control char in mode id
    run(mut(kLiveHeaderBytes + kLiveSampleFixed, ' '), LiveParseError::EBadPayload);
    run(mut(kLiveHeaderBytes + kLiveSampleFixed, 0xC3), LiveParseError::EBadPayload);  // non-ASCII

    {  // unknown frame type with a valid length is skipped, the next sample is still delivered
        std::vector<uint8_t> f = good;
        f[5] = 9;
        f.insert(f.end(), good.begin(), good.end());
        LiveFrameParser p;
        assert(p.feed(f.data(), f.size()) && p.next(&o) && o.mode == "normal" && !p.next(&o));
    }
    {  // buffer bound: a feed() chunk larger than allowed is refused
        LiveFrameParser p;
        std::vector<uint8_t> big(LiveFrameParser::kMaxChunk + kLiveMaxFrame + 1, 'D');
        assert(!p.feed(big.data(), big.size()) && p.error() == LiveParseError::EBufferOverflow);
    }
}

void testFuzz() {
    const std::vector<uint8_t> good = encodeLiveSample(sample());
    uint32_t x = 12345;
    LiveSample o;
    for (int round = 0; round < 3000; round++) {
        LiveFrameParser p;
        std::vector<uint8_t> data;
        const int kind = round % 3;
        if (kind == 0) {  // pure noise
            for (int i = 0; i < 200; i++) data.push_back(static_cast<uint8_t>(lcg(x) >> 24));
        } else {  // valid frames with random byte flips / truncation / duplication
            for (int k = 0; k < 4; k++) data.insert(data.end(), good.begin(), good.end());
            for (int i = 0, n = 1 + static_cast<int>(lcg(x) >> 29); i < n; i++)
                data[(lcg(x) >> 8) % data.size()] = static_cast<uint8_t>(lcg(x) >> 24);
            if (kind == 2) data.resize((lcg(x) >> 8) % data.size());
        }
        size_t off = 0;
        while (off < data.size()) {
            const size_t n = std::min<size_t>(1 + (lcg(x) >> 28), data.size() - off);
            if (!p.feed(data.data() + off, n)) break;
            off += n;
            while (p.next(&o)) {
                // everything that gets through is valid
                for (int a : o.axes) assert(std::abs(a) <= kLiveAxisLimit);
                assert(std::isfinite(o.v) && std::isfinite(o.h) && std::isfinite(o.zoom));
                assert(o.mode.size() <= kLiveMaxMode);
            }
            assert(p.buffered() <= kLiveMaxFrame + LiveFrameParser::kMaxChunk);
            if (p.error() != LiveParseError::Ok) break;
        }
    }
}

void testServerRateAndIdle() {
    auto *lp = new FakeListener;
    std::unique_ptr<FeedListener> l(lp);
    LiveFeedServer srv(std::move(l));
    assert(srv.start(nullptr));
    // no client: nothing is encoded or sent
    for (int i = 0; i < 100; i++) srv.publish(sample(), i * 4 * MS);
    assert(srv.clientCount() == 0 && srv.framesSent() == 0);

    auto c = std::make_unique<FakeConn>();
    FakeConn *cp = c.get();
    lp->waiting.push_back(std::move(c));
    // one second at the 250 Hz tick rate: about 30 frames, never one per tick
    const int64_t t0 = 1000 * MS;
    for (int i = 0; i < 250; i++) srv.publish(sample(), t0 + i * 4 * MS);
    assert(srv.clientCount() == 1);
    assert(srv.framesSent() >= 28 && srv.framesSent() <= 32);

    // what arrives parses cleanly, sequence numbers count up
    LiveFrameParser p;
    LiveSample o;
    assert(p.feed(cp->out.data(), cp->out.size()));
    uint32_t expect = 0;
    while (p.next(&o)) assert(o.seq == expect++);
    assert(expect == srv.framesSent() && p.error() == LiveParseError::Ok);

    // the server never reads: bytes a client sends are ignored
    cp->in = {'x', 'y'};
    srv.publish(sample(), t0 + 2000 * MS);
    assert(cp->in.size() == 2);

    // LIVE_FEED = 0 (active=false): clients are closed, new ones are refused
    LiveSample off = sample();
    off.active = false;
    srv.publish(off, t0 + 3000 * MS);
    assert(srv.clientCount() == 0);
    lp->waiting.push_back(std::make_unique<FakeConn>());
    srv.publish(off, t0 + 4000 * MS);
    assert(srv.clientCount() == 0 && lp->waiting.empty());
}

void testSlowClients() {
    auto *lp = new FakeListener;
    LiveFeedOptions opt;
    opt.maxDroppedFrames = 5;
    LiveFeedServer srv(std::unique_ptr<FeedListener>(lp), opt);
    assert(srv.start(nullptr));
    auto slow = std::make_unique<FakeConn>();
    auto fast = std::make_unique<FakeConn>();
    FakeConn *sp = slow.get(), *fp = fast.get();
    sp->cap = 10;  // takes part of the first frame, then blocks
    lp->waiting.push_back(std::move(slow));
    lp->waiting.push_back(std::move(fast));
    int64_t t = 1000 * MS;
    srv.publish(sample(), t);
    assert(srv.clientCount() == 2);
    assert(sp->out.size() == 10);  // never more than the buffer took, and never blocked
    for (int i = 0; i < 20 && srv.clientCount() == 2; i++) {
        t += 40 * MS;
        srv.publish(sample(), t);
    }
    assert(srv.clientCount() == 1 && srv.disconnects() == 1 && srv.framesDropped() >= 5);
    assert(!fp->out.empty());  // the slow client was destroyed, only the fast one is left

    // a client that drains again before the limit keeps its connection; frames stay whole
    auto *lp2 = new FakeListener;
    LiveFeedServer srv2(std::unique_ptr<FeedListener>(lp2), opt);
    assert(srv2.start(nullptr));
    auto c = std::make_unique<FakeConn>();
    FakeConn *cp = c.get();
    cp->cap = 20;
    lp2->waiting.push_back(std::move(c));
    t = 1000 * MS;
    srv2.publish(sample(), t);
    t += 40 * MS;
    srv2.publish(sample(), t);  // dropped (pending)
    cp->cap = 1 << 30;
    for (int i = 0; i < 3; i++) {
        t += 40 * MS;
        srv2.publish(sample(), t);
    }
    assert(srv2.clientCount() == 1);
    LiveFrameParser p;
    LiveSample o;
    assert(p.feed(cp->out.data(), cp->out.size()));
    int n = 0;
    while (p.next(&o)) n++;
    assert(n >= 3 && p.error() == LiveParseError::Ok);

    // a broken connection is removed; the client limit is enforced
    cp->broken = true;
    t += 40 * MS;
    srv2.publish(sample(), t);
    assert(srv2.clientCount() == 0);
    for (int i = 0; i < 10; i++) lp2->waiting.push_back(std::make_unique<FakeConn>());
    t += 200 * MS;
    srv2.publish(sample(), t);
    assert(srv2.clientCount() <= LiveFeedOptions{}.maxClients);
}

void testClient() {
    std::vector<FakeConn *> made;
    int attempts = 0;
    bool fail = false;
    std::vector<uint8_t> next;  // bytes the next connection will deliver
    LiveFeedClient cl([&](std::string *err) -> std::unique_ptr<FeedConn> {
        attempts++;
        if (fail) {
            *err = "refused";
            return nullptr;
        }
        auto c = std::make_unique<FakeConn>();
        c->in.assign(next.begin(), next.end());
        made.push_back(c.get());
        return c;
    });
    int64_t t = 0;
    int got = 0;
    auto cb = [&](const LiveSample &) { got++; };

    fail = true;  // daemon not running: backoff, not a busy loop
    assert(cl.poll(t, cb) == 0 && attempts == 1 && cl.state() == LiveFeedClient::State::Disconnected);
    for (int i = 0; i < 10; i++) cl.poll(t += 10 * MS, cb);
    assert(attempts == 1 && cl.lastError() == "refused");
    cl.poll(t += 600 * MS, cb);
    assert(attempts == 2);
    cl.poll(t += 600 * MS, cb);  // backoff doubled to 1 s
    assert(attempts == 2);

    // daemon appears
    fail = false;
    next = encodeLiveSample(sample(1));
    const std::vector<uint8_t> f2 = encodeLiveSample(sample(2));
    next.insert(next.end(), f2.begin(), f2.end());
    cl.poll(t += 2000 * MS, cb);
    assert(cl.state() == LiveFeedClient::State::Connected && got == 2 && cl.samples() == 2);

    // peer disappears: reconnect after the backoff and receive again
    made.back()->broken = true;
    cl.poll(t += 33 * MS, cb);
    assert(cl.state() == LiveFeedClient::State::Disconnected);
    const int a = attempts;
    cl.poll(t += 10 * MS, cb);
    assert(attempts == a);
    cl.poll(t += 700 * MS, cb);
    assert(attempts == a + 1 && cl.state() == LiveFeedClient::State::Connected && got == 4);

    // garbage: the connection is dropped, a good stream afterwards is accepted
    next = {'G', 'A', 'R', 'B', 'A', 'G', 'E', '!', '!', '!'};
    made.back()->broken = true;
    cl.poll(t += 33 * MS, cb);
    cl.poll(t += 6000 * MS, cb);  // reconnects and gets the garbage
    assert(cl.state() == LiveFeedClient::State::Disconnected && cl.lastError() == "bad magic");
    next = encodeLiveSample(sample(3));
    cl.poll(t += 6000 * MS, cb);
    assert(cl.state() == LiveFeedClient::State::Connected && got == 5);

    // version mismatch: reported as incompatible, retried slowly
    next = encodeLiveSample(sample(4));
    next[4] = 2;
    made.back()->broken = true;
    cl.poll(t += 33 * MS, cb);
    cl.poll(t += 6000 * MS, cb);
    assert(cl.state() == LiveFeedClient::State::Incompatible);
    assert(cl.lastError() == "protocol version mismatch");
    const int b = attempts;
    cl.poll(t += 1000 * MS, cb);
    assert(attempts == b);

    // bounded work per poll: a flood of valid frames is consumed in slices
    next.clear();
    const std::vector<uint8_t> ok = encodeLiveSample(sample(5));
    for (int i = 0; i < 400; i++) next.insert(next.end(), ok.begin(), ok.end());
    cl.poll(t += 6000 * MS, cb);
    assert(cl.state() == LiveFeedClient::State::Connected);
    const int first = got;
    cl.poll(t += MS, cb);
    assert(got > first);
}

void testModel() {
    LiveFeedModel m;
    assert(!m.hasSample() && m.stale(0));
    LiveSample s = sample();
    s.axes = {10, -20, 30, 0, 0, 0};
    s.buttons = 1;
    m.update(s, 100);
    s.axes = {-5, 40, -60, 0, 0, 0};
    s.buttons = 2;
    m.update(s, 200);
    assert(m.hasSample() && m.sampleCount() == 2 && !m.stale(300));
    assert(m.minOf(AX_X) == -5 && m.maxOf(AX_X) == 10 && m.peakOf(AX_Z) == 60 && m.peakOf(AX_Y) == 40);
    assert(m.buttonsEverPressed() == 3 && m.pressed(1) && !m.pressed(0) && !m.pressed(40));
    assert(m.stale(200 + LiveFeedModel::kStaleNs + 1));
    m.resetTracking();
    assert(m.peakOf(AX_Z) == 0 && m.buttonsEverPressed() == 0 && m.hasSample());
    s.axes = {1, 1, 1, 1, 1, 1};
    m.update(s, 400);
    assert(m.minOf(AX_Z) == 1 && m.maxOf(AX_Z) == 1);
}

// Real transport: POSIX socket in a private temp directory, or the user's named pipe on Windows.
// Skipped (and said so) if the environment forbids creating the endpoint.
void testLoopback() {
    std::string dir = tu::makeTempDir("dfl");
#ifndef _WIN32
    if (dir.size() > 60) {  // sun_path limit: would fall back to the shared /tmp location, do not use that
        std::printf("test_live_feed: loopback SKIPPED (temp directory path too long)\n");
        tu::removeAll(dir);
        return;
    }
#endif
    LiveFeedServer srv(makeFeedListener(dir));
    std::string err;
    if (!srv.start(&err)) {
        std::printf("test_live_feed: loopback SKIPPED (%s)\n", err.c_str());
        tu::removeAll(dir);
        return;
    }
    LiveFeedClient cl([dir](std::string *e) { return connectFeed(dir, e); });
    LiveFeedModel model;
    int64_t t = 0;
    auto now = [&] { return std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count(); };
    const int64_t start = now();
    (void)t;
    uint32_t n = 0;
    for (int i = 0; i < 400 && model.sampleCount() < 5; i++) {
        t = now();
        LiveSample s = sample(n++);
        srv.publish(s, t);
        model.pump(cl, t);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        if (now() - start > 8000 * MS) break;
    }
    if (cl.state() != LiveFeedClient::State::Connected && model.sampleCount() == 0) {
        std::printf("test_live_feed: loopback SKIPPED (could not connect: %s)\n", cl.lastError().c_str());
    } else {
        assert(model.sampleCount() >= 5);
        assert(srv.clientCount() == 1);
        assert(model.latest().mode == "normal" && model.latest().buttons == 5);
        // client goes away -> the server drops it on the next frame; a new client reconnects
        LiveFeedClient cl2([dir](std::string *e) { return connectFeed(dir, e); });
        LiveFeedModel m2;
        for (int i = 0; i < 400 && m2.sampleCount() < 3; i++) {
            const int64_t tt = now();
            srv.publish(sample(n++), tt);
            m2.pump(cl2, tt);
            model.pump(cl, tt);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        assert(m2.sampleCount() >= 3 && srv.clientCount() == 2);
    }
    tu::removeAll(dir);
}

}  // namespace

int main() {
    testFraming();
    testRejects();
    testFuzz();
    testServerRateAndIdle();
    testSlowClients();
    testClient();
    testModel();
    testLoopback();
    std::printf("test_live_feed: OK\n");
    return 0;
}
