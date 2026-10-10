# Depth lane: the depth model on LiteRT + XNNPACK (CPU), its thread and
# hand-off, and mlxr-depth-bench. GPLv3.
#
# LiteRT is built outside the repo by depth/tools/build-litert.sh into one
# static archive; without it the depth targets are skipped with a message so
# the rest of the tree still builds.
set(MLXR_LITERT_DIR "$ENV{HOME}/Games/MoonlightXRNative/third-party-build/litert/dist"
    CACHE PATH "LiteRT C API staging from depth/tools/build-litert.sh")
set(MLXR_DEPTH_ASSETS ${PROJECT_SOURCE_DIR}/assets/depth)

if(NOT EXISTS ${MLXR_LITERT_DIR}/lib/libmlxr_litert.a)
  message(STATUS "depth: no LiteRT at ${MLXR_LITERT_DIR} (run depth/tools/build-litert.sh); depth targets skipped")
  return()
endif()

find_package(Threads REQUIRED)
add_library(mlxr_litert STATIC IMPORTED GLOBAL)
set_target_properties(mlxr_litert PROPERTIES
  IMPORTED_LOCATION ${MLXR_LITERT_DIR}/lib/libmlxr_litert.a
  INTERFACE_INCLUDE_DIRECTORIES ${MLXR_LITERT_DIR}/include
  INTERFACE_LINK_LIBRARIES "pthread;dl;m")

# The depth library. Anything linking it links C++ statically (the archive is
# C++), as mlxr_link_openxr does for the loader: use mlxr_link_depth().
add_library(mlxr_depth STATIC litert_backend.c depth_engine.c)
target_include_directories(mlxr_depth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mlxr_depth PUBLIC mlxr_litert mlxr_xrcore Threads::Threads m)
set_target_properties(mlxr_depth PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
target_compile_options(mlxr_depth PRIVATE -O2 -Werror)

function(mlxr_link_depth target)
  target_link_libraries(${target} PRIVATE mlxr_depth)
  target_link_options(${target} PRIVATE -static-libstdc++ -static-libgcc)
  set_target_properties(${target} PROPERTIES LINKER_LANGUAGE CXX)
endfunction()

add_executable(mlxr-depth-bench depth_bench.c)
target_compile_definitions(mlxr-depth-bench PRIVATE MLXR_DEPTH_ASSETS="${MLXR_DEPTH_ASSETS}")
target_compile_options(mlxr-depth-bench PRIVATE -O2 -Werror)
mlxr_link_depth(mlxr-depth-bench)

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/depth/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/depth ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
