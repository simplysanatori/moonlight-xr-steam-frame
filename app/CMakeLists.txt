# Moonlight XR Native: the moonlight-xr binary. GPLv3.
add_executable(moonlight-xr main.c)
target_link_libraries(moonlight-xr PRIVATE mlxr_stream mlxr_xr)
mlxr_link_openxr(moonlight-xr)
set_target_properties(moonlight-xr PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
