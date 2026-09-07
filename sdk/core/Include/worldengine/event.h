// canonical event shape shared across engines and transports.
//
// this struct is the "wire contract" — the exact JSON shape
// emitted by every worldengine SDK (Unity/Unreal/Web/etc). the ingest server,
// ClickHouse schema, and AI analyst all depend on these field names and types.
// if you rename a field here you must update every downstream consumer.
#pragma once

#include <cstdint>
#include <string>

#include "worldengine/property.h"

namespace worldengine {

// canonical event as it appears on the wire. field names match the
// project's cross-SDK contract.
//
// populated in two stages:
// 1. Client::Track() fills every field except `properties` (which the caller
//    supplied). This happens on the game thread.
// 2. The transport reads it read-only when serializing. nothing mutates an
//    Event after it enters the queue.
struct Event {
    std::string   event_id;         // UUID v4-ish (see src/uuid.h)
    std::string   project_id;       // routing / tenant scope
    std::string   session_id;       // stable for the life of one Client instance
    std::string   user_id;          // optional; empty allowed
    std::string   event_name;       // caller-supplied ("player_death" etc.)
    std::int64_t  timestamp_ms{0};  // epoch milliseconds, worker-independent
    std::string   sdk_version;      // filled from kSdkVersion
    std::string   platform;         // filled from WORLDENGINE_PLATFORM macro
    Properties    properties;       // typed scalar bag; see property.h
};

// serialize an event to canonical JSON text (compact, single-line).
// used by a real HTTP transport and by StubTransport (via re-parse). never
// throws for well-formed Event values. propertyValue is closed over four
// concrete alternatives so std::visit is exhaustive.
std::string SerializeEvent(const Event& e);

// serialize an event to canonical JSON text (pretty-printed with two-space
// indent). used by StubTransport for the demo output.
std::string SerializeEventPretty(const Event& e);

} 
