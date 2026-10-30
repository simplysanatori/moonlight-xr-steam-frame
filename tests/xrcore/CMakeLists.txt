# Desktop unit tests for mlxr_xrcore, ported from the upstream app/src/test/cpp suites.
# Each prints "<module>: N checks, 0 failed" and exits non-zero on a failure.
set(XRCORE_TESTS math depthmap atlas roommesh layout audio rate gate pinch notice
                 glow grade keys controller headaim gamepad gamepad_frame clickhold pressfix
                 gamepad_guide gamepad_rumble ray bind hud battery)
foreach(t ${XRCORE_TESTS})
  add_executable(test_${t} test_${t}.c)
  target_link_libraries(test_${t} PRIVATE mlxr_xrcore m)
  target_compile_options(test_${t} PRIVATE -Werror)
  target_compile_definitions(test_${t} PRIVATE MLXR_ASSETS_DIR="${MLXR_ASSETS_DIR}")
  add_test(NAME xrcore_${t} COMMAND test_${t})
endforeach()
