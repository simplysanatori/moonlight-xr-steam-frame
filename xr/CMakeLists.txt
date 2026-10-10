# Moonlight XR Native: XR side. core/ = pure modules ported from Moonlight XR
# (lane X); the rest is the renderer core ported from upstream's xr-renderer
# (xr_ui, xr_input, xr_layers, xr_display, xr_log, parts of xr_assets) around a
# native Vulkan session host (xr_session). GPLv3.
add_subdirectory(core)

# The renderer core without the session: everything the tests and the
# snapshot tool reach without a headset
add_library(mlxr_xrui STATIC
  xr_log.c xr_result.c xr_ui.c xr_input.c xr_layers.c xr_display.c
  xr_room.c xr_model.c xr_ambilight.c xr_world.c
  xr_assets.c xr_panels.c xr_prefs.c xr_vk.c xr_pad_bridge.c)
target_sources(mlxr_xrui PRIVATE xr_keyboard.c xr_depth.c)
# M6: xr_depth.c reads depth/depth_engine.h's types only; the engine itself
# comes in through mlxr_xrdepth below
target_include_directories(mlxr_xrui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/depth)
target_link_libraries(mlxr_xrui PUBLIC openxr_loader OpenXR::headers Vulkan::Vulkan mlxr_xrcore mlxr_ui mlxr_input mlxr_input_core
  mlxr_rooms mlxr_passes pthread m)
target_compile_definitions(mlxr_xrui PRIVATE MLXR_ASSETS_DIR="${MLXR_ASSETS_DIR}")

add_library(mlxr_xr STATIC xr_session.c)
target_include_directories(mlxr_xr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlxr_xr PUBLIC mlxr_xrui PkgConfig::AV mlxr_video mlxr_passes mlxr_audio)

# M6: the depth model behind the 3D, when lane D's LiteRT build is there
# (depth/CMakeLists.txt skips mlxr_depth otherwise and the 3D stays off)
if(TARGET mlxr_depth)
  add_library(mlxr_xrdepth STATIC xr_depth_engine.c)
  target_link_libraries(mlxr_xrdepth PUBLIC mlxr_xrui mlxr_depth)
  target_link_libraries(mlxr_xr PUBLIC mlxr_xrdepth)
  target_compile_definitions(mlxr_xr PRIVATE MLXR_HAVE_DEPTH=1)
endif()

# M6: the 3D pipeline, headless (tests/m6)
if(EXISTS ${PROJECT_SOURCE_DIR}/tests/m6/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/m6 ${CMAKE_CURRENT_BINARY_DIR}/tests_m6)
endif()

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/keyboard/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/keyboard ${CMAKE_CURRENT_BINARY_DIR}/tests_keyboard)
endif()

# M5: the room, the glow, the models and the click, headless (tests/m5)
if(EXISTS ${PROJECT_SOURCE_DIR}/tests/m5/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/m5 ${CMAKE_CURRENT_BINARY_DIR}/tests_m5)
endif()
