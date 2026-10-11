# Backend tests (ctest names start with "backend_"). GPLv3.
set(FX ${CMAKE_CURRENT_SOURCE_DIR}/fixtures)

function(mlxr_backend_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE mlxr_backend)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${name} PRIVATE FIXTURE_DIR="${FX}")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

mlxr_backend_test(backend_test_parse test_parse.c)
mlxr_backend_test(backend_test_crypto test_crypto.c)
mlxr_backend_test(backend_test_identity test_identity.c)
mlxr_backend_test(backend_test_flow test_flow.c fake_host.c)
mlxr_backend_test(backend_test_netpath test_netpath.c fake_host.c)

if(TARGET mlxr_backend_ll)
  mlxr_backend_test(backend_test_glue test_glue.c)
  target_link_libraries(backend_test_glue PRIVATE mlxr_backend_ll)
endif()

# seed-from-moonlight-qt.sh against a synthetic Qt Moonlight.conf
add_test(NAME backend_test_seed
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_seed.sh
          ${PROJECT_SOURCE_DIR}/backend/seed-from-moonlight-qt.sh
          unused
          $<TARGET_FILE:mlxr-cli>)
set_tests_properties(backend_test_seed PROPERTIES TIMEOUT 60)

# mlxr-cli end to end against the fake host
add_executable(backend_fake_host_main fake_host_main.c fake_host.c)
target_link_libraries(backend_fake_host_main PRIVATE mlxr_backend)
target_include_directories(backend_fake_host_main PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME backend_test_cli
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_cli.sh $<TARGET_FILE:mlxr-cli> $<TARGET_FILE:backend_fake_host_main>)
set_tests_properties(backend_test_cli PROPERTIES TIMEOUT 60)
