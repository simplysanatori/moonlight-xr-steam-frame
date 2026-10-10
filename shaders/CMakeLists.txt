# Moonlight XR Native: lane S, the Vulkan ports of Moonlight XR's GL ES
# programs and the mlxr_passes library that records them. GPLv3.
#
# Not glsl_to_c: the programs share include files (common, grade, video)
# that glsl_to_c would not track, and the video passes are built twice,
# once with -DVIDEO_NV12=1. Same glslc flags otherwise.
set(MLXR_SHADER_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/common.glsl
  ${CMAKE_CURRENT_SOURCE_DIR}/grade.glsl
  ${CMAKE_CURRENT_SOURCE_DIR}/video.glsl)

# mlxr_shader(<outvar> <source> <output name> [glslc flags...])
function(mlxr_shader outvar src outname)
  set(out ${CMAKE_CURRENT_BINARY_DIR}/${outname}.inc)
  add_custom_command(OUTPUT ${out}
    COMMAND ${GLSLC} --target-env=vulkan1.2 -O -mfmt=c ${ARGN}
            ${CMAKE_CURRENT_SOURCE_DIR}/${src} -o ${out}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${src} ${MLXR_SHADER_INCLUDES}
    COMMENT "glslc ${outname}")
  set(${outvar} ${${outvar}} ${out} PARENT_SCOPE)
endfunction()

set(MLXR_PASSES_SPV)
foreach(s vertex.vert offset.frag glow_edge.frag glow.frag room.vert room.frag model.vert
          model.frag supersample.frag panel.vert panel.frag)
  mlxr_shader(MLXR_PASSES_SPV ${s} ${s})
endforeach()
# The passes that read the video: three planes, and NV12 through a
# VkSamplerYcbcrConversion
foreach(s warp upsample downscale ambi)
  mlxr_shader(MLXR_PASSES_SPV ${s}.frag ${s}.frag)
  mlxr_shader(MLXR_PASSES_SPV ${s}.frag ${s}.nv12.frag -DVIDEO_NV12=1)
endforeach()

add_library(mlxr_passes STATIC passes.c ${MLXR_PASSES_SPV})
target_include_directories(mlxr_passes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(mlxr_passes PUBLIC Vulkan::Vulkan PRIVATE m)
target_compile_options(mlxr_passes PRIVATE -Werror)

if(EXISTS ${PROJECT_SOURCE_DIR}/tests/shaders/CMakeLists.txt)
  add_subdirectory(${PROJECT_SOURCE_DIR}/tests/shaders ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
