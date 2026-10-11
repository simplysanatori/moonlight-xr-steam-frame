# Moonlight XR Native: stream side (orchestrator). GPLv3.
find_package(OpenSSL REQUIRED)
add_library(mlxr_stream STATIC frame_slot.c sw_decoder.c file_source.c session.c evdev_keyboard.c hw_video.c pacer.c rfi_watch.c vr_keyboard.c)
target_include_directories(mlxr_stream PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/moonlight-common-c/src)
target_link_libraries(mlxr_stream PUBLIC PkgConfig::AV moonlight-common-c OpenSSL::Crypto pthread mlxr_video mlxr_xrcore mlxr_input mlxr_input_core mlxr_audio)
add_subdirectory(${PROJECT_SOURCE_DIR}/tests/stream ${CMAKE_CURRENT_BINARY_DIR}/tests)
