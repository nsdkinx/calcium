# SDL3 renderer GPU backend (M1 — the only GPU backend until the MVP).
#
# SDL3 is vendored at third_party/sdl3 (version in VERSION.txt) and consumed as
# an imported shared library; its DLL is copied next to the umbrella at build
# time (see the root CMakeLists). The `sdl3` target is defined in
# src/backends/CMakeLists.txt so both backends share it.

add_library(gpu_sdl3 STATIC
    sdl3_gpu_device.cpp
)

target_link_libraries(gpu_sdl3
    PUBLIC sdl3 calcium_core calcium_geometry calcium_gpu
)
calcium_configure_module(gpu_sdl3)
