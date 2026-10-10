# Tests for the audio lane (ctest names audio_*), built with -Werror.
set(AUDIO_TESTS surround hrtf pcmq opus pulse)
foreach(t ${AUDIO_TESTS})
  add_executable(test_audio_${t} test_${t}.c)
  target_link_libraries(test_audio_${t} PRIVATE mlxr_audio m pthread)
  target_include_directories(test_audio_${t} PRIVATE ${PROJECT_SOURCE_DIR}/tests/xrcore)
  target_compile_options(test_audio_${t} PRIVATE -Werror)
  target_compile_definitions(test_audio_${t} PRIVATE
    HRTF_DIR="${PROJECT_SOURCE_DIR}/assets/hrtf/kemar")
  add_test(NAME audio_${t} COMMAND test_audio_${t})
endforeach()
# Needs a PulseAudio server; skips (77) without one. Plays only into a temporary
# null sink it loads itself.
target_link_libraries(test_audio_pulse PRIVATE PkgConfig::PULSE_SIMPLE)
set_tests_properties(audio_pulse PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 90 RUN_SERIAL TRUE)
