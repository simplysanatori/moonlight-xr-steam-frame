# Input lane: gamepad mode's output to the host (pad_out) and the host's rumble
# back to the controllers (rumble). Owned by lane I. GPLv3.
#
# mlxr_input_core is pure (Limelight.h for the flag values only); tests use it.
# mlxr_input adds padOutUseLimelight(), the real moonlight-common-c sends, and
# is what the frame loop links.
find_package(Threads REQUIRED)
set(MLXR_MCC_SRC ${PROJECT_SOURCE_DIR}/third_party/moonlight-common-c/src)

add_library(mlxr_input_core STATIC pad_out.c rumble.c)
target_include_directories(mlxr_input_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${MLXR_MCC_SRC})
target_link_libraries(mlxr_input_core PUBLIC mlxr_xrcore Threads::Threads)
set_target_properties(mlxr_input_core PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

add_library(mlxr_input STATIC pad_out_ll.c)
target_link_libraries(mlxr_input PUBLIC mlxr_input_core moonlight-common-c)
set_target_properties(mlxr_input PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/input/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/input ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
