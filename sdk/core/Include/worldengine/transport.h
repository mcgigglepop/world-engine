// transport interface: how a batch of events leaves the SDK.
//
// this is the seam between the engine-agnostic core and any
// concrete delivery mechanism (libcurl, Unreal FHttpModule, in-process stub,
// test recorder). the interface keeps the core reusable across engines.
#pragma once

#include <memory>
#include <vector>

#include "worldengine/auth.h"
#include "worldengine/event.h"

namespace worldengine {

// contract every transport implements. The worker thread calls Send()
// synchronously; implementations may still be internally async, but must
// not throw. in this MVP we ship a StubTransport that prints to stdout.
//
// headers are supplied by the Client per batch (sourced from IAuthProvider)
// so auth strategies stay pluggable without touching transport internals.
// transports own their own content-type header; auth providers do not.
//
// Threading: Send() is called from the Client's single worker thread, so no
// concurrent invocations happen from within one Client. if a transport is
// shared between multiple Client instances (uncommon), it's responsible for
// its own internal synchronization.
class ITransport {
public:
    virtual ~ITransport() = default;

    // deliver a batch of events with the given request headers. returns
    // true on success. in the MVP the return value is informational only.
    // there is no retry loop. implementations must not throw — errors are the
    // return value.
    virtual bool Send(const std::vector<Event>& batch,
                      const HttpHeaders& headers) = 0;
};

// StubTransport pretty-prints each batch to stdout with a visible banner
// so the demo works without any backend. thread-safe: internal mutex
// serializes concurrent Send() calls so output does not interleave.
// renders auth headers above the JSON body. API keys are redacted past
// the first 8 characters in log output (the full key is still passed to
// the transport unmodified so real network transports downstream would see
// the true value).
class StubTransport : public ITransport {
public:
    bool Send(const std::vector<Event>& batch,
              const HttpHeaders& headers) override;
};

// convenience factory for the default transport. used by Client::Initialize
// when Config::transport is left null so the SDK is demoable out of the box.
std::shared_ptr<ITransport> MakeStubTransport();

} 
