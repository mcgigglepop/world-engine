// Auth layer coverage: StaticApiKeyAuth shape, Config::api_key auto-wiring,
// unauthenticated mode, and explicit-provider override precedence.
//
// Why we care: the three-tier precedence documented in Config::auth is what
// lets tests run without auth wiring, the demo run with a static key, and
// production run with a real handshake provider — all without touching any
// core code. If any tier breaks, the whole "pluggable auth" story collapses.
// This file locks each tier down separately.

#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "worldengine/auth.h"
#include "worldengine/client.h"
#include "recording_transport.h"

namespace {

// Verify a HeaderList contains an exact (name, value) pair. Uses linear scan
// because HttpHeaders is a vector (order-preserving, allows duplicates).
bool HasHeader(const worldengine::HttpHeaders& hs,
               const std::string& name,
               const std::string& value) {
    for (const auto& kv : hs) {
        if (kv.first == name && kv.second == value) return true;
    }
    return false;
}

// Case-sensitive name lookup helper (StaticApiKeyAuth emits "Authorization").
bool HasHeaderName(const worldengine::HttpHeaders& hs, const std::string& name) {
    for (const auto& kv : hs) if (kv.first == name) return true;
    return false;
}

// Custom provider used in the "explicit auth overrides api_key" case.
// Kept small and inline so the test file is self-contained.
class FixedHeadersAuth : public worldengine::IAuthProvider {
public:
    explicit FixedHeadersAuth(worldengine::HttpHeaders headers)
        : headers_(std::move(headers)) {}

    worldengine::HttpHeaders GetHeaders() override { return headers_; }

private:
    worldengine::HttpHeaders headers_;
};

} // namespace

// Invariant: StaticApiKeyAuth's contract is one `Authorization: Bearer <key>`
// header and nothing else — no Content-Type (that's transport-owned), no
// duplicates.
TEST_CASE("StaticApiKeyAuth emits exactly one Authorization: Bearer header",
          "[auth][static]") {
    worldengine::StaticApiKeyAuth auth("sk_test_xyz");
    const auto hs = auth.GetHeaders();

    REQUIRE(hs.size() == 1u);
    CHECK(hs[0].first  == "Authorization");
    CHECK(hs[0].second == "Bearer sk_test_xyz");
}

// Invariant (tier 2): setting Config::api_key alone (no explicit provider)
// causes Client::Initialize to auto-construct a StaticApiKeyAuth, and every
// batch carries the resulting bearer header.
TEST_CASE("Config::api_key auto-wires StaticApiKeyAuth on every batch",
          "[auth][autowire]") {
    auto recorder = std::make_shared<worldengine::testing::RecordingTransport>();

    worldengine::Config cfg;
    cfg.api_key                = "sk_test_xyz";
    cfg.project_id             = "proj-auth";
    cfg.batch_size             = 5;
    cfg.flush_interval_seconds = 60.0;
    cfg.transport              = recorder;
    // cfg.auth intentionally left null: exercises the auto-wire path.

    worldengine::Client client;
    client.Initialize(cfg);

    for (int i = 0; i < 12; ++i) client.Track("e", {});
    client.Shutdown();

    const auto headers_per_batch = recorder->header_sets();
    REQUIRE(!headers_per_batch.empty());
    REQUIRE(headers_per_batch.size() == recorder->batch_count());

    for (const auto& hs : headers_per_batch) {
        CHECK(HasHeader(hs, "Authorization", "Bearer sk_test_xyz"));
    }
}

// Invariant (tier 3): neither api_key nor explicit auth means the transport
// gets an empty HttpHeaders vector. This is the demo/local-dev mode.
TEST_CASE("No api_key and no auth => transport receives empty headers vector",
          "[auth][unauthenticated]") {
    auto recorder = std::make_shared<worldengine::testing::RecordingTransport>();

    worldengine::Config cfg;
    cfg.project_id             = "proj-noauth";
    cfg.batch_size             = 1000;
    cfg.flush_interval_seconds = 3600.0;
    cfg.transport              = recorder;
    // api_key empty, auth null.

    worldengine::Client client;
    client.Initialize(cfg);
    client.Track("a", {});
    client.Track("b", {});
    client.Shutdown();

    const auto headers_per_batch = recorder->header_sets();
    REQUIRE(!headers_per_batch.empty());
    for (const auto& hs : headers_per_batch) {
        CHECK(hs.empty());
        CHECK(!HasHeaderName(hs, "Authorization"));
    }
}

// Invariant (tier 1): if both Config::auth and Config::api_key are set, the
// explicit provider wins and the api_key auto-wire is skipped entirely.
// This is what makes it possible to keep an api_key in a config file while
// still A/B-testing a new auth provider without editing config schemas.
TEST_CASE("Explicit Config::auth overrides api_key auto-wiring",
          "[auth][override]") {
    auto recorder = std::make_shared<worldengine::testing::RecordingTransport>();

    // Fixed headers that a naive api_key auto-wire would NOT produce.
    auto custom = std::make_shared<FixedHeadersAuth>(worldengine::HttpHeaders{
        {"X-Custom-Token", "custom-value-42"},
        {"X-Tenant",       "tenant-alpha"},
    });

    worldengine::Config cfg;
    cfg.api_key                = "sk_should_be_ignored";
    cfg.project_id             = "proj-override";
    cfg.batch_size             = 1000;
    cfg.flush_interval_seconds = 3600.0;
    cfg.transport              = recorder;
    cfg.auth                   = custom;

    worldengine::Client client;
    client.Initialize(cfg);
    client.Track("a", {});
    client.Shutdown();

    const auto headers_per_batch = recorder->header_sets();
    REQUIRE(!headers_per_batch.empty());
    for (const auto& hs : headers_per_batch) {
        CHECK(HasHeader(hs, "X-Custom-Token", "custom-value-42"));
        CHECK(HasHeader(hs, "X-Tenant",       "tenant-alpha"));
        // The static-key auto-wiring must have been skipped entirely.
        CHECK(!HasHeaderName(hs, "Authorization"));
    }
}
