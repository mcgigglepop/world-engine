// Verify the canonical event JSON matches the shape (fields, types, and values).


#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "worldengine/event.h"

// Invariant: SerializeEvent emits every canonical field with the right type
// and value. If someone renames "timestamp" -> "ts" or turns level into a
// string, this test breaks loudly.
TEST_CASE("Event serializes to canonical JSON with all required fields", "[serialization]") {
    worldengine::Event e;
    e.event_id     = "11111111-1111-4111-8111-111111111111";
    e.project_id   = "proj-abc";
    e.session_id   = "22222222-2222-4222-8222-222222222222";
    e.user_id      = "user-42";
    e.event_name   = "player_death";
    e.timestamp_ms = 1710000000000LL;
    e.sdk_version  = "0.1.0-mvp";
    e.platform     = "macos";
    e.properties["level"] = std::int64_t{7};
    e.properties["enemy"] = std::string{"dragon"};

    const auto j = nlohmann::json::parse(worldengine::SerializeEvent(e));

    REQUIRE(j.contains("event_id"));
    REQUIRE(j.contains("project_id"));
    REQUIRE(j.contains("session_id"));
    REQUIRE(j.contains("user_id"));
    REQUIRE(j.contains("event_name"));
    REQUIRE(j.contains("timestamp"));
    REQUIRE(j.contains("sdk_version"));
    REQUIRE(j.contains("platform"));
    REQUIRE(j.contains("properties"));

    CHECK(j["event_id"].get<std::string>()    == e.event_id);
    CHECK(j["project_id"].get<std::string>()  == "proj-abc");
    CHECK(j["session_id"].get<std::string>()  == e.session_id);
    CHECK(j["user_id"].get<std::string>()     == "user-42");
    CHECK(j["event_name"].get<std::string>()  == "player_death");
    CHECK(j["timestamp"].is_number_integer());
    CHECK(j["timestamp"].get<std::int64_t>()  == 1710000000000LL);
    CHECK(j["sdk_version"].get<std::string>() == "0.1.0-mvp");
    CHECK(j["platform"].get<std::string>()    == "macos");

    REQUIRE(j["properties"].is_object());
    CHECK(j["properties"]["level"].get<std::int64_t>() == 7);
    CHECK(j["properties"]["enemy"].get<std::string>()  == "dragon");
}

// Invariant: SerializeEventPretty produces multi-line output that still parses
// as JSON. Guards against a future formatter change that accidentally emits
// something like `dump(-2)` and produces invalid JSON.
TEST_CASE("Pretty serialization is multi-line and parses", "[serialization]") {
    worldengine::Event e;
    e.event_id   = "abc";
    e.event_name = "hello";

    const auto pretty = worldengine::SerializeEventPretty(e);
    REQUIRE(pretty.find('\n') != std::string::npos);
    REQUIRE_NOTHROW(nlohmann::json::parse(pretty));
}
