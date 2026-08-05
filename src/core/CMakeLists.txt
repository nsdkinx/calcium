add_library(calcium_core STATIC
    arena_allocator.cpp
    identifier.cpp
    interned_string.cpp
    thread_affinity.cpp
    time.cpp
    version.cpp
)
add_library(Calcium::core ALIAS calcium_core)

calcium_configure_module(calcium_core)

# Level 0: depends on nothing but the C++ standard library.
