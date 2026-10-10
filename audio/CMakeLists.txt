# Audio lane: Opus decode, HRTF virtual surround, PulseAudio playback, UI click.
# Owned by lane A (audio). GPLv3.
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
pkg_check_modules(PULSE_SIMPLE REQUIRED IMPORTED_TARGET libpulse-simple)

add_library(mlxr_audio STATIC
  hrtf.c surround.c pcmq.c pulse_out.c opus_out.c click.c)
target_include_directories(mlxr_audio PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/third_party/moonlight-common-c/src)
target_link_libraries(mlxr_audio PUBLIC mlxr_xrcore PkgConfig::OPUS PkgConfig::PULSE m pthread)
set_target_properties(mlxr_audio PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

# Manual by-ear test: plays a tone through each channel in turn
add_executable(mlxr-audio-test audio_test_main.c)
target_link_libraries(mlxr-audio-test PRIVATE mlxr_audio)
target_compile_definitions(mlxr-audio-test PRIVATE MLXR_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/audio/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/audio ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
