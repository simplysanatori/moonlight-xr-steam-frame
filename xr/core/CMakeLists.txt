# mlxr_xrcore: the renderer's pure modules (no GL, no Vulkan, no Android),
# ported from Gilleece's Moonlight XR with their logic intact.
add_library(mlxr_xrcore STATIC
  xr_atlas.c xr_controller.c xr_depthmap.c xr_gamepad.c xr_gate.c xr_glow.c
  xr_grade.c xr_headaim.c xr_keys.c xr_layout.c xr_math.c xr_notice.c
  xr_pinch.c xr_rate.c xr_roommesh.c xr_audio.c xr_padexit.c xr_kbfull.c xr_clickhold.c xr_pressfix.c xr_ray.c xr_raylog.c
  xr_bind.c xr_bindrun.c xr_scroll.c xr_padmouse.c xr_presence.c xr_hud.c xr_battery.c xr_perflog.c
  xr_hostkeys.c xr_lightgun.c xr_padgyro.c xr_abr.c xr_opentrack.c)
target_include_directories(mlxr_xrcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlxr_xrcore PUBLIC OpenXR::headers PRIVATE m)
set_target_properties(mlxr_xrcore PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
# The audio kernels are built at -O2 upstream, and test_audio prints what they cost
set_source_files_properties(xr_audio.c PROPERTIES COMPILE_OPTIONS -O2)

# Shipped assets live in the repo's assets/ and tests get the path compiled in
set(MLXR_ASSETS_DIR ${PROJECT_SOURCE_DIR}/assets CACHE PATH "Shipped assets")

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/xrcore/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/xrcore ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
