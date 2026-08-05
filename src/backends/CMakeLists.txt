# Backends (never public — docs/02-architecture.md §6).
#
# Each backend is a separate static library with no consumers except the
# umbrella, which links them all and registers their factories. A backend may
# include third-party headers freely; nothing it exports may reach a public
# header (enforced by tools/ci/check_header_hygiene.py).

add_library(calcium_backends STATIC
    backend_registration.cpp
)
add_library(Calcium::backends ALIAS calcium_backends)
calcium_configure_module(calcium_backends)

if(CALCIUM_PLATFORM_SDL3)
    add_subdirectory(platform_sdl3)
    target_link_libraries(calcium_backends PUBLIC platform_sdl3)
endif()

if(CALCIUM_GPU_D3D12 AND WIN32)
    add_subdirectory(gpu_d3d12)
    target_link_libraries(calcium_backends PUBLIC gpu_d3d12)
endif()
