#!/usr/bin/env bash
#
# Auxiliary CI harness: build and run the OS-INDEPENDENT subset of the unit
# tests on a POSIX host (Linux/macOS) with clang or g++.
#
# WHY THIS EXISTS
# ---------------
# Input Recorder targets Windows 11 and its authoritative build is
# `scripts/build.ps1 -Test -WarningsAsErrors` (MSVC, /W4 /WX). The Win32 pieces
# (keyboard/mouse hooks, clipboard, window context, UI, tray) cannot be compiled
# off Windows. However, the bulk of the application logic lives in
# `input_recorder_lib` and is deliberately OS-free so it can be unit tested
# without a message loop. This script compiles exactly that portable subset plus
# the portable SQLite amalgamation (when present) at a high warning level with
# warnings-as-errors, giving a real green/red signal on non-Windows CI.
#
# It is a SUPPLEMENT, not a replacement: the Windows MSVC build remains the gate
# for the Win32 code. Files that include <Windows.h> are intentionally excluded
# here; see the EXCLUDE list below and docs/architecture.md for what each
# platform verifies.
#
# Usage:
#   scripts/build-posix-tests.sh            # build + run
#   CXX=g++ scripts/build-posix-tests.sh    # pick a compiler
#
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

CXX="${CXX:-clang++}"
outdir="${OUT_DIR:-$root/build-posix}"
mkdir -p "$outdir"
bin="$outdir/ir_tests_posix"

# High warning level, warnings-as-errors — the POSIX analogue of MSVC /W4 /WX.
# (-Wconversion/-Wshadow are intentionally omitted: they flag clang-only
# pedantry that MSVC /W4 does not, which would fail code that is clean on the
# authoritative Windows build.)
warnflags=(-Wall -Wextra -Wpedantic -Werror
           # clang/gcc-only diagnostics with no MSVC /W4 equivalent:
           -Wno-unused-const-variable)
stdflags=(-std=c++20 -O2 -pthread -Isrc -Itests)

# OS-independent library translation units (everything in input_recorder_lib
# that does NOT include <Windows.h>). Keep in sync with src/CMakeLists.txt.
lib_srcs=(
  src/core/version.cpp
  src/core/time.cpp
  src/core/configuration.cpp
  src/core/serialization.cpp
  src/core/event_queue.cpp
  src/keyboard/keyboard_translator.cpp
  src/utils/unicode.cpp
  src/reconstruction/reconstruction_engine.cpp
  src/clipboard/clipboard_processor.cpp
  src/mouse/mouse_processor.cpp
  src/context/context_tracker.cpp
  src/storage/sqlite_database.cpp
  src/storage/event_store.cpp
  src/storage/storage_worker.cpp
  src/history/history_service.cpp
  src/history/snapshot_policy.cpp
  src/settings/settings_service.cpp
  src/system/hotkey.cpp
  src/reliability/emergency_buffer.cpp
  src/reliability/recovery.cpp
  src/reliability/recording_coordinator.cpp
  src/ui/app_view_model.cpp
  src/ui/history_formatting.cpp
  src/ui/theme.cpp
)

# OS-independent test translation units. Excludes the Win32 lifecycle tests
# (capture/keyboard_hook_test.cpp, clipboard/clipboard_monitor_test.cpp,
# capture/mouse_hook_test.cpp) which drive real Windows objects.
test_srcs=(
  tests/framework/test_main.cpp
  tests/unit/version_test.cpp
  tests/unit/ids_test.cpp
  tests/unit/time_test.cpp
  tests/unit/configuration_test.cpp
  tests/unit/serialization_test.cpp
  tests/unit/model_test.cpp
  tests/unit/event_queue_test.cpp
  tests/capture/keyboard_translator_test.cpp
  tests/unit/unicode_test.cpp
  tests/reconstruction/reconstruction_test.cpp
  tests/clipboard/clipboard_processor_test.cpp
  tests/mouse/mouse_processor_test.cpp
  tests/context/context_tracker_test.cpp
  tests/storage/sqlite_database_test.cpp
  tests/storage/event_store_test.cpp
  tests/storage/storage_worker_test.cpp
  tests/history/history_service_test.cpp
  tests/history/snapshot_policy_test.cpp
  tests/settings/settings_service_test.cpp
  tests/system/hotkey_test.cpp
  tests/reliability/emergency_buffer_test.cpp
  tests/reliability/recovery_test.cpp
  tests/reliability/recording_coordinator_test.cpp
  tests/security/security_test.cpp
  tests/ui/app_view_model_test.cpp
  tests/ui/history_formatting_test.cpp
  tests/ui/theme_test.cpp
  tests/ui/tray_menu_model_test.cpp
)

# Portable SQLite amalgamation, compiled as C when vendored (added in Phase 8).
sqlite_c="third_party/sqlite/sqlite3.c"
sqlite_obj=""
extra_incs=()
sqlite_defs=(
  -DSQLITE_THREADSAFE=1
  -DSQLITE_OMIT_LOAD_EXTENSION
  -DSQLITE_DQS=0
  -DSQLITE_DEFAULT_MEMSTATUS=0
  -DSQLITE_ENABLE_FTS5
)
if [[ -f "$sqlite_c" ]]; then
  extra_incs+=("-Ithird_party/sqlite")
  sqlite_obj="$outdir/sqlite3.o"
  cc="${CC:-clang}"
  # The amalgamation is large and never changes between runs; only rebuild the
  # object when it is missing or the source is newer (big iteration speed-up).
  if [[ ! -f "$sqlite_obj" || "$sqlite_c" -nt "$sqlite_obj" ]]; then
    echo ">> compiling SQLite amalgamation ($cc)"
    "$cc" -std=c11 -O2 "${sqlite_defs[@]}" -c "$sqlite_c" -o "$sqlite_obj"
  else
    echo ">> using cached SQLite object ($sqlite_obj)"
  fi
fi

# Only compile sources that exist yet (the file lists span all phases).
present=()
for f in "${lib_srcs[@]}" "${test_srcs[@]}"; do
  [[ -f "$f" ]] && present+=("$f")
done

link_libs=()
[[ -n "$sqlite_obj" ]] && link_libs+=("$sqlite_obj")
# SQLite needs libdl/libm on some POSIX hosts.
[[ -n "$sqlite_obj" ]] && link_libs+=(-ldl -lm)

echo ">> compiling ${#present[@]} translation units ($CXX)"
"$CXX" "${stdflags[@]}" "${extra_incs[@]}" "${sqlite_defs[@]}" "${warnflags[@]}" \
  "${present[@]}" "${link_libs[@]}" -o "$bin"

echo ">> running $bin"
"$bin" "$@"
