#include "ui/app_view_model.hpp"

namespace ir {

std::string status_indicator(RecordingStatus s) {
    // U+25CF BLACK CIRCLE (recording) / U+25CB WHITE CIRCLE (paused).
    return is_recording(s) ? "\xE2\x97\x8F RECORDING" : "\xE2\x97\x8B PAUSED";
}

std::string AppViewModel::display_text() const {
    if (annotated_.empty()) {
        return "No text has been reconstructed yet. Start typing in any "
               "application and it will appear here.";
    }
    return annotated_;
}

std::string AppViewModel::confidence_note() const {
    switch (confidence_) {
        case Confidence::High:
            return {};
        case Confidence::Medium:
            return "This reconstruction is approximate — some edits could not be "
                   "tracked precisely.";
        case Confidence::Low:
            return "This reconstruction is uncertain — the cursor position was "
                   "lost at some point.";
        case Confidence::Unknown:
            return "This reconstruction could not be determined reliably.";
    }
    return {};
}

}  // namespace ir
