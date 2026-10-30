# M0: OpenXR probe + two-eye Vulkan triangle. GPLv3.
glsl_to_c(mlxr-hello HELLO_SPV shaders/tri.vert shaders/tri.frag)
add_executable(mlxr-hello xr_hello.c ${HELLO_SPV})
target_include_directories(mlxr-hello PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
mlxr_link_openxr(mlxr-hello)
target_link_libraries(mlxr-hello PRIVATE mlxr_input_core)
