# Moonlight XR Native - lane V: V4L2 M2M decode + zero-copy Vulkan import.
# Copyright (C) 2026 Moonlight XR Native contributors. GPLv3, see LICENSE.txt.
add_library(mlxr_video STATIC v4l2_decoder.c vk_import.c)
target_include_directories(mlxr_video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlxr_video PUBLIC Vulkan::Vulkan pthread)

# Headless test harness and ctest entries live in tests/video.
add_subdirectory(${PROJECT_SOURCE_DIR}/tests/video ${PROJECT_BINARY_DIR}/tests/video)
