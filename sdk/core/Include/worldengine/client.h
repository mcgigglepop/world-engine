// public SDK entry point. thread-safe: Track/Flush/Shutdown may be called
// from any thread. track never performs I/O.
//
// role in the SDK: this is the single class game/engine code interacts with.
// everything else — event schema, transport, auth, batching, worker thread —
// hangs off Client via the pImpl in client.cpp.
#pragma once

#include <memory>
#include <string>

#include "worldengine/config.h"
#include "worldengine/property.h"

namespace worldengine {

// vrsion string emitted in every event's `sdk_version` field. bump on any
// change to the wire format so ingest can gate on client version.
constexpr const char* kSdkVersion = "0.1.0-mvp";

// compile-time platform string. set by the build system via the macro
// WORLDENGINE_PLATFORM (see CMakeLists.txt).
#ifndef WORLDENGINE_PLATFORM
#  define WORLDENGINE_PLATFORM "unknown"
#endif
constexpr const char* kPlatform = WORLDENGINE_PLATFORM;

// Client: owns the worker thread and the outbound event queue. non-copyable,
// non-movable (a moved-from Client would leave a live worker holding a stale
// pointer to Impl). construct one per process/game instance; destructor calls
// Shutdown() as a safety net if the caller forgot.
class Client {
public:
    Client();
    ~Client();

    Client(const Client&)            = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&)                 = delete;
    Client& operator=(Client&&)      = delete;

    // configure the client and start the background worker. safe to call
    // once; a second call after Shutdown() re-initializes (Initialize() calls
    // Shutdown() internally first). blocking only during worker startup;
    // returns as soon as the worker thread is running.
    void Initialize(const Config& config);

    // enqueue an event. never blocks on I/O — the whole point of the design.
    // adds session_id, event_id, timestamp, sdk_version, platform
    // automatically. thread-safe: multiple game threads may call Track() 
    // concurrently. silently drops if the client is not currently 
    // running (post-Shutdown or pre-Initialize).
    void Track(const std::string& event_name, const Properties& properties = {});

    // block until every event enqueued before this call has been handed to
    // the transport. Uses a "flush ticket" mechanism (see client.cpp) so
    // multiple concurrent callers each wait for their own snapshot to drain.
    // cheap no-op after Shutdown().
    void Flush();

    // flush + stop the worker thread. idempotent: safe to call twice, safe
    // to call before Initialize(). the destructor calls this so RAII-style
    // usage is fine.
    void Shutdown();

private:
    // pImpl: keeps all threading/queue state out of this public header, so
    // users of the SDK don't transitively include <thread>, <mutex>, etc.
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} 