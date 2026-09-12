// 4 producer threads pump 250 events each. The transport (a
// RecordingTransport) must observe all 1000 events after Shutdown.
//
// Why we care: Track() is documented as thread-safe. This test is the
// smoking gun for the mutex + condition_variable design in Client::Impl —
// if a race dropped events or double-counted them, the total would drift
// from the expected 1000.

#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "worldengine/client.h"
#include "recording_transport.h"

// File-scope constants (not locals of TEST_CASE) so lambdas can reference
// them without capture on both MSVC and clang — MSVC's odr-use interpretation
// requires the capture, clang's -Wunused-lambda-capture warns against it.
// Moving them out of the enclosing function sidesteps both compilers.
namespace {
constexpr int kThreads         = 4;
constexpr int kEventsPerThread = 250;
}  // namespace

// Invariant: no event is lost across concurrent Track() calls. flush_interval
// is set very high so this stresses the batch-size + shutdown drain paths,
// not the timer path.
TEST_CASE("Concurrent Track() calls all reach the transport", "[threading]") {
    auto recorder = std::make_shared<worldengine::testing::RecordingTransport>();

    worldengine::Config cfg;
    cfg.project_id             = "proj-thread";
    cfg.user_id                = "u";
    cfg.batch_size             = 50;
    cfg.flush_interval_seconds = 60.0;   // long: don't let interval races muddy the count
    cfg.transport              = recorder;

    worldengine::Client client;
    client.Initialize(cfg);

    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        workers.emplace_back([&client, t] {
            for (int i = 0; i < kEventsPerThread; ++i) {
                client.Track("player_move", {
                    {"thread", static_cast<std::int64_t>(t)},
                    {"i",      static_cast<std::int64_t>(i)},
                });
            }
        });
    }
    for (auto& w : workers) w.join();

    client.Shutdown();

    CHECK(recorder->event_count() == static_cast<std::size_t>(kThreads * kEventsPerThread));
}