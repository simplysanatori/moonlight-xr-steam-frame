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

# The release version (VERSION, e.g. 0.6.0-alpha.1): the in-VR About tab shows it (xr/CMakeLists.txt)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION MLXR_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)

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

# Libraries the release carries in app/lib (FFmpeg 7.0.2, libxml2 2.13.9), built by
# tools/build-bundled-libs.sh: SteamOS updates move them to new sonames (FFmpeg 8 in the Steam Frame
# OS Preview), and a binary linked against the system's copies then does not start. When this is set
# (build.sh sets it when the bundle is built), they are linked from here instead of the system, and
# each shipped binary finds them in a lib/ beside it (mlxr_shipped_binary below).
set(MLXR_BUNDLED_LIBS_DIR "" CACHE PATH "Prefix of tools/build-bundled-libs.sh (its dist/); empty: system FFmpeg and libxml2")
if(MLXR_BUNDLED_LIBS_DIR)
  if(NOT EXISTS ${MLXR_BUNDLED_LIBS_DIR}/lib/pkgconfig/libavcodec.pc OR NOT EXISTS ${MLXR_BUNDLED_LIBS_DIR}/lib/pkgconfig/libxml-2.0.pc)
    message(FATAL_ERROR "MLXR_BUNDLED_LIBS_DIR=${MLXR_BUNDLED_LIBS_DIR} has no FFmpeg or libxml2 (run tools/build-bundled-libs.sh)")
  endif()
  set(ENV{PKG_CONFIG_PATH} "${MLXR_BUNDLED_LIBS_DIR}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  message(STATUS "FFmpeg and libxml2: bundled, from ${MLXR_BUNDLED_LIBS_DIR}")
endif()
# pkg_check_modules keeps its first answer in the cache: ask again when the choice changed
if(NOT "${MLXR_BUNDLED_LIBS_DIR}" STREQUAL "${_MLXR_BUNDLED_LIBS_DIR_SEEN}")
  unset(__pkg_config_checked_AV CACHE)
  unset(__pkg_config_checked_MLXR_XML2 CACHE)
  set(_MLXR_BUNDLED_LIBS_DIR_SEEN "${MLXR_BUNDLED_LIBS_DIR}" CACHE INTERNAL "")
endif()
pkg_check_modules(AV REQUIRED IMPORTED_TARGET libavcodec libavformat libavutil libswscale)

# mlxr_shipped_binary(<target>): a binary the release ships in app/. Its RUNPATH is $ORIGIN/lib, so it
# loads the bundled libraries from app/lib before the system's (and the system's when app/lib lacks
# one). In the build tree a lib/ symlink beside it points at the bundle, so it runs there too.
function(mlxr_shipped_binary target)
  set_target_properties(${target} PROPERTIES BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN/lib")
  target_link_options(${target} PRIVATE "LINKER:--enable-new-dtags")
  get_target_property(dir ${target} RUNTIME_OUTPUT_DIRECTORY)
  if(NOT dir)
    set(dir ${CMAKE_CURRENT_BINARY_DIR})
  endif()
  if(MLXR_BUNDLED_LIBS_DIR)
    file(MAKE_DIRECTORY ${dir})
    file(CREATE_LINK ${MLXR_BUNDLED_LIBS_DIR}/lib ${dir}/lib SYMBOLIC)
  elseif(IS_SYMLINK ${dir}/lib)
    file(REMOVE ${dir}/lib)
  endif()
endfunction()

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

# The Steam Frame controller model converter's tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/frame-models/CMakeLists.txt)
  add_subdirectory(tests/frame-models)
endif()

# Flat launcher tests (skip with code 77 until launcher/build.sh has run)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/launcher/CMakeLists.txt)
  add_subdirectory(tests/launcher)
endif()

# Dock icon installer and branding art tests
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/branding/CMakeLists.txt)
  add_subdirectory(tests/branding)
endif()

# bug-report.sh masking and the privacy check over the tracked files
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/tools/CMakeLists.txt)
  add_subdirectory(tests/tools)
endif()
