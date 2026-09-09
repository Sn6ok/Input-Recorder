#pragma once

// Compact binary (de)serialization for events (spec §166, §167 hybrid: typed
// top-level fields + a serialized payload blob; and §480 serialization tests).
//
// Two granularities:
//   * payload only  -> the `payload` blob stored alongside typed columns.
//   * full event    -> a self-contained record for the event queue / emergency
//                       buffer / tests.

#include <cstddef>
#include <vector>

#include "core/event.hpp"

namespace ir {

// Current on-disk/wire format version. Bump on any layout change.
inline constexpr std::uint8_t kEventFormatVersion = 1;

// Serializes only the type-specific payload (variant discriminant + fields).
std::vector<std::byte> serialize_payload(const EventPayload& payload);

// Restores a payload blob produced by serialize_payload. Returns false on a
// malformed/truncated blob (out is left in a valid but unspecified state).
bool deserialize_payload(const std::vector<std::byte>& bytes, EventPayload& out);

// Serializes a complete event (all fields + payload), prefixed with the format
// version.
std::vector<std::byte> serialize_event(const Event& event);

// Restores a complete event. Returns false on a bad version or truncated data.
bool deserialize_event(const std::vector<std::byte>& bytes, Event& out);

}  // namespace ir
