#!/usr/bin/env bash
#
# Build and run the hot-path performance benchmark on a POSIX host (clang/g++).
# Reuses the cached SQLite object from build-posix/ when present.
#
# Usage: scripts/bench-posix.sh [iterations]
#
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$root"

CXX="${CXX:-clang++}"
CC="${CC:-clang}"
outdir="${OUT_DIR:-$root/build-posix}"
mkdir -p "$outdir"

sqlite_defs=(-DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0
             -DSQLITE_DEFAULT_MEMSTATUS=0 -DSQLITE_ENABLE_FTS5)
sqlite_obj="$outdir/sqlite3.o"
if [[ ! -f "$sqlite_obj" || third_party/sqlite/sqlite3.c -nt "$sqlite_obj" ]]; then
  "$CC" -std=c11 -O2 "${sqlite_defs[@]}" -c third_party/sqlite/sqlite3.c -o "$sqlite_obj"
fi

"$CXX" -std=c++20 -O2 -pthread -Isrc -Ithird_party/sqlite "${sqlite_defs[@]}" \
  bench/perf_bench.cpp \
  src/core/serialization.cpp src/core/event_queue.cpp \
  src/storage/sqlite_database.cpp src/storage/event_store.cpp src/storage/storage_worker.cpp \
  src/reconstruction/reconstruction_engine.cpp src/utils/unicode.cpp \
  "$sqlite_obj" -ldl -lm -o "$outdir/perf_bench"

exec "$outdir/perf_bench" "$@"
