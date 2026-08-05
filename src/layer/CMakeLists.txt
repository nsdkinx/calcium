add_library(calcium_layer STATIC
    layer_tree.cpp
)
add_library(Calcium::layer ALIAS calcium_layer)

calcium_configure_module(calcium_layer)

# Level 3 (layer) -> core, geometry, graphics, animation (the hygiene gate's
# DAG for layer). The tree registers its animatable properties with the
# coordinator, and packets carry sealed display lists.
target_link_libraries(calcium_layer
    PUBLIC  calcium_core calcium_geometry calcium_graphics calcium_animation
)
