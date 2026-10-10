# Depth model tests (ctest -R depth). They load the shipped models from
# assets/depth. GPLv3.
add_executable(test_depth test_depth.c scene.c)
target_compile_definitions(test_depth PRIVATE MLXR_DEPTH_ASSETS="${MLXR_DEPTH_ASSETS}")
target_compile_options(test_depth PRIVATE -O2 -Werror)
target_include_directories(test_depth PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
mlxr_link_depth(test_depth)
foreach(group spec order stable agree arith norm engine)
  add_test(NAME depth_${group} COMMAND test_depth ${group})
  set_tests_properties(depth_${group} PROPERTIES LABELS depth TIMEOUT 300)
endforeach()
