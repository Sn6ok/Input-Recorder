# --------------------------------------------------------------------------
# input_recorder_lib : all application logic that is independent of the Win32
# message loop / UI. Both the executable and the test runner link against it,
# so the reconstruction engine, storage, etc. can be unit tested without
# launching a window (spec §31, §95, §623).
# --------------------------------------------------------------------------
add_library(input_recorder_lib STATIC
    core/version.cpp
    core/time.cpp
    core/configuration.cpp
    core/serialization.cpp
    core/event_queue.cpp
    keyboard/keyboard_translator.cpp
    capture/keyboard_hook.cpp
    utils/unicode.cpp
    reconstruction/reconstruction_engine.cpp
    clipboard/clipboard_processor.cpp
    clipboard/clipboard_monitor.cpp
    mouse/mouse_processor.cpp
    capture/mouse_hook.cpp
    context/context_tracker.cpp
    context/window_context_monitor.cpp
    storage/sqlite_database.cpp
    storage/event_store.cpp
    storage/storage_worker.cpp
    storage/storage_paths.cpp
    history/history_service.cpp
    history/snapshot_policy.cpp
)

target_include_directories(input_recorder_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(input_recorder_lib PUBLIC cxx_std_20)
# Windows user-level input APIs (SetWindowsHookEx, message loop, etc.),
# Known Folder path resolution (shell32/ole32), and the vendored SQLite lib.
target_link_libraries(input_recorder_lib PUBLIC user32 shell32 ole32 sqlite3)
ir_set_target_warnings(input_recorder_lib)
ir_set_windows_defs(input_recorder_lib)

# --------------------------------------------------------------------------
# InputRecorder : the application executable.
# Console subsystem during bootstrap; switches to a Win32 GUI in Phase 10.
# --------------------------------------------------------------------------
add_executable(InputRecorder main.cpp)
target_link_libraries(InputRecorder PRIVATE input_recorder_lib)
ir_set_target_warnings(InputRecorder)
ir_set_windows_defs(InputRecorder)
