# SDL3 platform backend (M1: the portable bring-up path).
#
# The `sdl3` imported target is defined in src/backends/CMakeLists.txt so the
# GPU backend shares it.

add_library(platform_sdl3 STATIC
    sdl3_platform.cpp
)

target_link_libraries(platform_sdl3
    PUBLIC sdl3 calcium_core calcium_geometry calcium_platform
)
calcium_configure_module(platform_sdl3)
