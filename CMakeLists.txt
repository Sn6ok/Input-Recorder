cmake_minimum_required(VERSION 3.20)

project(InputRecorder
    VERSION 0.1.0
    DESCRIPTION "Personal local input recovery utility for Windows 11"
    LANGUAGES CXX C)  # C is required only to compile the vendored SQLite amalgamation

# --------------------------------------------------------------------------
# Global settings
# --------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Out-of-source builds only.
if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Configure into a separate build/ directory.")
endif()

# This project targets Windows 11 x64 with MSVC (see spec §22-§26).
if(NOT WIN32)
    message(FATAL_ERROR "Input Recorder targets Windows only.")
endif()
if(NOT MSVC)
    message(WARNING "Input Recorder is designed for MSVC. Other compilers are untested.")
endif()

# Release is the primary user-facing configuration (spec §25). For single-config
# generators default to Release; multi-config generators (Visual Studio) expose
# Debug + Release and are selected at build time with --config.
get_property(_ir_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _ir_multi_config AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo)
endif()

# Put all binaries in one place for easy launching.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ProjectSettings)

enable_testing()

add_subdirectory(third_party/sqlite)
add_subdirectory(src)
add_subdirectory(tests)

message(STATUS "Input Recorder ${PROJECT_VERSION} configured (generator: ${CMAKE_GENERATOR}).")
