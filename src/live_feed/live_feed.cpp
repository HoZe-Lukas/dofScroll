// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Lukas Hois-Zelinka
#include "live_feed.hpp"

#include <algorithm>
#include <iostream>

namespace spn {

// ---- server ------------------------------------------------------------------------------------

LiveFeedServer::LiveFeedServer(std::unique_ptr<FeedListener> listener, const LiveFeedOptions &opt)
    : listener_(std::move(listener)), opt_(opt) {
    opt_.hz = std::clamp(opt_.hz, 1, 60);
    if (opt_.maxDroppedFrames < 1) opt_.maxDroppedFrames = 1;
}

bool LiveFeedServer::start(std::string *err) {
    std::string dummy;
    if (!err) err = &dummy;
    if (!listener_) {
        *err = "no live feed transport";
        return false;
    }
    started_ = listener_->open(err);
    return started_;
}

void LiveFeedServer::acceptNew(bool keep) {
    for (int i = 0; i < 8; i++) {  // bounded per call
        std::unique_ptr<FeedConn> c = listener_->accept();
        if (!c) break;
        if (keep && clients_.size() < opt_.maxClients) {
            Client cl;
            cl.conn = std::move(c);
            clients_.push_back(std::move(cl));
        }  // else: the connection is closed by the destructor
    }
}

bool LiveFeedServer::flush(Client &c) {
    while (!c.pending.empty()) {
        const long w = c.conn->write(c.pending.data(), c.pending.size());
        if (w < 0) return false;
        if (w == 0) return true;
        c.pending.erase(c.pending.begin(), c.pending.begin() + w);
    }
    return true;
}

void LiveFeedServer::publish(const LiveSample &sample, int64_t nowNs) {
    if (!started_) return;
    if (!sample.active) {
        disconnects_ += clients_.size();
        clients_.clear();
        if (nowNs >= nextAccept_) {
            nextAccept_ = nowNs + opt_.acceptIntervalNs;
            acceptNew(false);  // connections are closed right away
        }
        return;
    }
    if (nowNs >= nextAccept_) {
        nextAccept_ = nowNs + opt_.acceptIntervalNs;
        acceptNew(true);
    }
    if (!haveInfo_ || sample.devicesRev != infoRev_) {
        haveInfo_ = true;
        infoRev_ = sample.devicesRev;
        infoFrame_ = encodeLiveDevices(sample.devices);
        for (Client &c : clients_) c.needInfo = true;
    }
    if (clients_.empty() || nowNs < nextFrame_) return;
    const int64_t period = 1000000000LL / opt_.hz;
    nextFrame_ = (nowNs - nextFrame_ > period) ? nowNs + period : nextFrame_ + period;

    LiveSample s = sample;
    s.seq = seq_++;
    std::vector<uint8_t> frame = encodeLiveSource(s.source);  // empty if unknown; then the sample frame
    const std::vector<uint8_t> sampleFrame = encodeLiveSample(s);
    frame.insert(frame.end(), sampleFrame.begin(), sampleFrame.end());

    for (size_t i = 0; i < clients_.size();) {
        Client &c = clients_[i];
        bool alive = flush(c);
        if (alive) {
            if (alive && c.needInfo && c.pending.empty()) {
                const long w = c.conn->write(infoFrame_.data(), infoFrame_.size());
                if (w < 0) {
                    alive = false;
                } else {
                    if (static_cast<size_t>(w) < infoFrame_.size()) c.pending.assign(infoFrame_.begin() + w, infoFrame_.end());
                    c.needInfo = false;
                }
            }
        }
        if (alive) {
            if (!c.pending.empty()) {  // still busy with the previous frame: drop this one
                dropped_++;
                if (++c.droppedInRow > opt_.maxDroppedFrames) alive = false;
            } else {
                const long w = c.conn->write(frame.data(), frame.size());
                if (w < 0) {
                    alive = false;
                } else {
                    if (static_cast<size_t>(w) < frame.size()) c.pending.assign(frame.begin() + w, frame.end());
                    c.droppedInRow = 0;
                    sent_++;
                }
            }
        }
        if (!alive) {
            disconnects_++;
            clients_.erase(clients_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            i++;
        }
    }
}

std::function<void(const LiveSample &, int64_t)> makeLiveFeedSink(const std::string &runtimeDir) {
    auto server = std::make_shared<LiveFeedServer>(makeFeedListener(runtimeDir));
    std::string err;
    if (!server->start(&err)) {
        std::cerr << "[dofscroll] live feed disabled: " << err << std::endl;
        return {};
    }
    return [server](const LiveSample &s, int64_t nowNs) { server->publish(s, nowNs); };
}

// ---- client ------------------------------------------------------------------------------------

LiveFeedClient::LiveFeedClient(Connector connector, const LiveFeedClientOptions &opt)
    : connector_(std::move(connector)), opt_(opt), backoff_(opt.reconnectMinNs) {}

void LiveFeedClient::drop(int64_t nowNs, const std::string &why, State st) {
    infoTotal_ += parser_.infoCount();
    conn_.reset();
    parser_.reset();
    err_ = why;
    state_ = st;
    nextConnect_ = nowNs + backoff_;
    backoff_ = std::min(backoff_ * 2, opt_.reconnectMaxNs);
}

int LiveFeedClient::poll(int64_t nowNs, const std::function<void(const LiveSample &)> &onSample) {
    if (!conn_) {
        if (nowNs < nextConnect_ || !connector_) return 0;
        std::string e;
        conn_ = connector_(&e);
        if (!conn_) {
            drop(nowNs, e.empty() ? "connect failed" : e, State::Disconnected);
            return 0;
        }
        parser_.reset();
        state_ = State::Connected;
        err_.clear();
        connects_++;
    }
    int delivered = 0;
    uint8_t buf[512];
    for (int r = 0; r < opt_.maxReadsPerPoll; r++) {
        const long n = conn_->read(buf, sizeof buf);
        if (n < 0) {
            drop(nowNs, "connection closed", State::Disconnected);
            return delivered;
        }
        if (n == 0) break;
        if (!parser_.feed(buf, static_cast<size_t>(n))) {
            drop(nowNs, liveParseErrorText(parser_.error()), State::Disconnected);
            return delivered;
        }
        LiveSample s;
        while (parser_.next(&s)) {
            samples_++;
            delivered++;
            if (onSample) onSample(s);
        }
        if (parser_.error() != LiveParseError::Ok) {
            const bool ver = parser_.error() == LiveParseError::EVersionMismatch;
            drop(nowNs, liveParseErrorText(parser_.error()), ver ? State::Incompatible : State::Disconnected);
            if (ver) backoff_ = opt_.reconnectMaxNs;  // a mismatch does not heal quickly
            nextConnect_ = nowNs + backoff_;
            return delivered;
        }
        if (static_cast<size_t>(n) < sizeof buf) break;
    }
    // A healthy stream resets the backoff.
    if (delivered > 0) backoff_ = opt_.reconnectMinNs;
    return delivered;
}

}  // namespace spn
