# M0 placeholder.
#
# Becomes the real Level-3 example at M2: a rounded rect that springs to a new
# position on tap, with velocity preserved across interruption. For now it just
# exercises the geometry and animation math that M0 delivers.

add_executable(example_hello_rectangle main.cpp)

# Links the umbrella shared library, not the static modules directly: examples
# consume Calcium the way an application does, so a missing export shows up here
# rather than in someone else's project.
target_link_libraries(example_hello_rectangle PRIVATE calcium)
calcium_apply_warnings(example_hello_rectangle)
