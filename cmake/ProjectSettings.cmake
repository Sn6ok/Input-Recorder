# Shared helper functions for target configuration.
#
# ir_set_target_warnings(<target>) : apply the project warning level.
# ir_set_windows_defs(<target>)    : apply the standard Windows preprocessor defs.

# Optionally treat warnings as errors. Off by default so incremental development
# is not blocked; CI / release builds can enable it with -DIR_WARNINGS_AS_ERRORS=ON.
option(IR_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

function(ir_set_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4              # high warning level
            /permissive-     # strict standard conformance
            /utf-8           # source and execution charset are UTF-8
            /Zc:__cplusplus  # report the real __cplusplus value
            /EHsc            # standard C++ exception handling
            /MP              # parallel compilation
        )
        if(IR_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()
endfunction()

function(ir_set_windows_defs target)
    target_compile_definitions(${target} PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        # Target Windows 10/11 APIs.
        _WIN32_WINNT=0x0A00
    )
endfunction()
