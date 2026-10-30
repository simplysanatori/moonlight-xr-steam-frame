# Moonlight XR Native, tests/m6: the 3D pipeline headless. test_m6 runs
# xr_depth.c on the Frame's GPU with a fake depth engine (no LiteRT);
# mlxr-m6-pipeline runs the real thing (hardware decoder, downscale, LiteRT,
# upsample, offset, warp) on a clip and writes the eyes and the depth map.
# GPLv3.
set(M6_INCLUDES ${PROJECT_SOURCE_DIR}/tests/xrcore ${PROJECT_SOURCE_DIR}/tests/m5 ${PROJECT_SOURCE_DIR}/tests/m4
    ${PROJECT_SOURCE_DIR}/ui/third_party ${PROJECT_SOURCE_DIR}/video)

add_library(m6_gpu STATIC m6_gpu.c)
target_link_libraries(m6_gpu PUBLIC m5_gpu m4_scene mlxr_video)
target_include_directories(m6_gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${M6_INCLUDES})

add_executable(test_m6 test_m6.c)
target_link_libraries(test_m6 PRIVATE m6_gpu)
target_compile_options(test_m6 PRIVATE -Werror)
mlxr_link_openxr(test_m6)
foreach(g pipeline governor settings states budget sbs)
  add_test(NAME m6_${g} COMMAND test_m6 ${g})
  set_tests_properties(m6_${g} PROPERTIES SKIP_RETURN_CODE 77 LABELS m6)
endforeach()

if(TARGET mlxr_xrdepth)
  add_executable(mlxr-m6-pipeline m6_pipeline.c)
  target_link_libraries(mlxr-m6-pipeline PRIVATE m6_gpu mlxr_xrdepth mlxr_stream)
  target_compile_options(mlxr-m6-pipeline PRIVATE -Werror)
  target_compile_definitions(mlxr-m6-pipeline PRIVATE MLXR_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
  target_include_directories(mlxr-m6-pipeline PRIVATE ${PROJECT_SOURCE_DIR}/stream)
  mlxr_link_openxr(mlxr-m6-pipeline)
  # The real model on the stock test clip, 4 s: maps made, eyes apart, no
  # errors (skipped without the clip)
  add_test(NAME m6_pipeline_real COMMAND mlxr-m6-pipeline --clip $ENV{HOME}/Games/MoonlightXRNative/test-1080p60.mp4
           --seconds 4 --out ${CMAKE_CURRENT_BINARY_DIR}/real --assets ${PROJECT_SOURCE_DIR}/assets)
  set_tests_properties(m6_pipeline_real PROPERTIES SKIP_RETURN_CODE 77 LABELS m6 RUN_SERIAL 1)
endif()
