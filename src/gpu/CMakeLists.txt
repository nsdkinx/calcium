add_library(calcium_gpu STATIC
    graphics_device.cpp
)
add_library(Calcium::gpu ALIAS calcium_gpu)

calcium_configure_module(calcium_gpu)

# Level 1: gpu -> core, geometry only (the level DAG, docs/02-architecture §1)
target_link_libraries(calcium_gpu PUBLIC calcium_core calcium_geometry)
