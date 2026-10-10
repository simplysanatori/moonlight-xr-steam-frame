# Moonlight XR Native, tests/keyboard: the Full keyboard (layout, modifier
# rules, auto-repeat, Caps, hit tests, layout switch persisted), the gamepad
# disconnect chord, launcher_disconnect, and mlxr-kb-snapshot (target
# kb-snapshots, run with ui-snapshots) for the panel's states. GPLv3.
set(KB_INCLUDES ${PROJECT_SOURCE_DIR}/tests/xrcore ${PROJECT_SOURCE_DIR}/ui/third_party ${PROJECT_SOURCE_DIR}/tests/m4)

add_executable(test_kb_layout test_kb_layout.c)
target_link_libraries(test_kb_layout PRIVATE mlxr_xrcore m)
target_include_directories(test_kb_layout PRIVATE ${KB_INCLUDES})
target_compile_options(test_kb_layout PRIVATE -Werror)
add_test(NAME keyboard_layout COMMAND test_kb_layout)

add_executable(test_kb_rules test_kb_rules.c)
target_link_libraries(test_kb_rules PRIVATE mlxr_stream mlxr_xrcore m)
target_include_directories(test_kb_rules PRIVATE ${KB_INCLUDES})
target_compile_options(test_kb_rules PRIVATE -Werror)
add_test(NAME keyboard_rules COMMAND test_kb_rules)

add_executable(test_kb_input test_kb_input.c)
target_link_libraries(test_kb_input PRIVATE m4_scene)
target_include_directories(test_kb_input PRIVATE ${KB_INCLUDES})
target_compile_options(test_kb_input PRIVATE -Werror)
target_compile_definitions(test_kb_input PRIVATE KB_TMP_DIR="${CMAKE_CURRENT_BINARY_DIR}")
mlxr_link_openxr(test_kb_input)
add_test(NAME keyboard_input COMMAND test_kb_input)

add_executable(test_kb_chord test_kb_chord.c)
target_link_libraries(test_kb_chord PRIVATE m4_scene)
target_include_directories(test_kb_chord PRIVATE ${KB_INCLUDES})
target_compile_options(test_kb_chord PRIVATE -Werror)
mlxr_link_openxr(test_kb_chord)
add_test(NAME keyboard_chord COMMAND test_kb_chord)

add_executable(test_kb_launcher test_kb_launcher.c)
target_link_libraries(test_kb_launcher PRIVATE mlxr_launcher)
target_include_directories(test_kb_launcher PRIVATE ${KB_INCLUDES})
target_compile_options(test_kb_launcher PRIVATE -Werror)
add_test(NAME keyboard_launcher COMMAND test_kb_launcher)

add_executable(mlxr-kb-snapshot kb_snapshot.c)
target_link_libraries(mlxr-kb-snapshot PRIVATE m4_scene)
target_include_directories(mlxr-kb-snapshot PRIVATE ${KB_INCLUDES})
mlxr_link_openxr(mlxr-kb-snapshot)
add_custom_target(kb-snapshots COMMAND mlxr-kb-snapshot ${PROJECT_BINARY_DIR}/ui-snapshots DEPENDS mlxr-kb-snapshot)
# `ninja ui-snapshots` writes these too, into the same folder
cmake_language(DEFER DIRECTORY ${PROJECT_SOURCE_DIR} CALL add_dependencies ui-snapshots kb-snapshots)
