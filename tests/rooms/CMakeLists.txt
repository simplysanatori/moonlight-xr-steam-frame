# Headless Vulkan tests for the rooms lane (no OpenXR). Each ctest entry runs
# one group of checks; images land in the build tree as PNG.
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
add_executable(mlxr-room-test test_rooms.c)
target_link_libraries(mlxr-room-test PRIVATE mlxr_rooms mlxr_passes mlxr_xrcore Vulkan::Vulkan PkgConfig::PNG m)
target_compile_definitions(mlxr-room-test PRIVATE MLXR_ASSETS_DIR="${MLXR_ASSETS_DIR}")
target_compile_options(mlxr-room-test PRIVATE -Werror)
set(MLXR_ROOM_TEST_OUT ${CMAKE_CURRENT_BINARY_DIR}/out)
file(MAKE_DIRECTORY ${MLXR_ROOM_TEST_OUT})
foreach(t logic load rescale render_home_theater render_grand_cinema render_synthwave model overlay)
  add_test(NAME rooms_${t} COMMAND mlxr-room-test ${t} ${MLXR_ROOM_TEST_OUT})
endforeach()
