# Moonlight XR Native: XR side. core/ = pure modules ported from Moonlight XR
# (lane X); the rest is the renderer core ported from upstream's xr-renderer
# (xr_ui, xr_input, xr_layers, xr_display, xr_log, parts of xr_assets) around a
# native Vulkan session host (xr_session). GPLv3.
add_subdirectory(core)

# The renderer core without the session: everything the tests and the
# snapshot tool reach without a headset
add_library(mlxr_xrui STATIC
  xr_log.c xr_result.c xr_ui.c xr_input.c xr_layers.c xr_display.c xr_later.c
  xr_assets.c xr_panels.c xr_prefs.c xr_vk.c xr_pad_bridge.c)
target_sources(mlxr_xrui PRIVATE xr_keyboard.c)
target_include_directories(mlxr_xrui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlxr_xrui PUBLIC openxr_loader OpenXR::headers Vulkan::Vulkan mlxr_xrcore mlxr_ui mlxr_input mlxr_input_core pthread m)

add_library(mlxr_xr STATIC xr_session.c)
target_include_directories(mlxr_xr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlxr_xr PUBLIC mlxr_xrui PkgConfig::AV mlxr_video mlxr_passes)

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/keyboard/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/keyboard ${CMAKE_CURRENT_BINARY_DIR}/tests_keyboard)
endif()
