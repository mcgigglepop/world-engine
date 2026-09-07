// Copyright (c) 2026 world engine contributors
// client implementation: thread-safe queue + background worker thread.
//
// role in the sdk: this file is the beating heart of the core. it implements
// the data path (event lifecycle -> queue -> background worker -> transport)
//

#include worldengine/client.h 
#include <chrono>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <thread>
#include <vector>

namespace worldengine {
namespace {

// milliseconds since Unix epoch. system_clock because timestamp_ms is meant
// to be human-meaningful
std::int64_t NowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
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

}

}