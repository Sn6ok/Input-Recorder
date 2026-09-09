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

Under active, phased development. Current milestone: **Phase 0 — project
bootstrap** (build system, module skeleton, test harness). See
[`docs/architecture.md`](docs/architecture.md) for the roadmap and design.

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
(`%LOCALAPPDATA%\InputRecorder\`). Nothing is ever transmitted off the device.
(Storage is implemented in Phase 8.)

## Hotkeys, recording and settings

Global hotkey, recording controls, retention and appearance settings are
implemented in later phases and documented here as they land.

## License

MIT — see [`LICENSE`](LICENSE).
