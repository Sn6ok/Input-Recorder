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
)

target_include_directories(ir_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ir_tests PRIVATE input_recorder_lib)
target_compile_features(ir_tests PRIVATE cxx_std_20)
ir_set_target_warnings(ir_tests)
ir_set_windows_defs(ir_tests)

add_test(NAME ir_tests COMMAND ir_tests)
