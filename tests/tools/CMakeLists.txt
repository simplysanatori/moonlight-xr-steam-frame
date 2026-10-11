# Moonlight XR for Frame, tests/tools: the bug report masks what a tester might
# not want to share, and the tracked tree passes the privacy check. GPLv3.
add_test(NAME tools_bug_report COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_bug_report.sh)
add_test(NAME tools_privacy_check COMMAND ${PROJECT_SOURCE_DIR}/tools/privacy-check.sh)
set_tests_properties(tools_bug_report tools_privacy_check PROPERTIES TIMEOUT 120)
