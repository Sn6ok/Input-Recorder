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
- [ ] **Phase 1** — Core data model (Event, EventType, Timestamp, Session,
  Context, ClipboardEntry, TextSnapshot, Configuration) + serialization tests.
- [ ] **Phase 2** — Event pipeline (thread-safe bounded queue, producer/consumer).
- [ ] **Phase 3** — Keyboard capture (`WH_KEYBOARD_LL`, minimal callback).
- [ ] **Phase 4** — Reconstruction engine + golden tests.
- [ ] **Phase 5** — Clipboard subsystem.
- [ ] **Phase 6** — Mouse subsystem.
- [ ] **Phase 7** — Window/process context.
- [ ] **Phase 8** — Storage (SQLite).
- [ ] **Phase 9** — History (search, snapshots, retention).
- [ ] **Phase 10** — Main UI (Win32).
- [ ] **Phase 11** — Settings & themes.
- [ ] **Phase 12** — System tray & global hotkey.
- [ ] **Phase 13** — Recovery/reliability.
- [ ] **Phase 14** — Security hardening.
- [ ] **Phase 15** — Performance optimization.
- [ ] **Phase 16** — Full QA.
- [ ] **Phase 17** — Release.
