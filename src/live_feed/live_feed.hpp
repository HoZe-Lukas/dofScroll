// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
// Live feed: transport interface (one implementation per OS family), the daemon-side publisher
// and the toolkit-free client. Protocol: live_feed_protocol.hpp. Security: docs/security.md.
//
// The server never blocks: accept, write and close are non-blocking and run inside publish(),
// called once per tick. Slow clients lose frames and are disconnected after too many in a row.
// Clients can only read; the server never reads from a connection.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "live_feed_protocol.hpp"

namespace spn {

// ---- transport ---------------------------------------------------------------------------------

// One connected byte stream, non-blocking.
class FeedConn {
public:
    virtual ~FeedConn() = default;
    // >0: bytes read; 0: nothing available now; <0: closed or failed.
    virtual long read(uint8_t *buf, size_t n) = 0;
    // >=0: bytes written (0 = would block); <0: closed or failed.
    virtual long write(const uint8_t *buf, size_t n) = 0;
};

// Server endpoint. accept() never blocks and returns only peers that passed the identity check.
class FeedListener {
public:
    virtual ~FeedListener() = default;
    virtual bool open(std::string *err) = 0;
    virtual std::unique_ptr<FeedConn> accept() = 0;  // null: nobody waiting
};

// Per-OS factories (live_feed_posix.cpp / live_feed_win.cpp). runtimeDir: PlatformPaths::runtimeDir
// (POSIX socket location; unused on Windows, where the pipe name carries the user SID).
std::unique_ptr<FeedListener> makeFeedListener(const std::string &runtimeDir);
std::unique_ptr<FeedConn> connectFeed(const std::string &runtimeDir, std::string *err);

// ---- server ------------------------------------------------------------------------------------

struct LiveFeedOptions {
    int hz = 30;                       // frames per second per client, clamped to 1..60
    int maxDroppedFrames = 30;         // consecutive dropped frames before a client is disconnected
    size_t maxClients = 4;
    int64_t acceptIntervalNs = 100000000;  // look for new clients at most this often
};

class LiveFeedServer {
public:
    explicit LiveFeedServer(std::unique_ptr<FeedListener> listener, LiveFeedOptions opt = {});
    bool start(std::string *err);

    // Called once per tick from the daemon main thread. Returns immediately unless a frame is due
    // and a client is connected; never blocks. sample.active == false closes all clients.
    void publish(const LiveSample &sample, int64_t nowNs);

    size_t clientCount() const { return clients_.size(); }
    uint64_t framesSent() const { return sent_; }
    uint64_t framesDropped() const { return dropped_; }
    uint64_t disconnects() const { return disconnects_; }

private:
    struct Client {
        std::unique_ptr<FeedConn> conn;
        std::vector<uint8_t> pending;  // unsent rest of a partially written frame
        int droppedInRow = 0;
    };
    std::unique_ptr<FeedListener> listener_;
    LiveFeedOptions opt_;
    bool started_ = false;
    std::vector<Client> clients_;
    int64_t nextAccept_ = 0, nextFrame_ = 0;
    uint32_t seq_ = 0;
    uint64_t sent_ = 0, dropped_ = 0, disconnects_ = 0;

    void acceptNew(bool keep);
    bool flush(Client &c);  // false: connection broken
};

// Creates the transport and the server and returns the sink for DaemonDeps::liveFeed (the server
// lives as long as the sink). A start failure is logged to stderr and yields an empty function:
// the daemon then runs without a feed.
std::function<void(const LiveSample &, int64_t)> makeLiveFeedSink(const std::string &runtimeDir);

// ---- client ------------------------------------------------------------------------------------

struct LiveFeedClientOptions {
    int64_t reconnectMinNs = 500000000;   // backoff after a failure: doubles up to reconnectMaxNs
    int64_t reconnectMaxNs = 5000000000;
    int maxReadsPerPoll = 8;              // bounds the work per poll()
};

class LiveFeedClient {
public:
    using Connector = std::function<std::unique_ptr<FeedConn>(std::string *err)>;
    enum class State { Disconnected, Connected, Incompatible };

    explicit LiveFeedClient(Connector connector, LiveFeedClientOptions opt = {});

    // Call periodically (UI timer, ~30 Hz). Connects/reconnects as needed, reads at most a bounded
    // amount and delivers every valid sample to `onSample` (may be null). Returns the number of
    // samples delivered.
    int poll(int64_t nowNs, const std::function<void(const LiveSample &)> &onSample);

    State state() const { return state_; }
    const std::string &lastError() const { return err_; }
    uint64_t samples() const { return samples_; }
    uint64_t connects() const { return connects_; }

private:
    Connector connector_;
    LiveFeedClientOptions opt_;
    std::unique_ptr<FeedConn> conn_;
    LiveFrameParser parser_;
    State state_ = State::Disconnected;
    std::string err_;
    int64_t nextConnect_ = 0, backoff_;
    uint64_t samples_ = 0, connects_ = 0;

    void drop(int64_t nowNs, const std::string &why, State st);
};

}  // namespace spn
