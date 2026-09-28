# Input Recorder — Architecture

This document tracks the high-level design and the phased implementation plan.
It is updated as each phase lands.

## Product scope (guardrails)

Input Recorder is a **personal, local input-recovery utility** for Windows 11.
These constraints are hard requirements and shape every design decision:

- **Local only.** No network, cloud sync, telemetry, analytics or remote access.
- **Visible & user-controlled.** An obvious recording state (● RECORDING /
  ○ PAUSED); the user can pause, resume, hide to tray, delete history and
  configure retention.
- **No surveillance features.** No stealth/hidden mode, screenshots, microphone,
  camera, credential exfiltration or remote administration.
- **Respect Windows security boundaries.** No process injection, no privilege
  escalation, no attempts to read intentionally protected input. Prefer
  documented, user-level Windows APIs. No Administrator requirement.
- **Truthful documentation.** Global input capture is a *mechanism*; the product
  is a recovery utility. Docs never claim capture is absent when it is used.

## Technology

| Area        | Choice                                             |
|-------------|----------------------------------------------------|
| Language    | C++20                                               |
| Compiler    | MSVC (VS 2022 Build Tools), x64                     |
| Build       | CMake + Visual Studio 2022 generator (Release main) |
| GUI         | Win32 API (no Electron/web frontend)                |
| Storage     | SQLite (embedded, `%LOCALAPPDATA%\InputRecorder\`)  |
| Tests       | In-repo header-only framework (no external dep)     |

### Build & verification (Windows vs. non-Windows CI)

The **authoritative** build and test gate is `scripts/build.ps1 -Test
-WarningsAsErrors` on Windows (MSVC, `/W4 /WX`), which compiles everything —
including the Win32 hooks, clipboard, window context, tray and UI — and runs the
full test suite via CTest.

Because most application logic lives in the OS-free `input_recorder_lib`, a
supplementary POSIX harness (`scripts/build-posix-tests.sh`, clang/g++,
`-Wall -Wextra -Wpedantic -Werror`) compiles the OS-independent subset plus the
portable SQLite amalgamation and runs those tests on Linux/macOS CI. This gives a
real green/red signal for the portable core off Windows. Files that include
`<Windows.h>` (the `*_hook`/`*_monitor`/window-context/UI/tray translation units)
are **only** compiled by the Windows build; on other platforms they are verified
by inspection against the proven `KeyboardHook`/`ClipboardMonitor` patterns. Each
phase below notes which parts are exercised by the POSIX harness vs. Windows-only.

### Why an in-repo test framework

The spec forbids adding third-party dependencies without real need and the
build must work offline. A ~150-line header + one runner TU gives us
`TEST_CASE` registration and `CHECK`/`REQUIRE`/`CHECK_EQ` with readable
diagnostics, wired to CTest as a single runner target. See
`tests/framework/`.

## Module layout

Non-UI logic lives in a static library `input_recorder_lib` so it can be unit
tested without a Win32 message loop. The `InputRecorder` executable and the
`ir_tests` runner both link it.

```
src/
  main.cpp            entry point (console during bootstrap; Win32 GUI in Phase 10)
  core/               fundamental types, event model, IDs, time, config (no UI, no OS deps)
  capture/            low-level Windows input capture (WH_KEYBOARD_LL, etc.)
  keyboard/           key event normalization, layout/Unicode translation
  reconstruction/     event stream -> reconstructed text (UI-independent, heavily tested)
  clipboard/          clipboard monitoring + history
  mouse/              mouse button/wheel capture (movement off by default)
  context/            foreground window / process context tracking (+ caching)
  storage/            SQLite persistence behind an interface, async writer
  history/            queries, search, snapshots, retention
  ui/                 Win32 windows, controls, tray, theme
  settings/           settings service (single source of truth)
  system/             lifecycle, single-instance, hotkeys, DPI, power/session events
  utils/              small shared helpers (Unicode conversion, logging)
```

### Dependency direction

```
ui  ->  app/services  ->  capture / reconstruction / storage / ...  ->  core
```

`core` never depends on UI. No circular dependencies.

## Runtime pipeline (target)

```
Windows input -> Capture -> Normalizer -> bounded Event Queue
                                              |-> Reconstruction Engine -> Text State -> UI (read-only)
                                              `-> Storage Worker (async) -> SQLite (local history)
```

Key performance rules: event-driven (no busy polling), the hook callback does
minimal work and returns immediately, disk I/O happens off the capture path via
an async writer, and the UI thread never blocks on I/O.

## Implementation phases

- [x] **Phase 0** — Project bootstrap: CMake, module skeleton, test harness,
  minimal buildable/launchable executable.
- [x] **Phase 1** — Core data model (Event, EventType/Category/Flags, Timestamp,
  Session, Context, ClipboardEntry, TextSnapshot, Confidence, Configuration) +
  compact binary serialization and key/value config round-trip; 29 unit tests,
  clean at `/W4 /WX`.
- [x] **Phase 2** — Event pipeline: `EventQueue`, a bounded thread-safe ring
  buffer. Non-blocking `try_push` (drops + counts on overflow, never blocks the
  producer), batched `wait_and_drain`/`try_drain` for the consumer, clean
  shutdown via `close()`. Stats + overflow flag. 7 tests incl. 20k-event SPSC
  and multi-producer burst.
- [x] **Phase 3** — Keyboard capture. `KeyboardTranslator` (OS-free, tested):
  KeyDown/KeyUp, modifier state, auto-repeat, injected/extended flags.
  `KeyboardHook` (`WH_KEYBOARD_LL` on a dedicated message-loop thread): minimal
  non-blocking callback, bounded install retry/backoff, single-instance guard,
  clean start/stop. Shared `EventIdAllocator`. 8 tests (real keystroke capture
  is exercised in the Phase 12 system-integration run).
- [x] **Phase 4** — Reconstruction engine (OS-free, tested). Code-point text
  buffer with cursor/selection tracking and a `Confidence` model that lowers
  (never fabricates) on unknowns. Handles TextInput, editing/navigation
  KeyDowns (Backspace/Delete/Enter/Tab/arrows/Home/End/word-move), Ctrl
  shortcuts (A/C/X/Z), Paste, and left-click (caret→unknown). Snapshot/restore
  for point-in-time recovery. Pure UTF-8↔UTF-32 utils. 26 tests incl. the
  golden scenarios of §481–§494.
- [x] **Phase 5** — Clipboard subsystem. `ClipboardProcessor` (OS-free, tested):
  consecutive-duplicate suppression, size cap with code-point-safe truncation,
  self-copy suppression (internal Copy All), and Paste-event construction with
  resolved text. `ClipboardMonitor` (Win32): message-only window +
  `AddClipboardFormatListener` (event-driven), bounded-retry `CF_UNICODETEXT`
  read, thread-safe. UTF-16↔UTF-8 utils. 7 tests.
- [x] **Phase 6** — Mouse subsystem. `MouseProcessor` (OS-free, tested): maps
  raw mouse inputs to MouseButton/MouseWheel/MouseMove events, honors the
  record-buttons/wheel/movement toggles, and throttles movement to the
  configured sampling interval (movement off by default). `MouseHook`
  (`WH_MOUSE_LL` on a dedicated message-loop thread): minimal non-blocking
  callback decoding L/R/M/X buttons + vertical/horizontal wheel, single-instance
  guard, bounded retry/backoff, thread-safe runtime settings update. 11 new
  processor tests (POSIX harness) + 3 Win32 lifecycle tests (Windows only).
- [x] **Phase 7** — Window/process context. `ContextTracker` (OS-free, tested):
  turns foreground-window observations into stable Context records with unique
  ids, deduplicates return-visits to a window/title seen this session (cache
  hit reuses the id), treats a title change on the same window as a distinct
  context so history keeps what the window said, and emits no event for a
  re-observed active window. `WindowContextMonitor` (Win32 `SetWinEventHook`
  on EVENT_SYSTEM_FOREGROUND..EVENT_OBJECT_NAMECHANGE, event-driven, no polling):
  reads process base-name + window title, publishes the active ContextId for
  other capture sources to stamp, pushes ContextChanged events and hands Context
  records to a storage sink; single-instance guard + bounded retry. 9 tracker
  tests (POSIX harness) + 2 Win32 lifecycle tests (Windows only).
- [x] **Phase 8** — Storage (SQLite). Vendored the SQLite **3.53.4**
  amalgamation under `third_party/sqlite/` (built as an isolated `sqlite3`
  static lib so its warnings never hit `/W4 /WX`; FTS5 enabled, extension
  loading omitted). `SqliteDatabase`/`SqliteStatement`: RAII wrapper over the C
  API with prepared statements, blob binding and a `transaction()` helper
  (BEGIN IMMEDIATE → COMMIT/ROLLBACK, rolls back + rethrows on exception).
  `EventStore`: schema (sessions/contexts/clipboard_entries/events/snapshots/
  settings/meta) with the hybrid typed-columns + serialized-payload-blob event
  layout, WAL + `synchronous=NORMAL` + busy-timeout pragmas, typed
  insert/upsert/read for every record, settings round-trip, and `max_*_id()`
  queries for allocator seeding across runs. `StorageWorker`: async writer on a
  dedicated thread that batches all staged records into one transaction
  (I/O off the capture path), with a deterministic `flush()`; validated race-
  free under ThreadSanitizer. `storage_paths` (Win32) resolves
  `%LOCALAPPDATA%\InputRecorder\`. 22 storage tests run for real on the POSIX
  harness (SQLite compiles there); 116 OS-independent tests total.
- [x] **Phase 9** — History (search, snapshots, retention). `SnapshotPolicy`
  (OS-free): a snapshot is due after N recorded events or T ms, whichever first.
  `HistoryService` over `EventStore`: paginated + filtered event/clipboard/
  session listings; FTS5 full-text search over recorded text and clipboard
  content with a safe MATCH builder (arbitrary user input can't cause a query
  error); point-in-time reconstruction — full replay and a snapshot-accelerated
  path proven equal to it; retention — age-based `purge_before`/`apply_retention`
  (days<=0 keeps forever), `clear_all`, `database_size_bytes`, and a storage
  size cap that trims oldest events and reclaims pages (incremental auto-vacuum).
  Extended `TextSnapshot` with caret state so snapshot restore is exact. 17 new
  tests (POSIX harness); 133 OS-independent tests total.
- [x] **Phase 10** — Main UI (Win32). OS-free presentation logic (tested):
  `AppViewModel` (recording status + reconstructed text/confidence, the
  ● RECORDING/○ PAUSED indicator, an empty-state placeholder, a confidence note
  shown only below High, and the verbatim Copy-All payload) and
  `history_formatting` (single-line, code-point-safe, control-char-sanitised row
  formatters for events/clipboard/sessions with an ellipsis on overflow).
  Win32 shells (compiled-by-inspection): `MainWindow` — indicator, read-only
  multi-line text view, confidence note, Pause/Resume + Copy All (self-copy
  announced to the clipboard monitor) + History buttons, dark/light palette via
  DWM + control colours; `HistoryWindow` — a search box with live full-text
  search over a results list, fed by a data-source callback the app wires to
  HistoryService. 13 new tests (POSIX harness); 146 OS-independent tests total.
  (Full window↔pipeline wiring lands with the app coordinator in Phase 13.)
- [ ] **Phase 11** — Settings & themes.
- [ ] **Phase 12** — System tray & global hotkey.
- [ ] **Phase 13** — Recovery/reliability.
- [ ] **Phase 14** — Security hardening.
- [ ] **Phase 15** — Performance optimization.
- [ ] **Phase 16** — Full QA.
- [ ] **Phase 17** — Release.
