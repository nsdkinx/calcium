# Test suite.
#
# Each test binary is self-contained (its own main via CA_TEST_MAIN), so a crash
# in one does not take the whole suite down with it.

function(calcium_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE calcium_core calcium_geometry)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    calcium_apply_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60)
endfunction()

calcium_add_test(test_core            unit/test_core.cpp)
calcium_add_test(test_geometry        unit/test_geometry.cpp)
calcium_add_test(test_transform_3d    unit/test_transform_3d.cpp)
calcium_add_test(test_identifier      unit/test_identifier.cpp)
calcium_add_test(test_interned_string unit/test_interned_string.cpp)
calcium_add_test(test_small_vector    unit/test_small_vector.cpp)
calcium_add_test(test_affine_transform unit/test_affine_transform.cpp)
calcium_add_test(test_path            unit/test_path.cpp)
calcium_add_test(test_rounded_rectangle unit/test_rounded_rectangle.cpp)
calcium_add_test(test_spsc_ring         unit/test_spsc_ring.cpp)
calcium_add_test(test_color             unit/test_color.cpp)

# The animation test links the module that owns TWELL_IMPL; it must NOT
# define TWELL_IMPL itself.
add_executable(test_animation unit/test_animation.cpp)
target_link_libraries(test_animation PRIVATE
    calcium_animation calcium_core calcium_geometry)
target_include_directories(test_animation PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
calcium_apply_warnings(test_animation)
add_test(NAME test_animation COMMAND test_animation)
set_tests_properties(test_animation PROPERTIES TIMEOUT 60)
# Production code reaches Twell through ca::animation exclusively (P5).
calcium_add_test(test_twell_conventions unit/test_twell_conventions.cpp)
target_link_libraries(test_twell_conventions PRIVATE twell)

# The display-list tests link the graphics module (the recorder + IR) and the
# internal rasterizer (src/ — it is not public API).
add_executable(test_display_list unit/test_display_list.cpp)
target_link_libraries(test_display_list PRIVATE
    calcium_graphics calcium_core calcium_geometry)
target_include_directories(test_display_list PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
calcium_apply_warnings(test_display_list)
add_test(NAME test_display_list COMMAND test_display_list)
set_tests_properties(test_display_list PROPERTIES TIMEOUT 60)

add_executable(test_rasterizer unit/test_rasterizer.cpp)
target_link_libraries(test_rasterizer PRIVATE
    calcium_graphics calcium_gpu calcium_core calcium_geometry)
target_include_directories(test_rasterizer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_SOURCE_DIR}/src")
calcium_apply_warnings(test_rasterizer)
add_test(NAME test_rasterizer COMMAND test_rasterizer)
set_tests_properties(test_rasterizer PROPERTIES TIMEOUT 60)

# The layer tests exercise the tree + the coordinator together (the layer
# module links animation).
add_executable(test_layer unit/test_layer.cpp)
target_link_libraries(test_layer PRIVATE
    calcium_layer calcium_animation calcium_graphics
    calcium_core calcium_geometry)
target_include_directories(test_layer PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
calcium_apply_warnings(test_layer)
add_test(NAME test_layer COMMAND test_layer)
set_tests_properties(test_layer PROPERTIES TIMEOUT 60)
