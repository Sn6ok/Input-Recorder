#pragma once

// The single source of truth for user settings (spec §252, §268, §294). Loads
// the Configuration from the storage `settings` table at startup, validates it,
// persists changes, and notifies listeners so the capture sources, UI and theme
// react to edits immediately. OS-free and unit-tested against an in-memory
// EventStore. Thread-safe: config() returns a snapshot under a lock.

#include <cstdint>
#include <functional>
#include <mutex>
#include <utility>
#include <vector>

#include "core/configuration.hpp"
#include "storage/event_store.hpp"

namespace ir {

class SettingsService {
public:
    using Listener = std::function<void(const Configuration&)>;

    explicit SettingsService(EventStore& store) : store_(store) {}

    // Loads settings from storage (validated). Missing keys keep their defaults;
    // an empty store yields defaults. Returns false only on a storage error.
    bool load();

    // Persists the current configuration. Returns false on a storage error.
    bool save();

    // A snapshot of the current configuration (thread-safe).
    Configuration config() const;

    // Replaces the configuration (validated), persists it, and notifies
    // listeners. Returns save() success.
    bool update(const Configuration& c);

    // Applies an in-place edit, then validates/saves/notifies. Convenient for
    // toggling a single field. Returns save() success.
    template <typename Fn>
    bool edit(Fn&& mutate) {
        Configuration c = config();
        mutate(c);
        return update(c);
    }

    // Registers a change listener; returns an id for removal. The listener is
    // invoked (outside the internal lock) on every successful update.
    int add_listener(Listener listener);
    void remove_listener(int id);

private:
    void notify(const Configuration& c);

    EventStore& store_;
    mutable std::mutex mutex_;
    Configuration config_{};
    std::vector<std::pair<int, Listener>> listeners_;
    int next_listener_id_ = 1;
};

}  // namespace ir
