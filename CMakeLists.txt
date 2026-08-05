cmake_minimum_required(VERSION 3.24)

project(Calcium
    VERSION 0.1.0
    DESCRIPTION "GPU-composited, physics-native application framework"
    LANGUAGES C CXX
)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Single output directory keeps DLL/EXE colocation simple on Windows.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(CALCIUM_BUILD_TESTS     "Build the Calcium test suite"    ON)
option(CALCIUM_BUILD_EXAMPLES  "Build the Calcium examples"      ON)
option(CALCIUM_BUILD_TOOLS     "Build Calcium developer tools"   OFF)
option(CALCIUM_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

# Backends. Off until their milestones (see docs/06-roadmap.md).
option(CALCIUM_RASTER_SKIA      "Skia rasterizer backend (M2)"     OFF)
option(CALCIUM_PLATFORM_SDL3    "SDL3 platform backend (M1)"       ON)
option(CALCIUM_GPU_D3D12        "D3D12 GPU backend (M1, Windows)"  ON)
option(CALCIUM_GPU_METAL        "Metal GPU backend (M1, macOS)"    OFF)
option(CALCIUM_GPU_VULKAN       "Vulkan GPU backend (M1)"          OFF)
option(CALCIUM_PLATFORM_NATIVE  "Native platform backends (M6)"    OFF)
option(CALCIUM_SHAPER_HARFBUZZ  "HarfBuzz shaping backend (M3)"    OFF)
option(CALCIUM_UNICODE_ICU      "ICU unicode backend (M3)"         OFF)

# Debug facilities. These are the enforcement mechanisms for P8 and the
# threading contract; both default to ON in Debug and OFF in Release.
option(CALCIUM_ENABLE_THREAD_ASSERTIONS
       "Assert thread affinity on public entry points" ON)
option(CALCIUM_ENABLE_ALLOCATION_SENTINEL
       "Hard-fail on heap allocation in the steady-state frame path" ON)

include(CalciumCompilerWarnings)

# ---------------------------------------------------------------------------
# third_party: Twell (the animation kernel — in-house, vendored)
#
# Twell is an INTERFACE target so exactly one translation unit defines
# TWELL_IMPL. It is linked PRIVATE into calcium_animation and never appears in
# a public header (enforced by tools/ci/check_header_hygiene.py).
# ---------------------------------------------------------------------------
add_library(twell INTERFACE)
target_include_directories(twell INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/twell")

# ---------------------------------------------------------------------------
# Module targets
#
# Link visibility carries the level DAG into the build system: a module lists
# only what it is permitted to depend on (docs/02-architecture.md §1).
# ---------------------------------------------------------------------------
add_subdirectory(src/core)
add_subdirectory(src/geometry)
add_subdirectory(src/platform)
add_subdirectory(src/gpu)
add_subdirectory(src/animation)
add_subdirectory(src/compositor)

# Backends link only into the umbrella (docs/02-architecture.md §6): the
# public API never names them, and the umbrella registers their factories.
add_subdirectory(src/backends)

# The umbrella shared library plus the C ABI. Modules are added here as their
# milestones land.
add_library(calcium SHARED src/calcium.cpp)
add_library(Calcium::calcium ALIAS calcium)

# The C++ API surface is the static modules (docs/04-public-api.md); the DLL
# exists for the C ABI (P13, M7) and contains the modules so that surface can
# be exported from one place when the ABI freezes.
target_link_libraries(calcium
    PRIVATE
        calcium_core
        calcium_geometry
        calcium_platform
        calcium_gpu
        calcium_animation
        calcium_backends
)
target_include_directories(calcium PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
)
# The umbrella includes src/ for the same reason the modules do: internal
# headers are never part of the public include path.
target_include_directories(calcium PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(calcium
    PRIVATE   CALCIUM_BUILDING_SHARED
    INTERFACE CALCIUM_USING_SHARED
)
calcium_apply_warnings(calcium)

# SDL3.dll ships next to the umbrella's DLL so applications can run from the
# build tree without extra PATH surgery.
if(CALCIUM_PLATFORM_SDL3)
    add_custom_command(TARGET calcium POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl3/lib/SDL3.dll"
                "$<TARGET_FILE_DIR:calcium>/SDL3.dll"
        COMMENT "Copying SDL3.dll next to calcium"
        VERBATIM
    )
endif()

set_target_properties(calcium PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME calcium
)

# ---------------------------------------------------------------------------
# Header hygiene gate (P5). Runs as part of the default build, not just CI —
# a violation should fail the developer's build, not a nightly job.
# ---------------------------------------------------------------------------
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_custom_target(calcium_header_hygiene ALL
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/ci/check_header_hygiene.py"
                --repo-root "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "Checking public header hygiene (P5)"
        VERBATIM
    )
    add_dependencies(calcium calcium_header_hygiene)
else()
    message(WARNING
        "Python3 not found: the header hygiene gate (P5) will NOT run. "
        "This is the mechanism that keeps third-party types out of the public "
        "API; please install Python 3.")
endif()

# ---------------------------------------------------------------------------
# Tests and examples
# ---------------------------------------------------------------------------
if(CALCIUM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(CALCIUM_BUILD_EXAMPLES)
    add_subdirectory(examples/01-hello-rectangle)
    add_subdirectory(examples/02-clear-color)
endif()

if(CALCIUM_BUILD_TOOLS)
    add_subdirectory(tools/calcium-tracer)
endif()

# ---------------------------------------------------------------------------
# Configuration summary
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "Calcium ${PROJECT_VERSION}")
message(STATUS "  Build type ......... ${CMAKE_BUILD_TYPE}")
message(STATUS "  Compiler ........... ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Tests .............. ${CALCIUM_BUILD_TESTS}")
message(STATUS "  Examples ........... ${CALCIUM_BUILD_EXAMPLES}")
message(STATUS "  Thread assertions .. ${CALCIUM_ENABLE_THREAD_ASSERTIONS}")
message(STATUS "  Alloc sentinel ..... ${CALCIUM_ENABLE_ALLOCATION_SENTINEL}")
message(STATUS "  Backends: sdl3=${CALCIUM_PLATFORM_SDL3} d3d12=${CALCIUM_GPU_D3D12} skia=${CALCIUM_RASTER_SKIA}")
message(STATUS "")
