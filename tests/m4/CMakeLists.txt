# Moonlight XR Native, tests/m4 (the in-headset UI): unit tests of the glue
# between the ported renderer core, lane U's panels, config.ini and the
# launcher, plus mlxr-ui-snapshot, which composes the UI's states into PNGs
# without a headset (target ui-snapshots, into build/ui-snapshots/). GPLv3.
set(M4_INCLUDES ${PROJECT_SOURCE_DIR}/tests/xrcore ${PROJECT_SOURCE_DIR}/ui/third_party)

add_library(m4_scene STATIC scene.c)
target_link_libraries(m4_scene PUBLIC mlxr_xrui)
target_include_directories(m4_scene PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${M4_INCLUDES})

add_executable(mlxr-ui-snapshot ui_snapshot.c)
target_link_libraries(mlxr-ui-snapshot PRIVATE m4_scene)
mlxr_link_openxr(mlxr-ui-snapshot)
# M5: the rooms, the glow and the models join them (tests/m5, mlxr-world-snapshot)
add_custom_target(ui-snapshots COMMAND mlxr-ui-snapshot ${PROJECT_BINARY_DIR}/ui-snapshots
  COMMAND mlxr-world-snapshot ${PROJECT_BINARY_DIR}/ui-snapshots DEPENDS mlxr-ui-snapshot mlxr-world-snapshot)
add_test(NAME m4_snapshot_budget COMMAND mlxr-ui-snapshot ${CMAKE_CURRENT_BINARY_DIR}/snapshots)
# Native: the snapshots against the goldens in tests/ui/golden (shrunk 4x; a
# state fails past 0.1% of its pixels off by more than 16 levels, see
# snapshot_golden.py). MLXR_UPDATE_GOLDEN=1 rewrites the goldens instead.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  add_test(NAME m4_snapshot_golden COMMAND ${Python3_EXECUTABLE} -I ${CMAKE_CURRENT_SOURCE_DIR}/snapshot_golden.py
           ${CMAKE_CURRENT_BINARY_DIR}/snapshots ${PROJECT_SOURCE_DIR}/tests/ui/golden)
  set_tests_properties(m4_snapshot_budget PROPERTIES FIXTURES_SETUP ui_snapshots)
  set_tests_properties(m4_snapshot_golden PROPERTIES FIXTURES_REQUIRED ui_snapshots SKIP_RETURN_CODE 77 TIMEOUT 600)
endif()

foreach(t prefs hits launcher budget vrkb panels_async kbbar stats replay hud place bindings cards lang lightgun splash hostpc headinput access)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_${t}.c)
    add_executable(test_m4_${t} test_${t}.c)
    target_link_libraries(test_m4_${t} PRIVATE m4_scene mlxr_launcher mlxr_stream)
    target_compile_options(test_m4_${t} PRIVATE -Werror)
    mlxr_link_openxr(test_m4_${t})
    add_test(NAME m4_${t} COMMAND test_m4_${t})
  endif()
endforeach()

# Native (lane ray-glitch): the ray recorder's CSV replayed through the input
# code, a synthetic one made by data/make_ray_replay.py
if(TARGET test_m4_replay)
  target_compile_definitions(test_m4_replay PRIVATE
    MLXR_REPLAY_CSV="${CMAKE_CURRENT_SOURCE_DIR}/data/ray-replay.csv")
  # Native (lane bar-glare): the ray along the bar while the other hand points
  # at the picture, data/make_bar_replay.py
  add_test(NAME m4_replay_bar COMMAND test_m4_replay ${CMAKE_CURRENT_SOURCE_DIR}/data/bar-replay.csv)
endif()
