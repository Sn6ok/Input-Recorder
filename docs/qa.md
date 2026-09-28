# QA

This document is the quality-assurance matrix for Input Recorder: what is
verified automatically, how, and what must be checked manually on Windows.

## Automated coverage (portable core)

The OS-independent core — the bulk of the application logic — is unit- and
integration-tested and runs in CI on any POSIX host via
`scripts/build-posix-tests.sh` (clang/g++, `-Wall -Wextra -Wpedantic -Werror`,
the analogue of the authoritative MSVC `/W4 /WX`). It also compiles the real,
vendored SQLite amalgamation, so the entire storage/history/retention stack is
exercised for real, not mocked.

Suites (per module):

| Area | Suites |
|------|--------|
| Core model / serialization / IDs / time / config | `unit/*` |
| Event queue (bounded, thread-safe) | `unit.event_queue` |
| Keyboard translation | `capture.keyboard` |
| Reconstruction engine (+ golden scenarios) | `reconstruction` |
| Clipboard processing | `clipboard.proc` |
| Mouse processing (buttons/wheel/movement throttle) | `mouse.proc` |
| Window/process context (cache + dedup) | `context.tracker` |
| SQLite wrapper / store / async writer | `storage.*` |
| History: pagination, FTS search, snapshots, retention | `history.*` |
| Settings service + theme | `settings.service`, `ui.theme` |
| Hotkey mapping + tray menu model | `system.hotkey`, `ui.tray` |
| Reliability: emergency buffer, recovery, coordinator | `reliability.*` |
| Security: no-leak diagnostics, FTS-injection safety | `security` |
| UI view-model + history formatting | `ui.viewmodel`, `ui.format` |
| **End-to-end pipeline** | `integration` |

Run it:

```bash
scripts/build-posix-tests.sh            # all suites
scripts/build-posix-tests.sh --suite=integration
```

## Sanitizers

- **AddressSanitizer + UndefinedBehaviorSanitizer** (memory safety, UB): the
  **entire** portable suite (174 tests) runs **clean** — no ASan reports, no UB —
  under:
  `OUT_DIR=/tmp/asan CXX=g++ CC=gcc EXTRA_CXXFLAGS="-fsanitize=address,undefined -fno-sanitize-recover=undefined" scripts/build-posix-tests.sh`
- **ThreadSanitizer** (data races): the concurrent components — the async
  `StorageWorker` and the `RecordingCoordinator` consumer thread — run **clean**
  under TSan (g++ `-fsanitize=thread`). The whole suite can also be built under
  TSan via `EXTRA_CXXFLAGS="-fsanitize=thread" EXTRA_CFLAGS="-fsanitize=thread"`.

These are real runs on the build host, not estimates.

## Security scan

`scripts/security-scan.sh` fails the build if first-party code references any
network / input-injection / process-injection / privilege-escalation /
external-code-loading API. Runs **clean**.

## Performance

Measured hot-path throughput and footprint are recorded in
[`performance.md`](performance.md). All stages have a large margin over real
input rates.

## Not covered by POSIX CI — manual Windows verification required

The Win32 layer (global hooks, clipboard/window monitors, tray, hotkey, the
window/dialogs, the app assembly) cannot run off Windows and is compiled by
inspection here. It must be verified on Windows 11 with the MSVC build. Checklist:

1. **Build gate.** `./scripts/build.ps1 -Test -WarningsAsErrors` — clean build at
   `/W4 /WX`, all tests green (77 → the full set including the Win32 lifecycle
   tests that only run on Windows).
2. **Launch & indicator.** App starts, main window shows ● RECORDING; tray icon
   present with the state tooltip.
3. **Typed-text recovery.** Type into Notepad/an editor across a few app
   switches; the main window shows the reconstructed text; Copy All copies it.
4. **Editing.** Backspace/Delete/arrows/Home/End/selection/Ctrl+Z reflected in
   the reconstruction with an appropriate confidence note.
5. **Clipboard.** Copy/paste text; it appears in History; pasted text is
   reconstructed. A password manager's copy (exclusion marker) is NOT recorded.
6. **Password fields.** Typing into a Windows password box (ES_PASSWORD) is NOT
   recorded (no keystrokes, no text).
7. **Mouse.** Clicks/wheel recorded per settings; movement off by default; when
   enabled it is throttled.
8. **Context.** Switching windows creates/deduplicates contexts; History shows
   the process/title.
9. **Pause/Resume.** Via button, tray menu, and the global hotkey
   (Ctrl+Shift+R); indicator + tooltip update; no events recorded while paused.
10. **Settings.** Toggles, theme (System/Light/Dark follows the OS), retention;
    changes persist across restart.
11. **History window.** Live full-text search returns matches; pagination works.
12. **Retention / size cap.** Old data purged by age; size cap trims oldest;
    "clear all" empties history.
13. **Tray behavior.** Minimize/close to tray per settings; double-click shows/
    hides; Exit shuts down cleanly.
14. **Recovery.** Kill the process (Task Manager) mid-use; on relaunch the prior
    session is marked Interrupted and any buffered events are recovered; the DB
    (WAL) is intact.
15. **Single instance.** A second launch does not start a duplicate.
16. **Resource use.** Idle and while typing, confirm low CPU and a modest working
    set (Task Manager), matching the design in `performance.md`.
17. **Locality.** Confirm (e.g. with a firewall / Resource Monitor) the process
    makes **no** network connections; data lives only under
    `%LOCALAPPDATA%\InputRecorder\`.
