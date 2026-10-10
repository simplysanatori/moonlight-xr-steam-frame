# Tests for the ui lane (ctest names ui_*). Built with -Werror.
set(PANEL_OUT ${PROJECT_BINARY_DIR}/panels)

# Renders every panel and tab to PNGs for a person to look at
add_executable(mlxr-panel-test panel_render.c)
target_link_libraries(mlxr-panel-test PRIVATE mlxr_ui m)
target_include_directories(mlxr-panel-test PRIVATE ${PROJECT_SOURCE_DIR}/ui/third_party)
target_compile_definitions(mlxr-panel-test PRIVATE PANEL_RENDER_MAIN=1)
add_custom_target(panels COMMAND mlxr-panel-test ${PANEL_OUT} DEPENDS mlxr-panel-test)
