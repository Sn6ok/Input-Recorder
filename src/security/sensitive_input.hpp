#pragma once

// Best-effort detection of sensitive input contexts so the recorder can avoid
// capturing credentials (spec: never capture passwords/tokens/secrets). This is
// a mitigation, not a guarantee — many apps (browsers) use custom password
// fields that cannot be detected from outside — so the product documents that
// standard Win32 password fields (ES_PASSWORD edit controls) are skipped.
// Windows-only; the header avoids <Windows.h>.

namespace ir {

// True when the focused control is a standard password entry field.
bool password_field_focused();

}  // namespace ir
