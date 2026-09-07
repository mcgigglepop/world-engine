// Copyright (c) 2026 world engine contributors
// Static API key auth: emits a single `Authorization: Bearer <key>` header.

#include "worldengine/auth.h"
#include <utility>

namespace worldengine {

StaticApiKeyAuth::StaticApiKeyAuth(std::string api_key)
    : api_key_(std::move(api_key)) {}

HttpHeaders StaticApiKeyAuth::GetHeaders() {
    // return the raw key. Redaction, if any, is a rendering concern owned
    // by the transport (see StubTransport::Send), not the auth provider --
    // the real network transport must see the real key.
    // thread-safe because api_key_ is written only to the constructor and
    // then treated as read-only for the object's lifetime.
    return { { "Authorization", "Bearer " + api_key_ } };
}

} 