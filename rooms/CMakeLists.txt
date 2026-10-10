# Moonlight XR Native: lane R, the 3D rooms and the controller model on
# Vulkan: room_logic (pure, from xr_room.c) and room_gpu (loading and
# uploading). GPLv3.
set(MLXR_ASSETS_DIR ${PROJECT_SOURCE_DIR}/assets CACHE PATH "Shipped assets")
add_library(mlxr_rooms STATIC room_logic.c room_gpu.c)
target_include_directories(mlxr_rooms PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlxr_rooms PUBLIC mlxr_xrcore mlxr_passes Vulkan::Vulkan PRIVATE m)
target_compile_options(mlxr_rooms PRIVATE -Werror)

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/rooms/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/rooms ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
