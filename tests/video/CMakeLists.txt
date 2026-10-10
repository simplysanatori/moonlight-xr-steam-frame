# Moonlight XR Native - lane V test harness and ctest entries.
# Copyright (C) 2026 Moonlight XR Native contributors. GPLv3, see LICENSE.txt.
glsl_to_c(mlxr-decode-test DECODE_TEST_SPV nv12_to_rgba.comp)
add_executable(mlxr-decode-test decode_test.c ${DECODE_TEST_SPV})
target_include_directories(mlxr-decode-test PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(mlxr-decode-test PRIVATE mlxr_video m)

# Test streams are generated (host ffmpeg, libx264/libx265) into the build
# tree by a fixture, never committed.
set(VSAMPLES ${CMAKE_CURRENT_BINARY_DIR}/samples)
add_test(NAME video_samples COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/gen_samples.sh ${VSAMPLES})
set_tests_properties(video_samples PROPERTIES FIXTURES_SETUP video_samples TIMEOUT 300)

# The decoder tests share one hardware decoder: run them one at a time.
function(video_test name)
  add_test(NAME ${name} COMMAND mlxr-decode-test ${ARGN})
  set_tests_properties(${name} PROPERTIES FIXTURES_REQUIRED video_samples RESOURCE_LOCK iris_decoder
    TIMEOUT 60 ENVIRONMENT "LD_LIBRARY_PATH=")
endfunction()

# 2 s 1080p60 clips: 120 frames, colour bars checked against a software decode.
video_test(video_decode_h264_1080 ${VSAMPLES}/t1080_h264.h264 --fps 120 --expect-frames 120
  --expect-source-changes 1 --check-ref ${VSAMPLES}/ref_t1080_h264_f30.ppm --check-frame 30)
video_test(video_decode_hevc_1080 ${VSAMPLES}/t1080_hevc.hevc --fps 120 --expect-frames 120
  --expect-source-changes 1 --check-ref ${VSAMPLES}/ref_t1080_hevc_f30.ppm --check-frame 30)
# One AU in must give one frame out without a second AU.
video_test(video_lockstep_hevc ${VSAMPLES}/t1080_hevc.hevc --fps 0 --expect-frames 120)
# 1080p -> 1440p -> 1080p, each segment starting with an IDR.
video_test(video_reschange_hevc ${VSAMPLES}/reschange_hevc.hevc --fps 120 --expect-frames 180)
# The colour check has teeth: against a hue-rotated reference it must fail.
video_test(video_colour_check_negative ${VSAMPLES}/t1080_hevc.hevc --fps 120
  --check-ref ${VSAMPLES}/ref_t1080_hevc_f30_hue.ppm --check-frame 30)
set_tests_properties(video_colour_check_negative PROPERTIES WILL_FAIL TRUE)
