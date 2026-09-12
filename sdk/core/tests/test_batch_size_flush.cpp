// With batch_size=10 and 25 events tracked, we expect at least 2 full-size
// batches to be emitted by the size trigger, and the remaining 5 to be
// delivered on Shutdown.

#include <chrono>
#include <memory>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "worldengine/client.h"
#include "recording_transport.h"

// Invariant: batch_size drives at least two full-size flushes during the run
// and Shutdown drains whatever's left. Interval is set high so the timer
// trigger cannot be the cause of any drain we observe.
TEST_CASE("batch_size drives multiple flushes; Shutdown drains the remainder",
          "[batching]") {
    auto recorder = std::make_shared<worldengine::testing::RecordingTransport>();

    worldengine::Config cfg;
    cfg.project_id             = "proj-batch";
    cfg.user_id                = "u";
    cfg.batch_size             = 10;
    cfg.flush_interval_seconds = 60.0;
    cfg.transport              = recorder;

    worldengine::Client client;
    client.Initialize(cfg);

    for (int i = 0; i < 25; ++i) {
        client.Track("event", { {"i", static_cast<std::int64_t>(i)} });
    }

    client.Shutdown();

    CHECK(recorder->event_count() == 25u);

    // At least two batches were emitted by the size trigger before shutdown,
    // giving us >= 3 batches total. We don't assert a strict count because
    // the worker may coalesce the tail on Shutdown into one or more batches.
    const auto batches = recorder->batches();
    REQUIRE(batches.size() >= 3);

    int full_batches = 0;
    for (const auto& b : batches) if (b.size() == 10u) ++full_batches;
    CHECK(full_batches >= 2);
}
