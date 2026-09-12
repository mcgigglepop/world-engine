// Each PropertyValue alternative must round-trip to the correct JSON type,
// not a stringified version of it.
//
// Why we care: the whole reason PropertyValue is a std::variant (not a plain
// std::string) is that downstream analytics needs to treat numbers as numbers
// and booleans as booleans. If someone accidentally regresses PropertyToJson
// so that `std::int64_t{7}` becomes `"7"` on the wire, this test catches it.

#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "worldengine/event.h"

// Invariant: every PropertyValue alternative renders as its native JSON type
// (string / number / float / boolean), never coerced to string.
TEST_CASE("Property variants serialize to native JSON types", "[serialization][variants]") {
    worldengine::Event e;
    e.event_name           = "types";
    e.properties["s"]      = std::string{"hello"};
    e.properties["i"]      = std::int64_t{-42};
    e.properties["d"]      = 3.14;
    e.properties["b_true"] = true;
    e.properties["b_false"] = false;

    const auto j = nlohmann::json::parse(worldengine::SerializeEvent(e));
    const auto& p = j.at("properties");

    CHECK(p.at("s").is_string());
    CHECK(p.at("s").get<std::string>() == "hello");

    CHECK(p.at("i").is_number_integer());
    CHECK(p.at("i").get<std::int64_t>() == -42);

    CHECK(p.at("d").is_number_float());
    CHECK(p.at("d").get<double>() == 3.14);

    CHECK(p.at("b_true").is_boolean());
    CHECK(p.at("b_true").get<bool>() == true);

    CHECK(p.at("b_false").is_boolean());
    CHECK(p.at("b_false").get<bool>() == false);
}
