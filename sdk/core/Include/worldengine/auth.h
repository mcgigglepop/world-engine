// pluggable authentication for outbound event batches. the Client calls
// IAuthProvider::GetHeaders() on the worker thread immediately before each
// transport Send(), so implementations must be thread-safe. for the MVP
// GetHeaders() is synchronous and non-blocking; future strategies (handshake
// tokens, HMAC signing) can drop in behind this same interface without
// touching Client or transport code.
//
// why auth is a separate abstraction from ITransport: HTTP `Content-Type` is a
// transport concern (JSON vs protobuf vs CBOR belongs to whoever serializes),
// while `Authorization` is an auth concern (bearer vs HMAC vs mTLS is
// orthogonal to how bytes are sent). splitting them means you can keep a
// single transport and swap StaticApiKeyAuth -> HandshakeTokenAuth without
// touching the transport, or vice versa.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace worldengine {

// ordered list of (name, value) header pairs. vector because HTTP
// permits repeated header names and preserves order. the shape also matches
// what a real HTTP client will consume when we replace StubTransport
// (libcurl's `curl_slist`, Unreal's `IHttpRequest::SetHeader`, etc.).
using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

// Contract every auth strategy implements.
//
// GetHeaders() is invoked from the Client's worker thread, exactly once
// per outbound batch, right before ITransport::Send(). implementations 
// must be thread-safe (a future producer thread could read cached token state) 
// and must not block on I/O in the MVP.
// Lifetime: the Client holds a shared_ptr to the provider for as long as it
// is running; provider outlives the last Send() call.
class IAuthProvider {
public:
    virtual ~IAuthProvider() = default;

    // called by the worker thread once per batch, immediately before Send().
    // must be thread-safe and non-blocking for the MVP. return an empty vector
    // to send the batch unauthenticated.
    virtual HttpHeaders GetHeaders() = 0;
};

// trivial auth strategy that stamps every batch with a static bearer token.
// suitable for demos and internal tools; static keys shipped in a game
// client can be extracted from binaries, so production deployments should
// swap in a provider that mints short-lived tokens against an auth service.
// the Client will construct one of these for you automatically if you set
// Config::api_key but leave Config::auth null (see Client::Initialize).
class StaticApiKeyAuth : public IAuthProvider {
public:
    // takes the key by value so callers can move a temporary in without a copy
    explicit StaticApiKeyAuth(std::string api_key);

    // always returns exactly one header: `Authorization: Bearer <api_key>`
    // thread-safe: api_key_ is const-after-construction
    HttpHeaders GetHeaders() override;

private:
    std::string api_key_;  // never mutated after construction
};

} 