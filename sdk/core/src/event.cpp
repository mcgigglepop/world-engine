// Copyright (c) 2026 world engine contributors
// Event -> canonical JSON serialization.
//
// Role in the SDK: this is the concrete implementation of the wire contract
// declared in include/worldengine/event.h. Anything that leaves the SDK — real
// HTTP transport, StubTransport for demos, test transports — funnels through
// SerializeEvent/SerializeEventPretty. The canonical JSON shape is identical
// across every worldengine SDK (Unity/Unreal/Web), so
// changes here are effectively API changes for the whole platform.
//
// Design notes:
//  - We route every value through nlohmann::json so property values are emitted
//    with their real JSON types (numbers as numbers, bools as bools, etc.), not
//    stringified.
//  - Field ordering is fixed by construction so the on-wire output is stable
//    and easy to eyeball in the demo. (nlohmann::json's default object is
//    sorted by key, but assigning in a fixed sequence lets us document intent.)

#include "worldengine/event.h"
#include <nlohmann/json.hpp>
#include <variant>

namespace worldengine {
namespace {

// Turn a PropertyValue into a JSON scalar of the correct type.
// std::visit dispatches on the currently-held alternative; the constexpr-if
// chain gets folded to a single branch per alternative at compile time. The
// terminal `else` is unreachable given the variant's closed set of types, but
// keeps the compiler happy about exhaustiveness.
nlohmann::json PropertyToJson(const PropertyValue& v) {
    return std::visit(
        [](auto&& arg) -> nlohmann::json {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::string>) return nlohmann::json(arg);
            else if constexpr (std::is_same_v<T, std::int64_t>) return nlohmann::json(arg);
            else if constexpr (std::is_same_v<T, double>) return nlohmann::json(arg);
            else if constexpr (std::is_same_v<T, bool>) return nlohmann::json(arg);
            else return nlohmann::json(nullptr);
        },
        v);
}

// Build the canonical JSON object. Kept private so both serializers share it.
nlohmann::json EventToJson(const Event& e) {
    nlohmann::json j;
    j["event_id"]    = e.event_id;
    j["project_id"]  = e.project_id;
    j["session_id"]  = e.session_id;
    j["user_id"]     = e.user_id;
    j["event_name"]  = e.event_name;
    j["timestamp"]   = e.timestamp_ms;
    j["sdk_version"] = e.sdk_version;
    j["platform"]    = e.platform;

    // Build the properties sub-object separately so nested types stay typed
    // rather than collapsing to a big string.
    nlohmann::json props = nlohmann::json::object();
    for (const auto& [k, v] : e.properties) {
        props[k] = PropertyToJson(v);
    }
    j["properties"] = std::move(props);
    return j;
}

} // namespace

// Compact single-line render — this is what a real HTTP transport would send.
std::string SerializeEvent(const Event& e) {
    return EventToJson(e).dump();
}

// Two-space-indent render — used only by StubTransport for readable demo output.
std::string SerializeEventPretty(const Event& e) {
    return EventToJson(e).dump(/*indent=*/2);
}

}