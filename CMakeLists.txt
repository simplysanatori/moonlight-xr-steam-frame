# Moonlight XR Native - top-level build. Only the orchestrator edits this file;
# each lane owns its own subdirectory CMakeLists.txt. GPLv3.
cmake_minimum_required(VERSION 3.25)
project(MoonlightXRNative C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# OpenXR loader: built static from the Khronos SDK. SteamOS's own
# /usr/lib/libopenxr_loader.a is a 14 KB thin archive from gfxreconstruct and
# cannot be linked, and its headers lag the runtime.
set(DYNAMIC_LOADER OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_API_LAYERS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_SYSTEM_JSONCPP OFF CACHE BOOL "" FORCE)
set(PRESENTATION_BACKEND wayland CACHE STRING "" FORCE)
add_subdirectory(third_party/OpenXR-SDK EXCLUDE_FROM_ALL)
find_package(Vulkan REQUIRED)
find_program(GLSLC glslc REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)

# glsl_to_c(<target> <out_var> <shader files...>): compile each shader to a
# C initializer list (<name>.inc) in the build tree.
function(glsl_to_c target outvar)
  set(outs)
  foreach(src ${ARGN})
    get_filename_component(name ${src} NAME)
    set(out ${CMAKE_CURRENT_BINARY_DIR}/${name}.inc)
    add_custom_command(OUTPUT ${out}
      COMMAND ${GLSLC} --target-env=vulkan1.2 -O -mfmt=c ${CMAKE_CURRENT_SOURCE_DIR}/${src} -o ${out}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src}
      COMMENT "glslc ${name}")
    list(APPEND outs ${out})
  endforeach()
  set(${outvar} ${outs} PARENT_SCOPE)
endfunction()

# Link helper: the static OpenXR loader needs the C++ runtime; keep it static
# so the binary depends only on libraries SteamOS ships.
function(mlxr_link_openxr target)
  target_link_libraries(${target} PRIVATE openxr_loader OpenXR::headers Vulkan::Vulkan m dl pthread)
  target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
  set_target_properties(${target} PROPERTIES LINKER_LANGUAGE CXX)
endfunction()

add_subdirectory(hello)

# moonlight-common-c (static; links the host's libcrypto.so.3)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/moonlight-common-c EXCLUDE_FROM_ALL)

enable_testing()

# Lanes are added here by the orchestrator when their milestone starts.
foreach(lane backend video audio ui input rooms depth shaders xr stream app)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${lane}/CMakeLists.txt)
    add_subdirectory(${lane})
  endif()
endforeach()
