#pragma once

// Startup recovery (spec §462-§476). A previous run that crashed or lost power
// leaves its session row Active and may leave events in the emergency buffer.
// At startup the app flags those sessions Interrupted (so the UI can show the
// session ended abnormally) and replays any buffered events into the database.
// OS-free and unit-tested.

#include <cstdint>

#include "reliability/emergency_buffer.hpp"
#include "storage/event_store.hpp"

namespace ir {

// Marks every still-Active session as Interrupted. Returns how many were
// changed (0 on a clean prior shutdown).
int mark_interrupted_sessions(EventStore& store);

// Replays every intact event from the emergency buffer into the store (one
// transaction) and, on success, clears the buffer. Returns the number of events
// recovered.
int replay_emergency_buffer(EmergencyBuffer& buffer, EventStore& store);

}  // namespace ir
