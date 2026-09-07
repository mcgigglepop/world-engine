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



}

}

}