#include "mouse/mouse_processor.hpp"

namespace ir {

bool MouseProcessor::movement_due(std::int64_t monotonic_ns) const {
    // A non-positive interval disables throttling (record every sample).
    if (settings_.movement_sampling_ms <= 0) return true;
    if (!has_last_move_) return true;
    const std::int64_t interval_ns =
        static_cast<std::int64_t>(settings_.movement_sampling_ms) * 1'000'000;
    return (monotonic_ns - last_move_monotonic_ns_) >= interval_ns;
}

std::optional<Event> MouseProcessor::process(const RawMouseInput& raw,
                                             Timestamp ts) {
    Event e;
    e.category = EventCategory::UserInput;
    e.time = ts;
    if (raw.injected) e.flags |= EventFlags::Injected;

    MouseData data;
    data.button = raw.button;
    data.x = raw.x;
    data.y = raw.y;

    switch (raw.action) {
        case RawMouseAction::Move: {
            if (!settings_.record_movement) return std::nullopt;
            if (!movement_due(ts.monotonic_ns)) return std::nullopt;
            has_last_move_ = true;
            last_move_monotonic_ns_ = ts.monotonic_ns;
            e.type = EventType::MouseMove;
            data.button = MouseButton::None;
            data.pressed = false;
            break;
        }
        case RawMouseAction::ButtonDown:
        case RawMouseAction::ButtonUp: {
            if (!settings_.record_buttons) return std::nullopt;
            e.type = EventType::MouseButton;
            data.pressed = (raw.action == RawMouseAction::ButtonDown);
            break;
        }
        case RawMouseAction::Wheel: {
            if (!settings_.record_wheel) return std::nullopt;
            e.type = EventType::MouseWheel;
            data.button = MouseButton::None;
            data.pressed = false;
            data.wheel_delta = raw.wheel_delta;
            break;
        }
        default:
            return std::nullopt;
    }

    e.payload = data;
    return e;
}

}  // namespace ir
