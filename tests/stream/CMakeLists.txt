add_executable(test_evkbd test_evkbd.c)
target_link_libraries(test_evkbd PRIVATE mlxr_stream mlxr_xr)
add_test(NAME stream_evkbd COMMAND test_evkbd)
