# --------------------------------------------------------------------------
# ir_tests : single test runner linking the in-repo framework + the library.
# All test source files are added here; each registers its cases via TEST_CASE.
# Registered with CTest as one test (the runner reports per-case results and
# returns non-zero if any case fails).
# --------------------------------------------------------------------------
add_executable(ir_tests
    framework/test_main.cpp
    unit/version_test.cpp
    unit/ids_test.cpp
    unit/time_test.cpp
    unit/configuration_test.cpp
    unit/serialization_test.cpp
    unit/model_test.cpp
    unit/event_queue_test.cpp
    capture/keyboard_translator_test.cpp
    capture/keyboard_hook_test.cpp
    unit/unicode_test.cpp
    reconstruction/reconstruction_test.cpp
    clipboard/clipboard_processor_test.cpp
    clipboard/clipboard_monitor_test.cpp
    mouse/mouse_processor_test.cpp
    capture/mouse_hook_test.cpp
    context/context_tracker_test.cpp
    context/window_context_monitor_test.cpp
    storage/sqlite_database_test.cpp
    storage/event_store_test.cpp
    storage/storage_worker_test.cpp
    history/history_service_test.cpp
    history/snapshot_policy_test.cpp
    ui/app_view_model_test.cpp
    ui/history_formatting_test.cpp
    ui/theme_test.cpp
    ui/tray_menu_model_test.cpp
    settings/settings_service_test.cpp
    system/hotkey_test.cpp
)

target_include_directories(ir_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ir_tests PRIVATE input_recorder_lib)
target_compile_features(ir_tests PRIVATE cxx_std_20)
ir_set_target_warnings(ir_tests)
ir_set_windows_defs(ir_tests)

add_test(NAME ir_tests COMMAND ir_tests)
