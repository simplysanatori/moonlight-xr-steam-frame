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

foreach(t prefs hits launcher budget vrkb panels_async kbbar stats)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/test_${t}.c)
    add_executable(test_m4_${t} test_${t}.c)
    target_link_libraries(test_m4_${t} PRIVATE m4_scene mlxr_launcher mlxr_stream)
    target_compile_options(test_m4_${t} PRIVATE -Werror)
    mlxr_link_openxr(test_m4_${t})
    add_test(NAME m4_${t} COMMAND test_m4_${t})
  endif()
endforeach()
