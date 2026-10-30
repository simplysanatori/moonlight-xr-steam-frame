# Headless Vulkan tests for mlxr_passes (no OpenXR). Each ctest entry runs
# one group of checks and fails on any miss; PPMs land in the build tree.
add_executable(mlxr-shader-test test_shaders.c vkh.c text_scene.c)
target_link_libraries(mlxr-shader-test PRIVATE mlxr_passes mlxr_xrcore Vulkan::Vulkan m)
target_compile_options(mlxr-shader-test PRIVATE -Werror)
# The text clarity group's desktop: Noto Sans through stb_truetype (the UI's copy)
target_include_directories(mlxr-shader-test PRIVATE ${PROJECT_SOURCE_DIR}/ui/third_party)
target_compile_definitions(mlxr-shader-test PRIVATE
  MLXR_TEXT_FONT="${PROJECT_SOURCE_DIR}/assets/fonts/NotoSans-Regular.ttf")
set_source_files_properties(text_scene.c PROPERTIES COMPILE_OPTIONS "-w")
set(MLXR_SHADER_TEST_OUT ${CMAKE_CURRENT_BINARY_DIR}/out)
file(MAKE_DIRECTORY ${MLXR_SHADER_TEST_OUT})
foreach(t device grade nv12 srgb warp_identity warp_step warp_fur warp_sbs warp_zoom sharpen upsample offset offset_balance downscale ambi
          glow_edge glow glow_feather room room_dim model supersample panel text)
  add_test(NAME shaders_${t} COMMAND mlxr-shader-test ${t} ${MLXR_SHADER_TEST_OUT})
  set_tests_properties(shaders_${t} PROPERTIES SKIP_RETURN_CODE 77)   # 77: no Vulkan driver or device
endforeach()
