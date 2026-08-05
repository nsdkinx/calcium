# Shared warning configuration.
#
# Calcium builds warning-clean. A framework that ships with warnings trains its
# developers to ignore them, and the specific warnings disabled below are
# documented rather than silently suppressed.

function(calcium_apply_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-        # standard-conforming preprocessor and lookup
            /Zc:__cplusplus     # report the real __cplusplus value
            /Zc:preprocessor    # conforming preprocessor
            /Zc:inline          # drop unreferenced internal-linkage functions
            /utf-8
            /EHsc
            /volatile:iso
            /bigobj
            # Deliberate suppressions:
            /wd4324             # structure padded due to alignas — intended in SoA storage
            /wd4251             # DLL-interface warning on std types — Calcium's ABI is the C API (P13)
        )
        if(CALCIUM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wnon-virtual-dtor
            -Wold-style-cast
            -Wcast-align
            -Wunused
            -Woverloaded-virtual
            -Wconversion
            -Wsign-conversion
            -Wdouble-promotion
            -Wformat=2
            -Wimplicit-fallthrough
        )
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_compile_options(${target} PRIVATE
                -Wmisleading-indentation
                -Wduplicated-cond
                -Wduplicated-branches
                -Wlogical-op
                -Wuseless-cast
            )
        endif()
        if(CALCIUM_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Applies the standard definitions every Calcium module needs.
function(calcium_configure_module target)
    calcium_apply_warnings(${target})

    # The modules are compiled into the umbrella shared library, so their
    # CALCIUM_API-annotated classes must emit dllexport. Harmless when a module
    # is linked into an executable directly (tests): the export table is
    # simply unused.
    target_compile_definitions(${target} PRIVATE CALCIUM_BUILDING_SHARED=1)

    target_include_directories(${target} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>"
        "$<INSTALL_INTERFACE:include>"
    )
    target_include_directories(${target} PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
    )

    if(CALCIUM_ENABLE_THREAD_ASSERTIONS)
        target_compile_definitions(${target} PUBLIC
            $<$<CONFIG:Debug>:CALCIUM_THREAD_ASSERTIONS_ENABLED=1>)
    endif()
    if(CALCIUM_ENABLE_ALLOCATION_SENTINEL)
        target_compile_definitions(${target} PUBLIC
            $<$<CONFIG:Debug>:CALCIUM_ALLOCATION_SENTINEL_ENABLED=1>)
    endif()
endfunction()
