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
