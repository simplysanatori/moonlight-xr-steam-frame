# Moonlight XR Native: XR side. core/ = pure modules ported from Moonlight XR
# (lane X); the rest is the native session, Vulkan and rendering. GPLv3.
add_subdirectory(core)

glsl_to_c(mlxr_xr XR_SPV shaders/fullscreen.vert shaders/yuv420p.frag)
add_library(mlxr_xr STATIC mlxr_log.c xr_vk.c xr_session.c ${XR_SPV})
target_include_directories(mlxr_xr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(mlxr_xr PUBLIC openxr_loader OpenXR::headers Vulkan::Vulkan PkgConfig::AV mlxr_xrcore mlxr_video mlxr_passes)
