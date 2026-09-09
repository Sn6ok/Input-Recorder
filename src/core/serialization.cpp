#include "core/serialization.hpp"

#include "utils/byte_buffer.hpp"

namespace ir {
namespace {

// Payload discriminants match the EventPayload variant alternative order.
enum class PayloadKind : std::uint8_t {
    None = 0,
    Key = 1,
    Text = 2,
    Shortcut = 3,
    Mouse = 4,
    ClipboardRef = 5,
    Diagnostic = 6,
};

void write_payload(ByteWriter& w, const EventPayload& payload) {
    w.put_u8(static_cast<std::uint8_t>(payload.index()));
    std::visit(
        [&w](const auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                // no fields
            } else if constexpr (std::is_same_v<T, KeyEventData>) {
                w.put_u16(data.virtual_key);
                w.put_u16(data.scan_code);
            } else if constexpr (std::is_same_v<T, TextInputData>) {
                w.put_string(data.text);
            } else if constexpr (std::is_same_v<T, ShortcutData>) {
                w.put_u32(data.modifier_flags);
                w.put_u16(data.key_virtual_key);
                w.put_string(data.text);
            } else if constexpr (std::is_same_v<T, MouseData>) {
                w.put_u8(static_cast<std::uint8_t>(data.button));
                w.put_bool(data.pressed);
                w.put_u32(static_cast<std::uint32_t>(data.x));
                w.put_u32(static_cast<std::uint32_t>(data.y));
                w.put_u32(static_cast<std::uint32_t>(data.wheel_delta));
            } else if constexpr (std::is_same_v<T, ClipboardRef>) {
                w.put_u64(data.entry.value);
            } else if constexpr (std::is_same_v<T, DiagnosticData>) {
                w.put_string(data.message);
            }
        },
        payload);
}

bool read_payload(ByteReader& r, EventPayload& out) {
    auto kind = static_cast<PayloadKind>(r.get_u8());
    switch (kind) {
        case PayloadKind::None:
            out = std::monostate{};
            break;
        case PayloadKind::Key: {
            KeyEventData d;
            d.virtual_key = r.get_u16();
            d.scan_code = r.get_u16();
            out = d;
            break;
        }
        case PayloadKind::Text: {
            TextInputData d;
            d.text = r.get_string();
            out = d;
            break;
        }
        case PayloadKind::Shortcut: {
            ShortcutData d;
            d.modifier_flags = r.get_u32();
            d.key_virtual_key = r.get_u16();
            d.text = r.get_string();
            out = d;
            break;
        }
        case PayloadKind::Mouse: {
            MouseData d;
            d.button = static_cast<MouseButton>(r.get_u8());
            d.pressed = r.get_bool();
            d.x = static_cast<std::int32_t>(r.get_u32());
            d.y = static_cast<std::int32_t>(r.get_u32());
            d.wheel_delta = static_cast<std::int32_t>(r.get_u32());
            out = d;
            break;
        }
        case PayloadKind::ClipboardRef: {
            ClipboardRef d;
            d.entry = ClipboardEntryId{r.get_u64()};
            out = d;
            break;
        }
        case PayloadKind::Diagnostic: {
            DiagnosticData d;
            d.message = r.get_string();
            out = d;
            break;
        }
        default:
            return false;
    }
    return r.ok();
}

}  // namespace

std::vector<std::byte> serialize_payload(const EventPayload& payload) {
    ByteWriter w;
    write_payload(w, payload);
    return w.take();
}

bool deserialize_payload(const std::vector<std::byte>& bytes, EventPayload& out) {
    ByteReader r(bytes);
    return read_payload(r, out);
}

std::vector<std::byte> serialize_event(const Event& e) {
    ByteWriter w;
    w.put_u8(kEventFormatVersion);
    w.put_u64(e.id.value);
    w.put_u64(e.session.value);
    w.put_u64(e.context.value);
    w.put_i64(e.time.wall_ms);
    w.put_i64(e.time.monotonic_ns);
    w.put_u8(static_cast<std::uint8_t>(e.type));
    w.put_u8(static_cast<std::uint8_t>(e.category));
    w.put_u32(static_cast<std::uint32_t>(e.flags));
    write_payload(w, e.payload);
    return w.take();
}

bool deserialize_event(const std::vector<std::byte>& bytes, Event& out) {
    ByteReader r(bytes);
    if (r.get_u8() != kEventFormatVersion) return false;
    Event e;
    e.id = EventId{r.get_u64()};
    e.session = SessionId{r.get_u64()};
    e.context = ContextId{r.get_u64()};
    e.time.wall_ms = r.get_i64();
    e.time.monotonic_ns = r.get_i64();
    e.type = static_cast<EventType>(r.get_u8());
    e.category = static_cast<EventCategory>(r.get_u8());
    e.flags = static_cast<EventFlags>(r.get_u32());
    if (!read_payload(r, e.payload)) return false;
    if (!r.ok()) return false;
    out = e;
    return true;
}

}  // namespace ir
