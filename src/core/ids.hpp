#pragma once

// Strongly-typed 64-bit identifiers.
//
// Distinct tags stop an EventId from being accidentally passed where a
// SessionId is expected. The underlying value is a plain uint64_t so IDs bind
// cleanly to SQLite and serialize trivially. 0 is reserved as "none/invalid".

#include <cstdint>
#include <functional>

namespace ir {

template <typename Tag>
struct Id {
    std::uint64_t value = 0;

    constexpr Id() = default;
    constexpr explicit Id(std::uint64_t v) : value(v) {}

    constexpr bool valid() const { return value != 0; }

    friend constexpr bool operator==(Id a, Id b) { return a.value == b.value; }
    friend constexpr bool operator!=(Id a, Id b) { return a.value != b.value; }
    friend constexpr bool operator<(Id a, Id b) { return a.value < b.value; }
    friend constexpr bool operator>(Id a, Id b) { return a.value > b.value; }
    friend constexpr bool operator<=(Id a, Id b) { return a.value <= b.value; }
    friend constexpr bool operator>=(Id a, Id b) { return a.value >= b.value; }
};

struct EventIdTag {};
struct SessionIdTag {};
struct ContextIdTag {};
struct ClipboardEntryIdTag {};
struct SnapshotIdTag {};

using EventId = Id<EventIdTag>;
using SessionId = Id<SessionIdTag>;
using ContextId = Id<ContextIdTag>;
using ClipboardEntryId = Id<ClipboardEntryIdTag>;
using SnapshotId = Id<SnapshotIdTag>;

}  // namespace ir

// Enable use as keys in unordered containers.
namespace std {
template <typename Tag>
struct hash<ir::Id<Tag>> {
    std::size_t operator()(const ir::Id<Tag>& id) const noexcept {
        return std::hash<std::uint64_t>{}(id.value);
    }
};
}  // namespace std
