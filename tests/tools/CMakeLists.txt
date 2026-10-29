# Moonlight XR for Frame, tests/tools: the bug report masks what a tester might
# not want to share, and the tracked tree passes the privacy check. GPLv3.
add_test(NAME tools_bug_report COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_bug_report.sh)
add_test(NAME tools_privacy_check COMMAND ${PROJECT_SOURCE_DIR}/tools/privacy-check.sh)
set_tests_properties(tools_bug_report tools_privacy_check PROPERTIES TIMEOUT 120)

# tools/xr/make_model.py (glTF/GLB/OBJ to the client's MXFM model format) on tiny generated models,
# read back with a Python mirror of rooms/room_gpu.c's reader. Plain python3, no GdkPixbuf.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME tools_make_model
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_make_model.py
            ${PROJECT_SOURCE_DIR}/tools/xr/make_model.py ${PROJECT_SOURCE_DIR}/tools/xr/make_controller.py)
  set_tests_properties(tools_make_model PROPERTIES TIMEOUT 120)
endif()
