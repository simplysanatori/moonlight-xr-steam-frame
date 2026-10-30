# Moonlight XR for Frame, tests/tools: the bug report masks what a tester might
# not want to share, and the tracked tree passes the privacy check. GPLv3.
add_test(NAME tools_bug_report COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_bug_report.sh)
add_test(NAME tools_privacy_check COMMAND ${PROJECT_SOURCE_DIR}/tools/privacy-check.sh)
set_tests_properties(tools_bug_report tools_privacy_check PROPERTIES TIMEOUT 120)

# tools/release.sh: the .tar.gz and the .zip (for Frametanium and FrameDrop) hold the same entries with the
# same modes, both pass the privacy check, and install.sh from the unpacked zip works with no terminal and no
# Steam. Binaries are copies of /bin/true, so no build is needed.
add_test(NAME tools_release_archives COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_release_archives.sh)
set_tests_properties(tools_release_archives PROPERTIES TIMEOUT 300)

# tools/xr/make_model.py (glTF/GLB/OBJ to the client's MXFM model format) on tiny generated models,
# read back with a Python mirror of rooms/room_gpu.c's reader. Plain python3, no GdkPixbuf.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME tools_make_model
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_make_model.py
            ${PROJECT_SOURCE_DIR}/tools/xr/make_model.py ${PROJECT_SOURCE_DIR}/tools/xr/make_controller.py)
  set_tests_properties(tools_make_model PROPERTIES TIMEOUT 120)
endif()

# tools/xr/make_gun.py (converts the Gun controller model, a G17 under CC BY 4.0, into MXFM) on a
# stand-in pistol: parts left out, scale, the bore on the aim ray, the grip placed, the trigger split
# out, colours, the mirrored left hand; then the committed assets/models/gun_{right,left}.model.
if(Python3_FOUND)
  add_test(NAME tools_make_gun
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_make_gun.py
            ${PROJECT_SOURCE_DIR}/tools/xr/make_gun.py ${PROJECT_SOURCE_DIR}/assets/models)
  set_tests_properties(tools_make_gun PROPERTIES TIMEOUT 120)
endif()

# packaging/steam_cef.py (the Steam CEF driver, and the PC VR hand-off's JavaScript, list parsing and
# preflight) against a fake CEF endpoint (tests/tools/fake_cef.py, a small HTTP + websocket stub).
if(Python3_FOUND)
  add_test(NAME tools_steam_cef
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_steam_cef.py
            ${PROJECT_SOURCE_DIR}/packaging/steam_cef.py)
  set_tests_properties(tools_steam_cef PROPERTIES TIMEOUT 120)
endif()

# tools/probe/doctor.py (the update doctor: the mlxr-hello --probe diff against a baseline, and the
# Steam debugging port line).
if(Python3_FOUND)
  add_test(NAME tools_doctor
    COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/test_doctor.py
            ${PROJECT_SOURCE_DIR}/tools/probe/doctor.py ${PROJECT_SOURCE_DIR}/docs/probe/openxr-probe.txt
            ${PROJECT_SOURCE_DIR}/packaging/steam_cef.py)
  set_tests_properties(tools_doctor PROPERTIES TIMEOUT 120)
endif()

# packaging/install.sh --desktop-shortcut (PLAN 6.4): the "Desktop in VR" Steam shortcut, against the fake CEF
# endpoint; binaries are copies of /bin/true.
if(Python3_FOUND)
  add_test(NAME tools_install_desktop_shortcut COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_install_desktop_shortcut.sh)
  set_tests_properties(tools_install_desktop_shortcut PROPERTIES TIMEOUT 300)
endif()
