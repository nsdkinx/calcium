add_library(calcium_animation STATIC
    animation_coordinator.cpp
    motion.cpp
    motion_scheme.cpp
    transaction.cpp
    twell_impl.cpp
)
add_library(Calcium::animation ALIAS calcium_animation)

calcium_configure_module(calcium_animation)

# Level 3 (animation) -> core, geometry. Twell is PRIVATE: its symbols never
# reach a public header (docs/03-project-structure.md section 3).
target_link_libraries(calcium_animation
    PUBLIC  calcium_core calcium_geometry
    PRIVATE twell
)
