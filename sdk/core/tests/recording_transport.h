// Copyright (c) 2026 world engine contributors
// Test-only in-memory transport that captures every batch it receives,
// along with the headers supplied by the Client for that batch.
//
// Role in the test suite: this is the observable-outputs backstop. Every
// integration test wires one of these into Config::transport and then asserts
// on batches/events/header_sets after Shutdown(). It replaces StubTransport
// (which writes to stdout) with an in-memory recorder so tests can assert on
// exact counts, ordering, and header contents.
//
// Threading: Send() is called from the Client's worker thread; the accessor
// methods are called from the test thread after Shutdown() has joined the
// worker, but they're still mutex-protected in case a future test wants to
// peek mid-run.
#pragma once

#include <mutex>
#include <vector>

#include "worldengine/auth.h"
#include "worldengine/event.h"
#include "worldengine/transport.h"

namespace worldengine::testing {

class RecordingTransport : public ITransport {
public:
    // Captures the batch, its headers, and appends events to a flat log all
    // under a single lock so counts stay consistent to a test reader.
    bool Send(const std::vector<Event>& batch,
              const HttpHeaders& headers) override {
        std::lock_guard<std::mutex> lock(mu_);
        batches_.push_back(batch);
        header_sets_.push_back(headers);
        for (const auto& e : batch) events_.push_back(e);
        return true;
    }

    // Returns a copy so callers can iterate at leisure without holding the
    // mutex; the RecordingTransport itself is used once per test and thrown
    // away, so the extra allocation is negligible.
    std::vector<std::vector<Event>> batches() const {
        std::lock_guard<std::mutex> lock(mu_);
        return batches_;
    }

    // Flat log of every event across every batch, in batch order.
    std::vector<Event> events() const {
        std::lock_guard<std::mutex> lock(mu_);
        return events_;
    }

    // Headers captured per batch, indexed 1:1 with batches().
    std::vector<HttpHeaders> header_sets() const {
        std::lock_guard<std::mutex> lock(mu_);
        return header_sets_;
    }

    std::size_t event_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return events_.size();
    }

    std::size_t batch_count() const {
        std::lock_guard<std::mutex> lock(mu_);
        return batches_.size();
    }

private:
    // `mutable` so const accessors can lock. Locking a const method is a
    // standard exception to the "no mutation" rule — the observable state
    // doesn't change.
    mutable std::mutex               mu_;
    std::vector<std::vector<Event>>  batches_;
    std::vector<Event>               events_;
    std::vector<HttpHeaders>         header_sets_;
};

}
