# Moonlight XR Native: the moonlight-xr binary. GPLv3.
add_library(mlxr_launcher STATIC launcher.c)
target_include_directories(mlxr_launcher PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlxr_launcher PUBLIC mlxr_ui)

add_executable(moonlight-xr main.c)
target_link_libraries(moonlight-xr PRIVATE mlxr_launcher mlxr_stream mlxr_xr mlxr_backend mlxr_backend_ll mlxr_audio)
target_compile_definitions(moonlight-xr PRIVATE MLXR_SOURCE_ASSETS="${PROJECT_SOURCE_DIR}/assets")
mlxr_link_openxr(moonlight-xr)
set_target_properties(moonlight-xr PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

add_subdirectory(${PROJECT_SOURCE_DIR}/tests/m4 ${CMAKE_CURRENT_BINARY_DIR}/tests)
