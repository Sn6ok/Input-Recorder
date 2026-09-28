# Security & Privacy

Input Recorder is a **personal, local** input-recovery utility. Its security
model is defined by hard boundaries it never crosses, and by concrete mitigations
in the code. This document is the single reference for that posture.

## Hard boundaries (never crossed)

- **Local only.** No network, cloud sync, telemetry, analytics, auto-update or
  remote access of any kind. Enforced in CI by `scripts/security-scan.sh`, which
  fails the build if first-party code references any networking API
  (Winsock/WinINet/WinHTTP/sockets/etc.).
- **No surveillance features.** No stealth/hidden mode, screenshots, microphone,
  camera or screen capture. The recording state is always visible
  (● RECORDING / ○ PAUSED) and fully user-controlled (pause/resume, delete,
  retention, clear-all).
- **No input injection.** The app never synthesizes input into other
  applications (`SendInput`/`keybd_event`/`mouse_event` are forbidden and
  scanned for).
- **No process/code injection.** No `CreateRemoteThread`, `WriteProcessMemory`,
  `VirtualAllocEx`, DLL injection, or loading of external code
  (`LoadLibrary`/`GetProcAddress` are forbidden and scanned for; SQLite is built
  with `SQLITE_OMIT_LOAD_EXTENSION`).
- **No privilege escalation.** Runs as a normal user; the manifest requests
  `asInvoker` (no `requireAdministrator`). No `AdjustTokenPrivileges`.
- **Respects Windows security boundaries.** Uses only documented, user-level
  APIs (`SetWindowsHookEx`, `AddClipboardFormatListener`, `SetWinEventHook`).

## Credential / secret mitigations

Global input capture is the product's mechanism, so the app takes concrete steps
to avoid recording secrets:

- **Password fields are skipped.** While a standard Win32 password control
  (`ES_PASSWORD` edit/rich-edit) is focused, keystrokes are **not** recorded —
  no keystroke event and no resolved text (`security::password_field_focused` +
  the keyboard hook's sensitive guard). This is best-effort: apps with custom
  password fields (some browsers) cannot be detected from outside, and this is
  stated plainly rather than overclaimed.
- **Clipboard exclusion is honored.** Clipboard changes carrying the
  `ExcludeClipboardContentFromMonitorProcessing` marker (set by password managers
  and other sensitive sources) are ignored and never stored.
- **No credential pipeline.** There is no code path that targets, extracts, or
  specially handles passwords, tokens or secrets.

## Data handling

- **All data stays on the device**, under `%LOCALAPPDATA%\InputRecorder\` (a
  per-user location with default user-only ACLs). Nothing is transmitted.
- **User control.** Pause/resume, delete history, "clear all", age-based
  retention and a storage size cap are all user-facing.
- **SQL injection.** Every query uses prepared statements with bound parameters;
  full-text search sanitizes arbitrary user input into a quoted FTS5 MATCH
  expression (`make_fts_match`), so search text can never be interpreted as query
  syntax (covered by `tests/security/security_test.cpp`).
- **Diagnostics never contain user text.** Internal diagnostic markers carry only
  fixed messages and are excluded from the searchable index (spec §322), verified
  by test.
- **Untrusted input.** All persisted/buffered data is length-checked on decode
  (`ByteReader` is bounds-safe; deserialization fails closed on malformed data).

## Truthful documentation

Global input capture is a *mechanism*; the product is a recovery utility. The
documentation never claims capture is absent when it is used, and never presents
an approximate reconstruction as guaranteed-correct (a confidence level is shown).
