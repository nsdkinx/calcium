add_library(calcium_geometry STATIC
    path.cpp
    rounded_rectangle.cpp
    transform_3d.cpp
)
add_library(Calcium::geometry ALIAS calcium_geometry)

calcium_configure_module(calcium_geometry)

# Level: geometry -> core
target_link_libraries(calcium_geometry PUBLIC calcium_core)
