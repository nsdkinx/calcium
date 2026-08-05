# SDL3 platform backend (M1: the portable bring-up path).

add_library(platform_sdl3 STATIC
    sdl3_platform.cpp
)

# SDL3 is vendored at third_party/sdl3 (version in VERSION.txt) and consumed as
# an imported shared library; its DLL is copied next to the umbrella at build
# time (see the root CMakeLists).
add_library(sdl3 SHARED IMPORTED)
set_target_properties(sdl3 PROPERTIES
    IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/third_party/sdl3/lib/SDL3.dll"
    IMPORTED_IMPLIB    "${CMAKE_SOURCE_DIR}/third_party/sdl3/lib/SDL3.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/third_party/sdl3/include"
)

target_link_libraries(platform_sdl3
    PUBLIC sdl3 calcium_core calcium_geometry calcium_platform
)
calcium_configure_module(platform_sdl3)
