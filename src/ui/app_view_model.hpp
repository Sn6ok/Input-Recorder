#pragma once

// The OS-independent state behind the main window (spec §566-§575). Holds the
// recording status and the current reconstructed text + confidence, and exposes
// exactly what the window renders: the "● RECORDING / ○ PAUSED" indicator, the
// read-only text (or a placeholder when empty), a confidence note, and the
// "Copy All" payload. Keeping this OS-free means the window is a thin shell and
// all display logic is unit-tested.

#include <cstdint>
#include <string>
#include <string_view>

#include "core/confidence.hpp"

namespace ir {

enum class RecordingStatus : std::uint8_t { Recording = 0, Paused = 1 };

inline bool is_recording(RecordingStatus s) {
    return s == RecordingStatus::Recording;
}
inline RecordingStatus toggled(RecordingStatus s) {
    return is_recording(s) ? RecordingStatus::Paused : RecordingStatus::Recording;
}

// The visible recording indicator (spec §567): a filled dot while recording, a
// hollow dot while paused, with an uppercase label.
std::string status_indicator(RecordingStatus s);

class AppViewModel {
public:
    RecordingStatus status() const { return status_; }
    void set_status(RecordingStatus s) { status_ = s; }
    RecordingStatus toggle_status() {
        status_ = toggled(status_);
        return status_;
    }
    std::string status_indicator() const { return ir::status_indicator(status_); }

    // Update the reconstructed text shown in the read-only view.
    void set_reconstruction(std::string text, Confidence confidence) {
        text_ = std::move(text);
        confidence_ = confidence;
    }
    const std::string& text() const { return text_; }
    Confidence confidence() const { return confidence_; }
    bool has_text() const { return !text_.empty(); }

    // What the read-only control shows: the text, or a placeholder when empty.
    std::string display_text() const;

    // A short human note when the reconstruction is not exact (empty for High).
    std::string confidence_note() const;

    // What the "Copy All" command puts on the clipboard (spec §571): the current
    // reconstructed text, verbatim.
    std::string copy_all_text() const { return text_; }

private:
    RecordingStatus status_ = RecordingStatus::Recording;
    std::string text_;
    Confidence confidence_ = Confidence::High;
};

}  // namespace ir
