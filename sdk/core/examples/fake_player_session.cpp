// simulates ~88 events from a single "player session" using the
// StubTransport, so the SDK is visibly working without a backend.

// this is the "hello world" for the core. It exercises
// Initialize -> many Track() calls of different event types -> Shutdown, and
// leans on StubTransport to prove the batching, header stamping, and
// canonical JSON shape all work end-to-end. nothing here mirrors production
// wiring beyond the API surface — a real game would use FWorldEngineSubsystem
// (Unreal) or MonoBehaviour lifecycle hooks (Unity) as the ownership site.

// expected output: multiple "--- WorldEngine batch (25 events) ---" banners
// followed by pretty-printed JSON arrays. the final (smaller) batch is
// flushed on Shutdown(). each batch is preceded by the
// `Authorization: Bearer sk_demo_... (redacted)` header banner so you can
// see the auth flow visibly happening.

#include <cstdint>
#include <string>

#include "worldengine/client.h"

int main() {
    worldengine::Config cfg;

    cfg.api_key                = "sk_demo_expo_abcdef123456";
    cfg.project_id             = "demo-expo";
    cfg.ingest_url             = "https://example.invalid/ingest";
    cfg.user_id                = "player-42";
    cfg.batch_size             = 25;
    cfg.flush_interval_seconds = 30.0;   // long: we want batch-size to drive flushes here

    worldengine::Client client;
    client.Initialize(cfg);

    // the events below intentionally exercise every PropertyValue variant
    // (int64, double, string, bool) so the demo output shows typed JSON
    // fields, not stringified ones 

    // 1x level_started
    client.Track("level_started", {
        {"level", std::int64_t{7}},
    });

    // 60x player_move — this alone crosses the batch_size=25 threshold
    // multiple times, so you'll see back-to-back full batches emitted.
    for (int i = 0; i < 60; ++i) {
        client.Track("player_move", {
            {"x", static_cast<double>(i) * 1.5},
            {"y", static_cast<double>(i) * -0.75},
        });
    }

    // 10x player_shot — mixes string + bool properties.
    for (int i = 0; i < 10; ++i) {
        client.Track("player_shot", {
            {"weapon", std::string{"shotgun"}},
            {"critical_hit", (i % 3) == 0},
        });
    }

    // 15x enemy_hit — mixes double + string properties.
    for (int i = 0; i < 15; ++i) {
        client.Track("enemy_hit", {
            {"damage", 12.5 + static_cast<double>(i)},
            {"enemy", std::string{"goblin"}},
        });
    }

    // 1x player_death — the "canonical" example event from the docs.
    client.Track("player_death", {
        {"enemy", std::string{"dragon"}},
        {"level", std::int64_t{7}},
    });

    // 1x level_completed
    client.Track("level_completed", {
        {"level", std::int64_t{7}},
        {"success", false},
    });

    // drains queue synchronously and joins the worker thread. This is where
    // the final partial batch (whatever's still in the queue below
    // batch_size) gets flushed to StubTransport.
    client.Shutdown();
    return 0;
}