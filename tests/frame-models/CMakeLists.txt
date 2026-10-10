# Moonlight XR Native, tests/frame-models: the Steam Frame controller model
# converter (tools/frame-models) on a made-up pair of SteamVR render models,
# and on the real ones when this machine has SteamVR. Skipped (77) without
# python3 GdkPixbuf. GPLv3.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME frame_models_converter
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_converter.py
            ${PROJECT_SOURCE_DIR}/tools/frame-models/frame-controller-models.py)
  set_tests_properties(frame_models_converter PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 300)
endif()
