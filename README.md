# Input Recorder

A lightweight, **local-only** input-recovery utility for Windows 11.

Input Recorder continuously records your keyboard, text, clipboard and mouse
input **on your own machine** and reconstructs it into a readable, copyable
history. Its purpose is simple: if you accidentally lose text you were writing
(a long prompt, a message, a note), you can open Input Recorder, find it in the
local history, and copy it back.

> **What this is not.** Input Recorder is a personal productivity/recovery tool.
> It is **not** a surveillance or monitoring tool. It has **no network code**,
> no cloud sync, no telemetry, no stealth mode, and it does not attempt to
> capture credentials or bypass Windows security boundaries. The recording
> state is always visible and fully under your control (pause / resume / delete).

## Status

Feature-complete (Phases 0–17). Keyboard/mouse/clipboard capture, window-context
tracking, text reconstruction with a confidence model, local SQLite storage with
an async writer and WAL, history with full-text search, snapshots and retention,
a Win32 UI (recording indicator, read-only text, Copy All, History), settings and
dark/light themes, a system tray with a global toggle hotkey, crash recovery and
an emergency buffer, and security hardening. See
[`docs/architecture.md`](docs/architecture.md) for the design and the per-phase
notes, [`docs/qa.md`](docs/qa.md) for the QA matrix,
[`docs/performance.md`](docs/performance.md) for measured performance, and
[`SECURITY.md`](SECURITY.md) for the security posture.

**Testing note.** The OS-independent core (the bulk of the logic, including the
whole SQLite storage/history stack) is covered by 174 unit + integration tests
that run on any POSIX host via `scripts/build-posix-tests.sh` and are clean under
AddressSanitizer/UBSan and ThreadSanitizer. The Win32 layer (hooks, monitors,
tray, windows, app assembly) is built and gated by the authoritative MSVC
`/W4 /WX` build on Windows; see the manual checklist in `docs/qa.md`.

## Building

Requirements:

- Windows 11 x64
- Visual Studio 2022 **or** the VS 2022 Build Tools (MSVC v14.4x, Windows 11 SDK)
- CMake ≥ 3.20 (bundled with the Build Tools)

The project uses CMake with the Visual Studio 2022 generator. The easiest way to
build is the helper script:

```powershell
# From the repository root
./scripts/build.ps1              # configure + build Release
./scripts/build.ps1 -Test        # build + run the unit tests
./scripts/build.ps1 -Config Debug
```

Or drive CMake directly:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is produced at `build/bin/Release/InputRecorder.exe`.
**Release** is the primary configuration.

## Where data is stored

All recorded data stays on the local machine under your user profile
(`%LOCALAPPDATA%\InputRecorder\`), in a local SQLite database. Nothing is ever
transmitted off the device.

## Hotkeys, recording and settings

- **Global toggle hotkey:** `Ctrl+Shift+R` pauses/resumes recording (configurable).
- **Recording controls:** pause/resume from the window, the tray menu or the
  hotkey; the state is always visible (● RECORDING / ○ PAUSED).
- **Settings:** recording toggles (keyboard, mouse clicks/wheel/movement, active
  window, clipboard), theme (System/Light/Dark), retention (days + storage cap),
  startup and tray behavior.
- **History:** browse and full-text-search everything recorded; Copy All copies
  the current reconstructed text.

## License

MIT — see [`LICENSE`](LICENSE).
