// thread-safe queue + background worker thread.
//
// this file is the beating heart of the core. It implements
// the data path (event lifecycle -> queue
// -> background worker -> transport) and the three-tier auth precedence.
//
// Design notes (why the code looks the way it does):
//  - Track() only enqueues and notifies. It NEVER touches the transport. This
//    is the single most important property of the SDK
//    (Don't send HTTP from the game thread. [...] You absolutely do not want
//    someone saying: 'Your analytics SDK caused our game to stutter.')
//  - The worker has three wake conditions: (a) queue reached
//    batch_size, (b) flush_interval elapsed, (c) explicit Flush()/Shutdown().
//    These are all encoded in a single condition_variable predicate below.
//  - Flush() blocks until the queue is drained by publishing a monotonically
//    increasing "flush ticket" the worker acknowledges after each drain.
//    Multiple concurrent Flush() callers each wait for their own ticket, so
//    ordering is well defined.
//  - Headers are fetched on the worker thread immediately before Send(), not
//    at Track() time. This matters because a future IAuthProvider might lock
//    an internal mutex or block briefly on token refresh — we absolutely do
//    not want that latency to hit the game thread.

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
#include "worldengine/transport.h"
#include "uuid.h"

namespace worldengine {
namespace {

// Milliseconds since Unix epoch. system_clock (not steady_clock) because
// timestamp_ms is meant to be human-meaningful ("when did this happen in the
// real world"), not just monotonic since process start.
std::int64_t NowMillis() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

} // namespace

// pImpl for Client. Split out so <thread>, <mutex>, <deque> don't leak into
// the public header — game code that includes "worldengine/client.h" shouldn't
// pay for those transitively.
struct Client::Impl {
    // --- config snapshot (immutable after Initialize) --------------------
    // Copied by value from Config so a caller can safely mutate or destroy
    // the Config they passed in.
    Config                          config;
    std::shared_ptr<ITransport>     transport;   // resolved (never null after Initialize)
    std::shared_ptr<IAuthProvider>  auth;        // may be null (unauthenticated mode)
    std::string                     session_id;  // one UUID per Initialize() call

    // --- queue + synchronization ----------------------------------------
    // Single mutex protects `queue`, `stop`, `running`, and the flush counters.
    // A single lock is simpler than sharding for this MVP; the critical
    // sections are all short (push/pop, flag reads).
    std::mutex                   mu;
    // cv is notified by producers (Track) and by Flush/Shutdown. The worker
    // waits on it with a predicate covering all three wake conditions.
    std::condition_variable      cv;
    // std::deque, not std::vector: we pop from the front and push at the back
    // in different threads. deque gives O(1) at both ends and doesn't
    // invalidate front elements when the back grows — a vector would either
    // require ring-buffer bookkeeping or costly shifts.
    std::deque<Event>            queue;

    // Lifecycle flags.
    // `running` is set true by Initialize, false by Shutdown. Track() checks
    // it and silently drops events after shutdown (never crashes).
    // `stop` is a one-shot flag telling the worker to exit its loop.
    bool                         running = false;
    bool                         stop    = false;

    // Flush coordination. When a caller wants to block until the queue is
    // drained, it increments `flush_requested` (its "ticket") and waits for
    // `flush_completed` to catch up to that ticket. Monotonic counters are
    // simpler and more robust than a "flushing" bool — multiple concurrent
    // flushers can't confuse each other.
    std::uint64_t                flush_requested = 0;
    std::uint64_t                flush_completed = 0;

    // The single worker thread. Owned here so Shutdown can join it.
    std::thread                  worker;

    // Pop up to `max` events off the queue and hand them to the transport.
    // Called only from the worker thread. Returns number of events sent.
    // `max == 0` means "unlimited"; otherwise the batch is capped so callers
    // can drain in fixed-size chunks (matches the size-trigger semantics).
    //
    // Note the shape: we hold the mutex only while moving events out of the
    // queue, then release it before calling into the transport. The transport
    // might do arbitrary work (in a real HTTP transport: DNS, TLS, socket
    // I/O) and holding `mu` across that would block every Track() call.
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
            // Compute headers on the worker thread (per requirements).
            // If no auth is configured, pass an empty header vector so
            // unauthenticated demo/local mode still works. Doing it here —
            // and not in Track() — means a future provider can safely
            // acquire a mutex or refresh a token; the cost stays off the
            // game thread.
            HttpHeaders headers = auth ? auth->GetHeaders() : HttpHeaders{};
            transport->Send(batch, headers);
        }
        return batch.size();
    }

    // Drain the queue completely in `chunk`-sized batches so downstream
    // observers see predictable batch sizes. Returns total events sent.
    // Used on Flush(), Shutdown(), and periodic interval flushes so a big
    // spike doesn't turn into one giant batch that violates batch_size.
    std::size_t DrainAllChunked(std::size_t chunk) {
        std::size_t total = 0;
        while (true) {
            const std::size_t n = DrainOnce(chunk);
            if (n == 0) break;
            total += n;
        }
        return total;
    }

    // The worker's main loop. Runs on `worker` thread until `stop` is set.
    // Handles all three wake conditions inside a single
    // wait_until + predicate.
    void WorkerLoop() {
        using clock = std::chrono::steady_clock;  // monotonic; not affected by wall-clock jumps
        auto last_flush = clock::now();
        const auto interval = std::chrono::duration<double>(config.flush_interval_seconds);

        while (true) {
            std::unique_lock<std::mutex> lock(mu);

            // Wake if: stopping, an explicit flush was requested, or we have
            // at least one full batch. Otherwise wait up to the flush
            // interval and let the timeout drive a periodic flush.
            //
            // The predicate is what makes condition_variable safe against
            // spurious wakeups: even if the OS decides to wake us for no
            // reason, we go back to sleep unless one of the three real
            // conditions is true. This is the canonical CV usage.
            const auto deadline = last_flush + std::chrono::duration_cast<clock::duration>(interval);
            cv.wait_until(lock, deadline, [this] {
                return stop
                    || flush_requested > flush_completed
                    || queue.size() >= config.batch_size;
            });

            // Snapshot the reasons we woke up while we still hold the lock,
            // so the downstream code doesn't have to re-check under the mutex.
            const bool  should_stop           = stop;
            const bool  explicit_flush        = flush_requested > flush_completed;
            const auto  now                   = clock::now();
            const bool  interval_elapsed      = now >= deadline;
            const bool  has_full_batch        = queue.size() >= config.batch_size;
            const std::uint64_t seen_request  = flush_requested;

            lock.unlock();

            // All drains chunk by batch_size so downstream sees predictable
            // batch sizes regardless of the trigger. Order matters: stop and
            // explicit flush must drain everything; the size trigger only
            // sends one batch (leaves the rest for later so we don't
            // monopolize the worker); interval-elapsed drains everything
            // pending, matching s.8's "10 seconds elapsed -> flush".
            if (should_stop || explicit_flush) {
                DrainAllChunked(config.batch_size);
            } else if (has_full_batch) {
                // Send exactly one full batch; leave the rest for later.
                DrainOnce(config.batch_size);
            } else if (interval_elapsed) {
                DrainAllChunked(config.batch_size);
            }

            last_flush = clock::now();

            // Wake any Flush() callers whose ticket has now been satisfied.
            // We use max() defensively so we never step backwards even if
            // multiple flushes race.
            if (explicit_flush) {
                std::lock_guard<std::mutex> lock2(mu);
                flush_completed = std::max(flush_completed, seen_request);
                cv.notify_all();
            }

            if (should_stop) {
                // Catch anything enqueued between our wake-up and now — a
                // Track() call could have raced past our earlier snapshot.
                // Draining once more is cheap and guarantees the "Shutdown
                // flushes pending events" invariant tested in tests/.
                DrainAllChunked(config.batch_size);
                std::lock_guard<std::mutex> lock2(mu);
                flush_completed = flush_requested;
                cv.notify_all();
                return;
            }
        }
    }
};

// Trivial ctor — all the interesting state initializes in Initialize().
Client::Client() : impl_(std::make_unique<Impl>()) {}

Client::~Client() {
    // RAII safety net: if the caller forgets to Shutdown, do it now.
    // Shutdown is idempotent so calling it here on an already-shut-down or
    // never-initialized Client is safe.
    Shutdown();
}

void Client::Initialize(const Config& config) {
    // If we're already running, tear down cleanly first so Initialize is
    // idempotent from the caller's perspective. A test / hot-reload can
    // safely call Initialize twice.
    Shutdown();

    impl_->config     = config;
    // Fall back to StubTransport so the SDK works with zero configuration.
    impl_->transport  = config.transport ? config.transport : MakeStubTransport();

    // Auth wiring precedence (mirrors Config::auth doc):
    //   1. explicit provider  -> use as-is
    //   2. non-empty api_key  -> auto-wire StaticApiKeyAuth
    //   3. neither            -> null; worker sends empty HttpHeaders
    // Order matters: explicit wins so tests can inject a fake provider even
    // when Config::api_key is also set (see test_auth.cpp override case).
    if (config.auth) {
        impl_->auth = config.auth;
    } else if (!config.api_key.empty()) {
        impl_->auth = std::make_shared<StaticApiKeyAuth>(config.api_key);
    } else {
        impl_->auth.reset();
    }

    // One session_id per Client lifetime — stable across every Track() call
    // until the next Initialize/Shutdown cycle.
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
    // properties.
    Event e;
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
