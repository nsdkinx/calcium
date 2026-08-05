add_library(calcium_platform STATIC
    application.cpp
    display.cpp
)
add_library(Calcium::platform ALIAS calcium_platform)

calcium_configure_module(calcium_platform)

# Level 1: platform -> core, geometry
target_link_libraries(calcium_platform PUBLIC calcium_core calcium_geometry)
