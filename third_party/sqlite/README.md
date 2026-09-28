# SQLite amalgamation (vendored)

Input Recorder embeds SQLite as a single-translation-unit amalgamation so the
storage layer has **no external dependency** and the build works fully offline
(spec §24, §358, and the Phase 8 storage requirement).

| Field           | Value                                                                 |
|-----------------|-----------------------------------------------------------------------|
| Version         | **3.53.4**                                                            |
| Source ID       | `2026-07-24 19:02:57 bf7c7f30031888f4e796e429ab3978879485813aaca6f641c7b33e4e09459bcc` |
| Files           | `sqlite3.c`, `sqlite3.h`                                              |
| License         | Public domain (SQLite is dedicated to the public domain by its authors) |

The two files are the **unmodified** official amalgamation, as distributed in
`sqlite-amalgamation-3530400.zip`. They were obtained via the widely-used
[`SRombauts/SQLiteCpp`](https://github.com/SRombauts/SQLiteCpp) project's
vendored copy (the canonical `sqlite.org` download host was not reachable from
the build environment). Authenticity was verified against the amalgamation
banner and the official `SQLITE_SOURCE_ID`.

SHA-256:

```
b1dd5d74ec7f29055a6684fa06fb3c2f6821c87dd38f9a458dfd2e8a1db28189  sqlite3.c
919e7f2e8ed1d8f56ac17b412b8971c76aa5d1a879752cc6058f75e7d5910e1d  sqlite3.h
```

## Build configuration

The amalgamation is compiled as part of `input_recorder_lib` with these compile
definitions (see `src/CMakeLists.txt` and `scripts/build-posix-tests.sh`):

- `SQLITE_THREADSAFE=1` — serialized threading (the async writer owns one
  connection; a reader connection is used from the UI thread).
- `SQLITE_OMIT_LOAD_EXTENSION` — never load external code (security, spec §14).
- `SQLITE_DQS=0` — no double-quoted string literals (stricter, safer SQL).
- `SQLITE_DEFAULT_MEMSTATUS=0` — small perf win; memory stats unused.
- `SQLITE_ENABLE_FTS5` — full-text search for history (Phase 9).

To refresh: replace both files with a newer official amalgamation and update the
version, source id and hashes above.
