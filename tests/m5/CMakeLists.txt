# Moonlight XR Native, tests/m5: the rooms, the glow, the controller models and
# the click through the ported code (test_m5: no GPU; test_world: the Frame's
# GPU, headless), and mlxr-world-snapshot, which adds each room with the
# picture placed, the models and the glow to the ui-snapshots pictures. GPLv3.
set(M5_INCLUDES ${PROJECT_SOURCE_DIR}/tests/xrcore ${PROJECT_SOURCE_DIR}/tests/m4)

add_library(m5_gpu STATIC m5_gpu.c)
target_link_libraries(m5_gpu PUBLIC mlxr_xrui)
target_include_directories(m5_gpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${M5_INCLUDES})
target_compile_definitions(m5_gpu PRIVATE MLXR_ASSETS_DIR="${MLXR_ASSETS_DIR}")

add_executable(test_m5 test_m5.c)
target_link_libraries(test_m5 PRIVATE m4_scene)
target_include_directories(test_m5 PRIVATE ${M5_INCLUDES})
target_compile_options(test_m5 PRIVATE -Werror)
mlxr_link_openxr(test_m5)
foreach(g room env budget click one_layer eye_swapchains)
  add_test(NAME m5_${g} COMMAND test_m5 ${g})
endforeach()

add_executable(test_m5_world test_world.c)
target_link_libraries(test_m5_world PRIVATE m5_gpu m4_scene dl)
target_compile_options(test_m5_world PRIVATE -Werror)
mlxr_link_openxr(test_m5_world)
foreach(g leaks timing glow curve supersample frame_models eye_images)
  add_test(NAME m5_world_${g} COMMAND test_m5_world ${g})
endforeach()

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/world_snapshot.c)
  add_executable(mlxr-world-snapshot world_snapshot.c)
  target_link_libraries(mlxr-world-snapshot PRIVATE m5_gpu m4_scene)
  mlxr_link_openxr(mlxr-world-snapshot)
  add_test(NAME m5_world_snapshot COMMAND mlxr-world-snapshot ${CMAKE_CURRENT_BINARY_DIR}/snapshots)
endif()

# The generic controller beside the Steam Frame's own (tools/frame-models) at
# the same grips; run by hand: mlxr-model-snapshot OUTDIR FRAMEMODELSDIR
add_executable(mlxr-model-snapshot model_snapshot.c)
target_link_libraries(mlxr-model-snapshot PRIVATE m5_gpu m4_scene)
target_compile_definitions(mlxr-model-snapshot PRIVATE MLXR_ASSETS_DIR="${MLXR_ASSETS_DIR}")
mlxr_link_openxr(mlxr-model-snapshot)

# The Frame's textured models with their moving parts posed (trigger, grip, stick,
# buttons, d-pad); run by hand: mlxr-model-pose-snapshot OUTDIR FRAMEMODELSDIR
add_executable(mlxr-model-pose-snapshot model_pose_snapshot.c)
target_link_libraries(mlxr-model-pose-snapshot PRIVATE m5_gpu m4_scene)
target_compile_definitions(mlxr-model-pose-snapshot PRIVATE MLXR_ASSETS_DIR="${MLXR_ASSETS_DIR}")
mlxr_link_openxr(mlxr-model-pose-snapshot)
