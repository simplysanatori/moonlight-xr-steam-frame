# Moonlight XR for Frame, tests/branding: the dock icon installer (packaging/dock_icon.py)
# in a temporary HOME, and the branding art it installs. Touches nothing outside it. GPLv3.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME branding_dock_icon
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_dock_icon.py
            ${PROJECT_SOURCE_DIR}/packaging/dock_icon.py ${PROJECT_SOURCE_DIR}/assets/branding)
  set_tests_properties(branding_dock_icon PROPERTIES TIMEOUT 60)
endif()
