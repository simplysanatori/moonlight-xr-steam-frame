add_executable(test_evkbd test_evkbd.c)
target_link_libraries(test_evkbd PRIVATE mlxr_stream mlxr_xr)
add_test(NAME stream_evkbd COMMAND test_evkbd)
# Lane av-latency: video frame pacing policy
add_executable(test_pacer test_pacer.c)
target_link_libraries(test_pacer PRIVATE mlxr_stream)
add_test(NAME stream_pacer COMMAND test_pacer)
# Lane perf: the RFI watchdog
add_executable(test_rfi_watch test_rfi_watch.c)
target_link_libraries(test_rfi_watch PRIVATE mlxr_stream)
add_test(NAME stream_rfi_watch COMMAND test_rfi_watch)
# Lane perf: the host frame cadence
add_executable(test_cadence test_cadence.c)
target_link_libraries(test_cadence PRIVATE mlxr_stream)
add_test(NAME stream_cadence COMMAND test_cadence)
# Lane au-scatter: every entry of a decode unit to the hardware decoder
add_executable(test_au_gather test_au_gather.c)
target_link_libraries(test_au_gather PRIVATE mlxr_stream)
add_test(NAME stream_au_gather COMMAND test_au_gather)
# Lane wave0-commonc: the common-c log lines session.c and rfi_watch.c parse still exist
add_executable(test_commonc_log test_commonc_log.c)
target_compile_definitions(test_commonc_log PRIVATE
  COMMONC_SRC_DIR="${PROJECT_SOURCE_DIR}/third_party/moonlight-common-c/src")
add_test(NAME stream_commonc_log COMMAND test_commonc_log)
# Lane wave3-session: auto-reconnect policy and its retry loop, driven by a fake session
add_executable(test_reconnect test_reconnect.c)
target_link_libraries(test_reconnect PRIVATE mlxr_stream)
add_test(NAME stream_reconnect COMMAND test_reconnect)
# Lane deploy-res-fix: the rate limit on common-c's "Unrecoverable frame" log lines
add_executable(test_log_throttle test_log_throttle.c)
target_link_libraries(test_log_throttle PRIVATE mlxr_stream)
add_test(NAME stream_log_throttle COMMAND test_log_throttle)
