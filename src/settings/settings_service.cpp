#include "settings/settings_service.hpp"

namespace ir {

bool SettingsService::load() {
    std::map<std::string, std::string> kv = store_.load_settings();
    Configuration loaded = configuration_from_key_values(kv);
    validate(loaded);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = loaded;
    }
    return true;
}

bool SettingsService::save() {
    Configuration snapshot = config();
    return store_.save_settings(to_key_values(snapshot));
}

Configuration SettingsService::config() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

bool SettingsService::update(const Configuration& c) {
    Configuration validated = c;
    validate(validated);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        config_ = validated;
    }
    const bool ok = store_.save_settings(to_key_values(validated));
    notify(validated);  // listeners see the new config even if persistence failed
    return ok;
}

int SettingsService::add_listener(Listener listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int id = next_listener_id_++;
    listeners_.emplace_back(id, std::move(listener));
    return id;
}

void SettingsService::remove_listener(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = listeners_.begin(); it != listeners_.end(); ++it) {
        if (it->first == id) {
            listeners_.erase(it);
            return;
        }
    }
}

void SettingsService::notify(const Configuration& c) {
    // Copy the listener list under the lock, then invoke outside it so a
    // listener may safely read config() or (un)register listeners.
    std::vector<std::pair<int, Listener>> snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        snapshot = listeners_;
    }
    for (auto& [id, listener] : snapshot) {
        (void)id;
        if (listener) listener(c);
    }
}

}  // namespace ir
