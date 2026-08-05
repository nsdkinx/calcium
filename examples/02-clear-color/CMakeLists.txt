# Clear Color — the M1 exit demo (docs/06-roadmap.md).
#
# Consumes the umbrella the way an application does, plus the internal
# compositor header (a documented seam that closes when Level 3 lands and the
# compositor becomes framework-internal).

add_executable(example_clear_color main.cpp)

target_link_libraries(example_clear_color PRIVATE
    calcium_compositor
    calcium_backends
)
target_include_directories(example_clear_color PRIVATE "${CMAKE_SOURCE_DIR}/src")
calcium_apply_warnings(example_clear_color)
