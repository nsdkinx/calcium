# Spring Rectangle — the M2 exit demo (docs/06-roadmap.md M2).
#
# Consumes the umbrella the way an application does, plus the internal
# compositor header (the documented seam that closes when the compositor
# becomes framework-internal).

add_executable(example_spring_rectangle main.cpp)

target_link_libraries(example_spring_rectangle PRIVATE
    calcium_compositor
    calcium_backends
)
target_include_directories(example_spring_rectangle PRIVATE "${CMAKE_SOURCE_DIR}/src")
calcium_apply_warnings(example_spring_rectangle)
