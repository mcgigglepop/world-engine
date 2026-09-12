// Shutdown() must drain the queue even when nothing has hit batch_size and
// no flush interval has elapsed.
//

#include <memory>

#include <catch2/catch_test_macros.hpp>

#include "worldengine/client.h"
#include "recording_transport.h"

// Invariant: Shutdown() drains pending events even when neither size nor
// interval trigger has fired.
TEST_CASE("Shutdown() flushes pending events", "[shutdown]") {
    auto recorder = std::make_shared<worldengine::testing::RecordingTransport>();

    worldengine::Config cfg;
    cfg.project_id             = "proj-shutdown";
    cfg.user_id                = "u";
    cfg.batch_size             = 1000;    // never hit by count
    cfg.flush_interval_seconds = 3600.0;  // never hit by time
    cfg.transport              = recorder;

    worldengine::Client client;
    client.Initialize(cfg);

    client.Track("a", {});
    client.Track("b", {});
    client.Track("c", {});

    client.Shutdown();

    CHECK(recorder->event_count() == 3u);
    REQUIRE(recorder->batch_count() >= 1u);
}

// Invariant: Flush() is synchronous — it does not return until the worker
// has acknowledged the ticket by draining the queue. Exercises the
// flush_requested / flush_completed ticket dance in Client::Impl.
TEST_CASE("Flush() blocks until queue is drained", "[shutdown][flush]") {
    auto recorder = std::make_shared<worldengine::testing::RecordingTransport>();

    worldengine::Config cfg;
    cfg.project_id             = "proj-flush";
    cfg.batch_size             = 1000;
    cfg.flush_interval_seconds = 3600.0;
    cfg.transport              = recorder;

    worldengine::Client client;
    client.Initialize(cfg);

    for (int i = 0; i < 7; ++i) client.Track("e", {});
    client.Flush();

    CHECK(recorder->event_count() == 7u);
    client.Shutdown();
}
