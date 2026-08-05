# Backends (never public — docs/02-architecture.md §6).
#
# Each backend is a separate static library with no consumers except the
# umbrella, which links them all and registers their factories. A backend may
# include third-party headers freely; nothing it exports may reach a public
# header (enforced by tools/ci/check_header_hygiene.py).
#
# SDL3 is vendored at third_party/sdl3 (version in VERSION.txt) and consumed as
# an imported shared library; its DLL is copied next to the umbrella at build
# time (see the root CMakeLists). The target is defined here so every SDL3
# backend (platform and GPU) shares it.
add_library(sdl3 SHARED IMPORTED)
set_target_properties(sdl3 PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/sdl3/lib/SDL3.dll"
    IMPORTED_IMPLIB    "${CMAKE_SOURCE_DIR}/third_party/sdl3/lib/SDL3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/sdl3/include"
)

add_library(calcium_backends STATIC
    backend_registration.cpp
)
add_library(Calcium::backends ALIAS calcium_backends)
calcium_configure_module(calcium_backends)

if(CALCIUM_PLATFORM_SDL3)
    add_subdirectory(platform_sdl3)
    target_link_libraries(calcium_backends PUBLIC platform_sdl3)
endif()

# SDL3 is the only GPU backend until the MVP (docs/06-roadmap.md M1); the
# D3D12/Vulkan/Metal backends return after the core stabilizes.
if(CALCIUM_GPU_SDL3)
    add_subdirectory(gpu_sdl3)
    target_link_libraries(calcium_backends PUBLIC gpu_sdl3)
endif()
