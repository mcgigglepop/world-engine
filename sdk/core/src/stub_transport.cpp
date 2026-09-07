// Copyright (c) 2026 world engine contributors
// StubTransport: pretty-prints a batch to stdout with a visible banner so
// the demo works without any backend running. Renders the request headers
// (with API keys redacted past the first 8 characters) above the JSON body
// so demos visibly show auth flowing.
//
// Role in the SDK: default fallback transport used by Client when the caller
// leaves Config::transport null. Also useful as a manual sanity check while
// developing new features — you can wire it in explicitly to see the raw
// batches your game is emitting.

#include "worldengine/transport.h"

#include <cstddef>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

#include "worldengine/auth.h"
#include "worldengine/event.h"

namespace worldengine {
namespace {

// Guards stdout so two threads calling Send() concurrently don't interleave
// their banners and JSON bodies. A single Client only calls Send() from its
// worker thread, but if two Client instances share the same StubTransport
// (or a test does something weird) this keeps output readable.
std::mutex g_stdout_mutex;

// Case-insensitive compare of ASCII header names (HTTP is case-insensitive).
// We roll our own so we don't drag in <cctype>+locale surprises; header names
// are guaranteed ASCII by the HTTP spec.
bool IEqualsAscii(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const char ca = a[i];
        const char cb = b[i];
        const char la = (ca >= 'A' && ca <= 'Z') ? static_cast<char>(ca + 32) : ca;
        const char lb = (cb >= 'A' && cb <= 'Z') ? static_cast<char>(cb + 32) : cb;
        if (la != lb) return false;
    }
    return true;
}

// Redact a header value for log output. For `Authorization: Bearer <key>`
// we keep the scheme + first 8 characters of the key, then mark as
// redacted. Non-Authorization headers are logged verbatim.
//
// IMPORTANT: This only affects rendering. The auth provider still returns
// the real key and a real transport would send it unmodified. We keep 8
// characters of prefix so a developer eyeballing demo output can visually
// confirm "yes this is the sk_demo_... key I expected" without spilling the
// full secret into terminal scrollback.
std::string RedactForLog(const std::string& name, const std::string& value) {
    if (!IEqualsAscii(name, "Authorization")) return value;

    // Look for a bearer-style value: "Bearer <key>".
    const std::string kBearer = "Bearer ";
    if (value.size() > kBearer.size() &&
        value.compare(0, kBearer.size(), kBearer) == 0) {
        const std::string key = value.substr(kBearer.size());
        const std::size_t keep = std::min<std::size_t>(8, key.size());
        return kBearer + key.substr(0, keep) + "... (redacted)";
    }

    // Unknown auth scheme (Basic, Digest, custom): show only the first 8 chars
    // total to avoid leaking whatever comes after.
    const std::size_t keep = std::min<std::size_t>(8, value.size());
    return value.substr(0, keep) + "... (redacted)";
}

}

bool StubTransport::Send(const std::vector<Event>& batch,
                         const HttpHeaders& headers) {
    // Assemble the entire output off-lock so we minimize time under the mutex.
    // Building into an ostringstream then writing once keeps other threads
    // waiting on stdout for microseconds, not milliseconds.
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& e : batch) {
        // Reparse via nlohmann to avoid duplicating the serializer here — the
        // canonical shape lives in event.cpp and we don't want it to drift.
        arr.push_back(nlohmann::json::parse(SerializeEvent(e)));
    }

    std::ostringstream out;
    out << "--- WorldEngine batch (" << batch.size() << " events) ---\n";
    out << "Headers:\n";
    for (const auto& kv : headers) {
        out << "  " << kv.first << ": " << RedactForLog(kv.first, kv.second) << '\n';
    }
    // Transports own their own content type; auth providers do not emit it.
    // Documented in include/worldengine/auth.h and include/worldengine/transport.h.
    out << "  Content-Type: application/json\n";
    out << arr.dump(/*indent=*/2) << '\n';

    {
        // Single lock for the actual write — everything above is thread-local.
        std::lock_guard<std::mutex> lock(g_stdout_mutex);
        std::cout << out.str();
        std::cout.flush();
    }
    return true;
}

// Convenience factory. Returned as shared_ptr so it can be stored directly
// into Config::transport and shared between Client instances if desired.
std::shared_ptr<ITransport> MakeStubTransport() {
    return std::make_shared<StubTransport>();
}

}