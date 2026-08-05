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

# The Twell convention test is the only target that includes twell.h directly.
# Production code reaches Twell through ca::animation exclusively (P5).
calcium_add_test(test_twell_conventions unit/test_twell_conventions.cpp)
target_link_libraries(test_twell_conventions PRIVATE twell)
