# Tests for the input lane (ctest names input_*), built with -Werror.
set(INPUT_TESTS pad_out rumble)
foreach(t ${INPUT_TESTS})
  add_executable(test_input_${t} test_${t}.c)
  target_link_libraries(test_input_${t} PRIVATE mlxr_input_core m)
  target_include_directories(test_input_${t} PRIVATE ${PROJECT_SOURCE_DIR}/tests/xrcore)
  target_compile_options(test_input_${t} PRIVATE -Werror)
  add_test(NAME input_${t} COMMAND test_input_${t})
endforeach()

# The rumble threading test again under ThreadSanitizer, where the compiler has it
include(CheckCSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=thread")
check_c_source_compiles("int main(void) { return 0; }" MLXR_HAVE_TSAN)
unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_LINK_OPTIONS)
if(MLXR_HAVE_TSAN)
  add_executable(test_input_rumble_tsan test_rumble.c ${PROJECT_SOURCE_DIR}/input/rumble.c)
  target_link_libraries(test_input_rumble_tsan PRIVATE mlxr_xrcore Threads::Threads m)
  target_include_directories(test_input_rumble_tsan PRIVATE
    ${PROJECT_SOURCE_DIR}/input ${PROJECT_SOURCE_DIR}/tests/xrcore)
  target_compile_options(test_input_rumble_tsan PRIVATE -Werror -fsanitize=thread -g -O1)
  target_link_options(test_input_rumble_tsan PRIVATE -fsanitize=thread)
  add_test(NAME input_rumble_tsan COMMAND test_input_rumble_tsan)
  set_tests_properties(input_rumble_tsan PROPERTIES ENVIRONMENT "TSAN_OPTIONS=halt_on_error=1")
endif()

# The real sends, with no connection
add_executable(test_input_limelight test_limelight.c)
target_link_libraries(test_input_limelight PRIVATE mlxr_input m)
target_include_directories(test_input_limelight PRIVATE ${PROJECT_SOURCE_DIR}/tests/xrcore)
target_compile_options(test_input_limelight PRIVATE -Werror)
add_test(NAME input_limelight COMMAND test_input_limelight)
