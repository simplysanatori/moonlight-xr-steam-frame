add_executable(test_evkbd test_evkbd.c)
target_link_libraries(test_evkbd PRIVATE mlxr_stream mlxr_xr)
add_test(NAME stream_evkbd COMMAND test_evkbd)
# Lane av-latency: video frame pacing policy
add_executable(test_pacer test_pacer.c)
target_link_libraries(test_pacer PRIVATE mlxr_stream)
add_test(NAME stream_pacer COMMAND test_pacer)
