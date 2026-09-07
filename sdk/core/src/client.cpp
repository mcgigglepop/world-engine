// Copyright (c) 2026 world engine contributors
// client implementation: thread-safe queue + background worker thread.
//
// role in the sdk: this file is the beating heart of the core. it implements
// the data path (event lifecycle -> queue -> background worker -> transport)
//

#include "worldengine/client.h" 

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "worldengine/auth.h"
#include "worldengine/event.h"

namespace worldengine {
namespace {

// milliseconds since Unix epoch. system_clock because timestamp_ms is meant
// to be human-meaningful
std::int64_t NowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

}

// pImpl for client. Split out so <thread>, <mutex>, <dequeue> don't leak into the 
// public header
struct Client::Impl {
    Config                         config;
    std::shared_ptr<ITransport>    transport;
    std::shared_ptr<IAuthProvider> auth;
    std::string                    session_id;
    std::mutex                     mu;
    std::condition_variable        cv;
    std::deque<Event>              queue;
    
    bool running = false;
    bool stop = false;

    std::uint64_t flush_requested = 0;
    std::uint64_t flush_completed = 0;
    
    std::thread                    worker;

    // Pop up to `max` events off the queue and hand them to the transport. 
    // called only from the worker thread. returns number of events sent. 
    std::size_t DrainOnce(std::size_t max) {
        std::vector<Event> batch;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (queue.empty()) return 0;
            const std::size_t take = (max == 0)
                ? queue.size()
                : std::min(queue.size(), max);
            batch.reserve(take);
            for (std::size_t i = 0; i < take; ++i) {
                batch.push_back(std::move(queue.front()));
                queue.pop_front();
            } 
        }
        if (transport) {
            // compute headers on the worker thread. if no auth is configured,
            // pass an empty header vector so unauthenticated local mode still works.
            HttpHeaders headers = auth ? auth->GetHeaders() : HttpHeaders();
            transport->Send(batch, headers);
        }
        return batch.size();
    }

    // Drain the queue completely in chunk-size batches so downstream
    // observers see predictable batch sizes. Returns total events sent.
    // used on Flush(), Shutdown(), and periodic interval flushes so a big
    // spike doesn't turn into a giant batch and violate batch_size.
    std::size_t DrainAllChunked(std::size_t chunk) {
        std::size_t = 0;
        while (true) {
            const std::size_t n = DrainOnce(chunk);
            if (n == 0) break;
            total += n;
        }
        return total;
    }

    // the workers main loop. runs on `worker` thread until `stop` is set.
    // handles all three wake conditions inside a single
    // wait_until + predicate.
    void WorkerLoop() {
        using clock = std::chrono::steady_clock;
        auto last_flush = clock::now();
        const auto interval = std::chrono::duration<double>(config.flush_interval_seconds);

        while (true) {
            std::unique_lock<std::mutex> lock(mu);

            // wake if: stopping, an explicit flush was requested, or we have
            // at least one full batch. otherwise, wait up to the flush
            // interval and let the timeout drive a periodic flush.

            const auto deadline = last_flush + std::chrono::duration_cast<clock::duration>(interval);
            cv.wait_until(lock, deadline, [this] {
                return stop
                    || flush_requested > flush_completed
                    || queue.size() >= config.batch_size;
            });

            // snapshot the reason we woke up while we still hold the lock,
            // so the downstream code doesnt' have to re-check under the mutex.
            const bool should_stop = stop;
            const bool explicit_flush = flush_requested > flush_completed;
            const auto now = clock::now();
            const bool interval_elapsed = now >= deadline;
            const bool has_full_batch = queue.size() >= config.batch_size;
            const std::uint64_t seen_request = flush_requested;

            lock.unlock();

            // drains all chunks by batch_size so downstream sees predictable
            // batch sizes regardless of the trigger
            if (should_stop || explicit_flush) {
                DrainAllChunked(config.batch_size);
            } else if (has_full_batch) {
                // send exactly one full batch. leave the rest for later
                DrainOnce(config.batch_size);
            } else if (interval_elapsed) {
                DrainAllChunked(config.batch_size);
            }

            last_flush = clock::now();

            // wake any Flush() callers whose ticket has now been satisfied,
            // we use max() defensively so we never have to step backwards even if
            // multiple flushes race.
            if (explicit_flush) {
                std::lock_guard<std::mutex> lock2(mu);
                flush_completed = std::max(flush_completed, seen_request);
                cv.notify_all();
            }

            if (should_stop) {
                // catch anything enqueued between our wake-up and now. 
                DrainAllChunked(config.batch_size);
                std::lock_guard<std::mutex> lock2(mu);
                flush_completed = flush_requested;
                cv.notify_all();
                return;
            }

        }
    }
};

Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() {
    // RAII safety net: if the caller forgets to shutdown, do it now. 
    Shutdown();
}

void Client::Initialize(const Config& config) {
    // if we're already running, tear down cleanly first so Initialize is 
    // idempotent from the callers perspective.
    Shutdown();

    impl_->config = config;
    // fall back to StubTransport so the SDK works with zero configuration.
    impl_->transport = config.transport ? config.transport : MakeStubTransport();

    if (config.auth) {
        impl_->auth = config.auth;
    } else if (!config.api_key.empty()) {
        impl_->auth = std::make_shared<StaticApiKeyAuth>(config.api_key);
    } else {
        impl_->auth.reset();
    }

    // one session_id per client lifetime = stable across every Track() call
    // until the next Initialize/shutdown cycle.
    impl_->session_id = internal::NewUuidV4();

    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        impl_->stop            = false;
        impl_->running         = true;
        impl_->flush_requested = 0;
        impl_->flush_completed = 0;
    }

    // Start the worker last, after everything it will read is in place.
    impl_->worker = std::thread(&Impl::WorkerLoop, impl_.get());
}

void Client::Track(const std::string& event_name, const Properties& properties) {
    // Fast path: cheap to call from the game thread. All work is either
    // trivial (copy strings, generate UUID via thread_local RNG) or defers
    // to the worker. No I/O, no allocations beyond the Event and its
    // properties
    e.event_id     = internal::NewUuidV4();
    e.project_id   = impl_->config.project_id;
    e.session_id   = impl_->session_id;
    e.user_id      = impl_->config.user_id;
    e.event_name   = event_name;
    e.timestamp_ms = NowMillis();
    e.sdk_version  = kSdkVersion;
    e.platform     = kPlatform;
    e.properties   = properties;

    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (!impl_->running) return;   // silently drop after shutdown
        impl_->queue.push_back(std::move(e));
    }
    // notify_one because only the (single) worker is waiting on the queue.
    // notify_all would be equivalent but wastes a syscall on some platforms.
    impl_->cv.notify_one();
}

void Client::Flush() {
    // Take a ticket (a snapshot of flush_requested) while holding the lock so
    // the worker sees a consistent view. If already shut down, nothing to do.
    std::uint64_t my_ticket;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (!impl_->running) return;
        my_ticket = ++impl_->flush_requested;
    }
    // notify_all in case multiple threads are waiting (worker + other
    // Flush() callers waiting for their own tickets).
    impl_->cv.notify_all();

    // Wait until the worker has acknowledged our ticket or the client has
    // been shut down out from under us.
    std::unique_lock<std::mutex> lock(impl_->mu);
    impl_->cv.wait(lock, [&] { return impl_->flush_completed >= my_ticket || !impl_->running; });
}

void Client::Shutdown() {
    // Move the worker handle out under the lock so we can call join()
    // without holding it — join() will block until the worker returns, and
    // the worker acquires the lock inside WorkerLoop.
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (!impl_->running) return;
        impl_->stop    = true;
        impl_->running = false;
        worker         = std::move(impl_->worker);
    }
    // Wake the worker so it observes `stop == true` and exits promptly.
    impl_->cv.notify_all();
    // We join (not detach) because we want a strict "no worker running after
    // Shutdown returns" guarantee — a detached worker could still be writing
    // to stdout while the demo prints "done" and confuses users.
    if (worker.joinable()) worker.join();
}

}