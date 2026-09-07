// client-side configuration passed to Client::Initialize.
//
// this is a pure POD-style knobs struct. all fields are
// snapshotted at Initialize() time into Client::Impl — mutating the Config
// afterwards has no effect. 
#pragma once

#include <cstddef>
#include <memory>
#include <string>

#include "worldengine/auth.h"
#include "worldengine/transport.h"

namespace worldengine {

// all fields have safe defaults. you can Initialize() with a value-constructed
// Config and get a working SDK (stub transport, unauthenticated, batch=100).
struct Config {
    // credentials / routing. `api_key` drives StaticApiKeyAuth auto-wiring
    // (see Client::Initialize). `project_id` and `ingest_url` are surfaced
    // to transports but StubTransport ignores them.
    std::string api_key;
    std::string project_id;
    std::string ingest_url;
    
    // optional stable user identifier. empty is allowed.
    // if your game has anonymous play sessions before login, leave this empty
    // for now and Track() will emit "user_id": "".
    std::string user_id;

    // flush when the queue reaches this many events
    std::size_t batch_size = 100;

    // ...or when this many seconds elapse since the last flush attempt.
    // (interval trigger — the periodic-drain wake condition.)
    double flush_interval_seconds = 10.0;

    // injected transport. if null, Client::Initialize substitutes a
    // StubTransport so the SDK works standalone (useful for demos and tests).
    std::shared_ptr<ITransport> transport;

    // injected auth provider. Precedence at Initialize() time:
    // 1. If `auth` is set, use it as-is.
    // 2. Else if `api_key` is non-empty, auto-construct
    //    StaticApiKeyAuth(api_key).
    // 3. Else no auth: transports receive an empty HttpHeaders vector
    //    (plus whatever headers the transport adds itself, e.g.
    //    Content-Type). This keeps demo/local mode unauthenticated.
    // the three-tier precedence exists so tests can run without auth wiring
    // (case 3), the demo can be trivial (case 2), and production can plug in
    // a real handshake provider (case 1) without changing any core code.
    std::shared_ptr<IAuthProvider> auth;
};

}
