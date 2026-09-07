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
    std::uint64_t flush_compelted = 0;
    
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


}

}

}