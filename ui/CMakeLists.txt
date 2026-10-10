# UI lane: text rendering, settings and the CPU panel rasterisers.
# Owned by lane U (ui). GPLv3.
add_library(mlxr_ui STATIC
  canvas.c text.c ui_strings.c settings.c
  icons.c panels_common.c panel_cog.c panel_keyboard.c panel_picker.c panel_bar.c
  panel_dialogs.c panel_misc.c)
target_include_directories(mlxr_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(mlxr_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
target_link_libraries(mlxr_ui PUBLIC mlxr_xrcore PRIVATE m)
target_compile_definitions(mlxr_ui PUBLIC MLXR_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
set_target_properties(mlxr_ui PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/ui/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/ui ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
