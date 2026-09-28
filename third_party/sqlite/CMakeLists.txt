# Vendored SQLite amalgamation (see README.md for version/provenance). Built as
# an isolated static library so its many benign third-party warnings never reach
# the project's /W4 /WX gate, and so its compile definitions are applied
# consistently to every consumer.
add_library(sqlite3 STATIC sqlite3.c)

target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(sqlite3 PUBLIC
    SQLITE_THREADSAFE=1            # serialized: safe from multiple threads
    SQLITE_OMIT_LOAD_EXTENSION     # never load external code (security, spec §14)
    SQLITE_DQS=0                   # reject double-quoted string literals
    SQLITE_DEFAULT_MEMSTATUS=0     # small perf win; memory stats unused
    SQLITE_ENABLE_FTS5            # full-text search for history (Phase 9)
)

# Third-party code: do not apply the project warning level or /WX.
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /w)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()

# POSIX builds need libdl/libm for a few SQLite entry points.
if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(sqlite3 PUBLIC ${CMAKE_DL_LIBS} Threads::Threads)
endif()
