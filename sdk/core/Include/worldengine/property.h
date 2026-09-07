// property value types and container for event properties.
//
// this is the leaf-most public header. both Event and the
// Client public API include it so game code can hand typed values to Track()
// without touching JSON. nothing here depends on threading, transport, or auth.
// the canonical event's `properties` field is a heterogeneous scalar map.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

namespace worldengine {

// a property value is one of: string, 64-bit integer, double, boolean.
// this mirrors the canonical JSON scalar types used on the wire.
//
// why std::variant and not std::string: we want `{"level": 7}` on the wire, not
// `{"level": "7"}`. preserving JSON scalar types end-to-end lets ClickHouse
// (and the AI analyst downstream) run typed queries — e.g. numeric aggregations
// on `damage`, boolean filters on `critical_hit` — without every consumer having
// to parse strings back to numbers. 
using PropertyValue = std::variant<std::string, std::int64_t, double, bool>;

// convenience alias: a bag of named properties attached to an event.
// unordered because property ordering is not part of the canonical contract;
// serialization uses whatever iteration order the map yields.
using Properties = std::unordered_map<std::string, PropertyValue>;

} 