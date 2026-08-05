# The compositor (internal — Level 3 machinery; not public API).
#
# In-repo examples link it directly until Level 3 (ca::layer/ca::animation)
# lands and the compositor becomes framework-internal; that is the documented
# temporary seam.
#
# The compositor links the umbrella shared library rather than the static
# modules: the umbrella already contains them (plus the backends), and linking
# both would duplicate every exported symbol at the consumer's link.

add_library(calcium_compositor STATIC
    compositor.cpp
)
add_library(Calcium::compositor ALIAS calcium_compositor)

calcium_configure_module(calcium_compositor)

target_link_libraries(calcium_compositor
    PUBLIC calcium_core calcium_geometry calcium_platform calcium_gpu
    PRIVATE calcium_backends
)
