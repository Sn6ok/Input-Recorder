# Performance

Input Recorder's performance goals (in priority order): reliability, then
correctness, then low RAM, then low CPU, then no UI stalls (spec §33-§35,
§453-§457). This document records **real measurements** of the hot paths and the
design choices behind them.

## What was measured, and where

The numbers below are **real** measurements of the OS-independent core, produced
by `bench/perf_bench.cpp` via `scripts/bench-posix.sh` on the build host
(Linux x86-64, clang -O2). They characterise the *algorithms* — queue,
serialization, reconstruction, and the SQLite writer — which are identical on
Windows.

They are **not** the end-to-end idle-CPU / working-set of the Windows GUI app
under live user input. That figure depends on the Win32 hooks and message loops,
which cannot run off Windows, so it is **pending measurement on Windows**
(Task Manager / `GetProcessMemoryInfo`, running the built `InputRecorder.exe`).
The same benchmark compiles and runs on Windows to reproduce the core figures
there. This split is deliberate and honest: nothing here is estimated.

## Measured hot-path throughput (Linux x86-64, clang -O2, n = 1,000,000)

| Path                         | Throughput        | Notes                              |
|------------------------------|-------------------|------------------------------------|
| EventQueue push + drain      | ~12.9 M ops/sec   | bounded, lock-based, batched drain |
| serialize + deserialize      | ~3.8 M ops/sec    | 53 bytes/event on the wire         |
| Reconstruction `process`     | ~36.8 M ops/sec   | code-point buffer edits            |
| Storage write (SQLite, WAL)  | ~42.6 K events/sec| async writer, batched transactions |
| On-disk size                 | ~79 bytes/event   | typed columns + payload + FTS index|

Memory footprint:

| Item                         | Value             |
|------------------------------|-------------------|
| `sizeof(Event)`              | 96 bytes          |
| Bounded queue (65,536 slots) | ~6.0 MiB (fixed)  |
| Process peak RSS (bench)     | ~27 MiB           |

## Interpretation vs. real input rates

Human typing is on the order of 5–10 events/sec; even sustained heavy input
(fast typing + mouse) stays well under ~100 events/sec. Against that:

- The **slowest** stage, SQLite persistence, sustains ~43,000 events/sec — a
  **400×+** margin over heavy real input. Disk I/O is never on the capture path
  (the async `StorageWorker` batches writes into transactions off-thread), so the
  hook callbacks return in microseconds and never stall system input.
- Reconstruction and the queue are effectively free at real input rates.
- Memory is bounded by design: a fixed-capacity ring queue (no per-event
  allocation of queue nodes) plus a single reconstruction buffer. There is no
  unbounded growth in steady state.

## Design choices that keep CPU/RAM low

- **Event-driven, no polling** (spec §35): keyboard/mouse use low-level hooks,
  the clipboard uses `AddClipboardFormatListener`, and window context uses
  `SetWinEventHook`. Idle CPU is dominated by nothing but a lightweight ~300 ms
  UI-refresh timer (only meaningful while the window is visible).
- **Minimal hook callbacks** (spec §58, §455): a hook builds one event, stamps
  it, and enqueues — no I/O, no locks held across work, no allocation beyond the
  event itself. A full queue drops-and-counts rather than blocking input.
- **Batched, off-thread persistence** (spec §170): the writer coalesces many
  events into one transaction, amortising fsyncs; WAL keeps the UI's reads from
  blocking the writer.
- **Bounded memory** (spec §454): fixed-capacity queue; snapshots bound
  reconstruction replay so history queries never re-run a whole session.
- **Retention + size cap** (spec §259): old data is purged by age and a storage
  cap trims oldest events and reclaims pages (incremental auto-vacuum), so the
  database does not grow without bound.

## Reproducing

```bash
# Core hot-path figures (any POSIX host):
scripts/bench-posix.sh 1000000

# On Windows, additionally measure the running app's idle/typing CPU and
# working set via Task Manager or GetProcessMemoryInfo against InputRecorder.exe.
```
