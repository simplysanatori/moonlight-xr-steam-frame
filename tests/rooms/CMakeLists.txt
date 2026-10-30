# Headless Vulkan tests for the rooms lane (no OpenXR). Each ctest entry runs
# one group of checks; images land in the build tree as PNG.
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
add_executable(mlxr-room-test test_rooms.c)
target_link_libraries(mlxr-room-test PRIVATE mlxr_rooms mlxr_passes mlxr_xrcore Vulkan::Vulkan PkgConfig::PNG m)
target_compile_definitions(mlxr-room-test PRIVATE MLXR_ASSETS_DIR="${MLXR_ASSETS_DIR}")
target_compile_options(mlxr-room-test PRIVATE -Werror)
set(MLXR_ROOM_TEST_OUT ${CMAKE_CURRENT_BINARY_DIR}/out)
file(MAKE_DIRECTORY ${MLXR_ROOM_TEST_OUT})
foreach(t logic load rescale render_home_theater render_grand_cinema render_synthwave model overlay)
  add_test(NAME rooms_${t} COMMAND mlxr-room-test ${t} ${MLXR_ROOM_TEST_OUT})
endforeach()

# Native: the textured Steam Frame models. The converter runs first into this
# build tree (never the source tree: Valve's models, converted, stay out of
# git) when this machine has SteamVR; model_textured then renders them too.
set(MLXR_ROOM_TEST_FRAME ${CMAKE_CURRENT_BINARY_DIR}/frame-models)
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME rooms_frame_models_convert
    COMMAND sh -c "if [ -d \"$1\" ]; then exec \"$2\" -I \"$3\" \"$4\" \"$1\"; else echo \"no SteamVR models at $1\"; exit 77; fi" sh
            /opt/steamvr/drivers/frame_controller/resources/rendermodels ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tools/frame-models/frame-controller-models.py ${MLXR_ROOM_TEST_FRAME})
  # The converted models are the textured test's input, and the converter
  # rewrites them: a fixture, and the same lock as every test that reads or
  # writes the folder (ctest -j ran them at once otherwise)
  set_tests_properties(rooms_frame_models_convert PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300
    FIXTURES_SETUP frame_models RESOURCE_LOCK room_test_models)
endif()
add_test(NAME rooms_model_textured COMMAND mlxr-room-test model_textured ${MLXR_ROOM_TEST_OUT} ${MLXR_ROOM_TEST_FRAME})
if(Python3_FOUND)
  set_tests_properties(rooms_model_textured PROPERTIES FIXTURES_REQUIRED frame_models)
endif()
# Native: the moving parts (made-up models, and the real ones when converted)
add_test(NAME rooms_model_pose COMMAND mlxr-room-test model_pose ${MLXR_ROOM_TEST_OUT} ${MLXR_ROOM_TEST_FRAME})
if(Python3_FOUND)
  set_tests_properties(rooms_model_pose PROPERTIES FIXTURES_REQUIRED frame_models)
endif()
add_test(NAME rooms_model_fallback COMMAND mlxr-room-test model_fallback ${MLXR_ROOM_TEST_OUT})
# Native: the model by kind (Show controllers): gun, Frame, custom and their fall backs
add_test(NAME rooms_model_kind COMMAND mlxr-room-test model_kind ${MLXR_ROOM_TEST_OUT})
set_tests_properties(rooms_model_textured rooms_model_pose rooms_model_fallback rooms_model_kind PROPERTIES RESOURCE_LOCK room_test_models)
# exit 77 (no Vulkan driver or device, e.g. a CI runner) is a skip
set_tests_properties(rooms_load rooms_rescale rooms_render_home_theater rooms_render_grand_cinema rooms_render_synthwave
  rooms_model rooms_overlay rooms_model_textured rooms_model_pose rooms_model_fallback rooms_model_kind PROPERTIES SKIP_RETURN_CODE 77)
