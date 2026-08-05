add_library(calcium_graphics STATIC
    display_list.cpp
    rasterizer.cpp
)
add_library(Calcium::graphics ALIAS calcium_graphics)

calcium_configure_module(calcium_graphics)

# Level 2 (graphics) -> core, geometry. The rasterizer (internal —
# src/graphics/rasterizer.cpp) emits into the gpu interface (Level 1), which
# is a downward dependency; it is PRIVATE because graphics' public headers
# never name gpu (the hygiene gate's DAG allows graphics only core+geometry,
# and that is the surface that stays).
target_link_libraries(calcium_graphics
    PUBLIC  calcium_core calcium_geometry
    PRIVATE calcium_gpu
)
