// Copyright (c) 2026 world engine contributors
// Small internal UUID v4 helper. Not part of the public API — used by client.cpp
// for both `session_id` and per-event `event_id`.
//
// Role in the SDK: gives every event a globally-unique-enough id so ingest can
// deduplicate on replay (idea.txt s.11 talks about retry — retried batches will
// reach the server with identical event_ids and can be dropped by the writer).
//
// Threading: NewUuidV4() is safe to call from any thread; the RNG is
// thread_local so concurrent Track() calls do not contend on a shared mutex.

#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>

namespace worldengine::internal {

// Returns a 36-char UUID v4 string like "11111111-1111-4111-8111-111111111111".
//
// What this guarantees: uniqueness that is more than sufficient for event
// identity across sessions and processes (2^122 random bits per id).
//
// What this does NOT guarantee: cryptographic unpredictability. std::mt19937_64
// is a well-known predictable PRNG once you've observed enough of its output;
// std::random_device seeding on some platforms is also not cryptographic-grade.
// Do NOT reuse this for auth tokens, session cookies, CSRF nonces, or anything
// where predictability is an attack — for those, use a real CSPRNG. For event
// telemetry ids, this is plenty.

inline std::string NewUuidV4() {
    // Thread-local RNG so concurrent Track() calls don't contend on a mutex.
    // Seeded once per thread from std::random_device.
    thread_local std::mt19937_64 rng{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> dist;

    // Fill 16 bytes with two 64-bit draws. Big-endian byte order into the array
    // so the hex string reads left-to-right the way the draws generated it.
    std::array<std::uint8_t, 16> b{};
    std::uint64_t hi = dist(rng);
    std::uint64_t lo = dist(rng);
    for (int i = 0; i < 8; ++i) {
        b[i]     = static_cast<std::uint8_t>((hi >> (8 * (7 - i))) & 0xFFu);
        b[8 + i] = static_cast<std::uint8_t>((lo >> (8 * (7 - i))) & 0xFFu);
    }
    // Stamp the v4 version (upper nibble of byte 6 = 0100) and the RFC 4122
    // variant (top two bits of byte 8 = 10). Without these an id would be
    // "shaped like" a UUID but not a spec-compliant v4; some parsers reject it.
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0Fu) | 0x40u);
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3Fu) | 0x80u);

    // Hex-encode with hyphens at positions 8, 13, 18, 23 (the canonical
    // 8-4-4-4-12 layout).
    static const char* kHex = "0123456789abcdef";
    std::string s;
    s.reserve(36);
    for (int i = 0; i < 16; ++i) {
        s.push_back(kHex[b[i] >> 4]);
        s.push_back(kHex[b[i] & 0x0Fu]);
        if (i == 3 || i == 5 || i == 7 || i == 9) s.push_back('-');
    }
    return s;
}

}