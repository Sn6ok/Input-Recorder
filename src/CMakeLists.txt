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
    keyboard/keyboard_text.cpp
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
    ui/app_view_model.cpp
    ui/history_formatting.cpp
    ui/theme.cpp
    ui/os_theme.cpp
    ui/main_window.cpp
    ui/history_window.cpp
    ui/settings_window.cpp
    ui/tray_icon.cpp
    settings/settings_service.cpp
    system/hotkey.cpp
    system/global_hotkey.cpp
    reliability/emergency_buffer.cpp
    reliability/recovery.cpp
    reliability/recording_coordinator.cpp
    security/sensitive_input.cpp
    app/application.cpp
)

target_include_directories(input_recorder_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(input_recorder_lib PUBLIC cxx_std_20)
# Windows user-level input APIs (SetWindowsHookEx, message loop, etc.),
# Known Folder path resolution (shell32/ole32), GDI + DWM for the themed window,
# and the vendored SQLite lib.
target_link_libraries(input_recorder_lib PUBLIC
    user32 shell32 ole32 gdi32 dwmapi uxtheme advapi32 comctl32 sqlite3)
ir_set_target_warnings(input_recorder_lib)
ir_set_windows_defs(input_recorder_lib)

# --------------------------------------------------------------------------
# InputRecorder : the application executable. A Win32 GUI app (no console):
# WIN32_EXECUTABLE selects /SUBSYSTEM:WINDOWS and the wWinMain entry point.
# --------------------------------------------------------------------------
add_executable(InputRecorder main.cpp)
set_target_properties(InputRecorder PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_libraries(InputRecorder PRIVATE input_recorder_lib)
ir_set_target_warnings(InputRecorder)
ir_set_windows_defs(InputRecorder)

# Version resource + application manifest (asInvoker, DPI, UTF-8, ComCtl v6).
if(MSVC)
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.rc.in
                   ${CMAKE_CURRENT_BINARY_DIR}/version.rc @ONLY)
    target_sources(InputRecorder PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}/version.rc
        ${CMAKE_CURRENT_SOURCE_DIR}/app.manifest)
endif()
